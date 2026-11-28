#include "types.h"
#include "gic.h"

#define GICD_BASE 0x08000000u
#define GICC_BASE 0x08010000u

#define GICD_CTLR       0x0000u
#define GICD_ISENABLER  0x0100u
#define GICD_IPRIORITYR 0x0400u
#define GICD_ITARGETSR  0x0800u

#define GICC_CTLR 0x0000u
#define GICC_PMR  0x0004u
#define GICC_IAR  0x000Cu
#define GICC_EOIR 0x0010u

static inline void wr(uint32_t base, uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(base + reg) = value;
}

static inline uint32_t rd(uint32_t base, uint32_t reg) {
    return *(volatile uint32_t*)(base + reg);
}

void arm_gic_init(void) {
    wr(GICD_BASE, GICD_CTLR, 0);
    wr(GICC_BASE, GICC_CTLR, 0);

    wr(GICD_BASE, GICD_ISENABLER, 1u << 30);
    *(volatile uint8_t*)(GICD_BASE + GICD_IPRIORITYR + 30) = 0x80u;
    *(volatile uint8_t*)(GICD_BASE + GICD_ITARGETSR + 30) = 0x01u;

    wr(GICC_BASE, GICC_PMR, 0xFFu);
    wr(GICC_BASE, GICC_CTLR, 1u);
    wr(GICD_BASE, GICD_CTLR, 1u);
}

uint32_t arm_gic_ack(void) {
    return rd(GICC_BASE, GICC_IAR);
}

void arm_gic_eoi(uint32_t iar) {
    wr(GICC_BASE, GICC_EOIR, iar);
}
