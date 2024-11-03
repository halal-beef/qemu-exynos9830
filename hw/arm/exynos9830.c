#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/qdev-properties.h"
#include "hw/arm/exynos9830.h"
#include "hw/arm/boot.h"
#include "target/arm/gtimer.h"
#include "sysemu/sysemu.h"

// SoC specific code

#define SBSA_GTIMER_HZ 26000000 // CPU 0 Wont need this as for some reason Samsung doesn't set cntfrq for CPU 0, but 1-5 (Mongoose M5 hasnt been implemented) do set cntfrq */

const hwaddr exynos9830_memmap[] = {
    [EXYNOS9830_GIC_DIST] = 0x10101000,
    [EXYNOS9830_GIC_REDIST] = 0x10102000,
    [EXYNOS9830_SDRAM] = 0x80000000
};

static void exynos9830_init(Object *obj)
{
    EXYNOS9830State *s = EXYNOS9830(obj);

    s->memmap = exynos9830_memmap;

    /* Init first cluster (cortex-a55) */
    for (int i = 0; i < EXYNOS9830_NCPUS_A55; i++) {
        object_initialize_child(obj, "cpu[*]", &s->cpus[i], ARM_CPU_TYPE_NAME("cortex-a55"));
    }

    /* Init second cluster (cortex-a76) */
    for (int i = EXYNOS9830_NCPUS_A55; i < EXYNOS9830_NCPUS_A76 + EXYNOS9830_NCPUS_A55; i++) {
        object_initialize_child(obj, "cpu[*]", &s->cpus[i - EXYNOS9830_NCPUS_A55], ARM_CPU_TYPE_NAME("cortex-a76"));
    }

    /* TODO Init third cluster (Mongoose)? */

    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());
}

static void exynos9830_realize(DeviceState *dev, Error **err)
{
    EXYNOS9830State *s = EXYNOS9830(dev);
    QList *redist_region_count;

    /* CPUs */
    for (int i = 0; i < EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76; i++) {
	if (i != 0) {
            object_property_set_int(OBJECT(&s->cpus[i]), "cntfrq", SBSA_GTIMER_HZ, &error_abort);
	}
	else {
	    object_property_set_int(OBJECT(&s->cpus[i]), "cntfrq", 0, &error_abort); // Intenionally break cntfrq on CPU0 as Samsung does the same.
	}

        qdev_realize(DEVICE(&s->cpus[i]), NULL, &error_fatal);
    }

    /* GIC */
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", GIC_IRQ_NUM + GIC_INTERNAL);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 3);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76);
    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76);
    qdev_prop_set_array(DEVICE(&s->gic), "redist-region-count", redist_region_count);
    sysbus_realize(SYS_BUS_DEVICE(&s->gic), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0, s->memmap[EXYNOS9830_GIC_DIST]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1, s->memmap[EXYNOS9830_GIC_REDIST]);

    for (int i = 0; i < EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76; i++) {
        DeviceState *cpu = DEVICE(&s->cpus[i]);
        int ppibase = GIC_IRQ_NUM + i * GIC_INTERNAL + GIC_NR_SGIS;

        const int timer_irq[] = {
            [GTIMER_PHYS] = 14,
            [GTIMER_VIRT] = 11,
            [GTIMER_HYP]  = 10,
            [GTIMER_SEC]  = 13,
        };

        for (int j = 0; j < ARRAY_SIZE(timer_irq); j++) {
            qdev_connect_gpio_out(cpu, j, qdev_get_gpio_in(DEVICE(&s->gic), ppibase + timer_irq[j]));
        }

        qemu_irq irq = qdev_get_gpio_in(DEVICE(&s->gic),
                                        ppibase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpu, "gicv3-maintenance-interrupt",
                                    0, irq);
        qdev_connect_gpio_out_named(cpu, "pmu-interrupt", 0,
                qdev_get_gpio_in(DEVICE(&s->gic), ppibase + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i, qdev_get_gpio_in(cpu, ARM_CPU_IRQ));

        // TODO: Not sure if these are correct
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76,
                           qdev_get_gpio_in(cpu, ARM_CPU_FIQ));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 2 * EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76,
                           qdev_get_gpio_in(cpu, ARM_CPU_VIRQ));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 3 * EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76,
                           qdev_get_gpio_in(cpu, ARM_CPU_VFIQ));
    }

    /* Timer */
    // TODO: Find some implementation for armv8-timer

    /* TODO: UART 
    serial_mm_init(get_system_memory(), s->memmap[EXYNOS9830_UART0], 2,
                   qdev_get_gpio_in(DEVICE(&s->gic), EXYNOS9830_GIC_SPI_UART0),
                   115200, serial_hd(0), DEVICE_NATIVE_ENDIAN); */
}

static void exynos9830_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = exynos9830_realize;
    dc->user_creatable = false;
}

static const TypeInfo exynos9830_type_info = {
    .name = TYPE_EXYNOS9830,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(EXYNOS9830State),
    .instance_init = exynos9830_init,
    .class_init = exynos9830_class_init,
};

static void exynos9830_register_types(void)
{
    type_register_static(&exynos9830_type_info);
}

type_init(exynos9830_register_types)


// Machine specific code

static struct arm_boot_info exynos9830_boot_info;

static void exynos9830_mach_init(MachineState *machine)
{
    EXYNOS9830State *state;

    state = EXYNOS9830(object_new(TYPE_EXYNOS9830));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(state));
    object_unref(OBJECT(state));

    qdev_realize(DEVICE(state), NULL, &error_abort);

    /* SDRAM */
    memory_region_add_subregion(get_system_memory(), state->memmap[EXYNOS9830_SDRAM],
                                machine->ram);
    
    exynos9830_boot_info.loader_start = state->memmap[EXYNOS9830_SDRAM];
    exynos9830_boot_info.ram_size = machine->ram_size;
    exynos9830_boot_info.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(&state->cpus[0], machine, &exynos9830_boot_info);
}

static void exynos9830_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a55"),
	ARM_CPU_TYPE_NAME("cortex-a76"),
        NULL
    };

    mc->desc = "EXYNOS9830 test platform";
    mc->init = exynos9830_mach_init;
    mc->min_cpus = EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76;
    mc->max_cpus = EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76;
    mc->default_cpus = EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a55");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "ram";
}

DEFINE_MACHINE("exynos9830", exynos9830_machine_init)
