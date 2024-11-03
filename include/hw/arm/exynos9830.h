#ifndef EXYNOS9830_H
#define EXYNOS9830_H

#include "qom/object.h"
#include "hw/intc/arm_gicv3.h"
#include "target/arm/cpu.h"

#define EXYNOS9830_NCPUS_A55	(4)
#define EXYNOS9830_NCPUS_A76    (2)
/* No emulation available for Mongoose M5 (Lion) */
#define TYPE_EXYNOS9830     	"exynos9830"

enum {
    EXYNOS9830_GIC_DIST,
    EXYNOS9830_GIC_REDIST,
    EXYNOS9830_SDRAM
};

// GIC
#define GIC_IRQ_NUM (288)
#define ARCH_GIC_MAINT_IRQ (9)
#define VIRTUAL_PMU_IRQ (7)

/*enum {
    EXYNOS9830_GIC_SPI_UART0 = 91,
}; TODO */

OBJECT_DECLARE_SIMPLE_TYPE(EXYNOS9830State, EXYNOS9830)

struct EXYNOS9830State {
    DeviceState parent_obj;

    ARMCPU cpus[EXYNOS9830_NCPUS_A55 + EXYNOS9830_NCPUS_A76];
    const hwaddr *memmap;
    GICv3State gic;
};

#endif // EXYNOS9830_H
