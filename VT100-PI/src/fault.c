// Minimal hard-fault capture. On a fault we stash the faulting PC/LR and fault
// status registers in uninitialised RAM (survives the reboot) and reset; the
// next boot reads them back so we can see exactly where it died.
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "fault.h"

#define FAULT_MAGIC 0xFA017ABCu

static struct {
    uint32_t magic, pc, lr, cfsr, hfsr, reboots;
} __uninitialized_ram(fault_rec);

void hardfault_handler_c(uint32_t pc, uint32_t lr) {
    if (fault_rec.magic != FAULT_MAGIC) fault_rec.reboots = 0;
    fault_rec.magic = FAULT_MAGIC;
    fault_rec.pc    = pc;
    fault_rec.lr    = lr;
    fault_rec.cfsr  = *(volatile uint32_t *)0xE000ED28;   // SCB->CFSR
    fault_rec.hfsr  = *(volatile uint32_t *)0xE000ED2C;   // SCB->HFSR
    // Reboot to preserve+show the fault on next boot, but never loop forever:
    // after a few tries just halt so a boot-path fault can't brick the board.
    if (++fault_rec.reboots <= 3) watchdog_reboot(0, 0, 0);
    while (1) __asm volatile("wfi");
}

// Override the SDK's weak hard-fault vector. Recover the stacked PC/LR (from
// MSP or PSP per EXC_RETURN) and hand them to the C handler.
void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "movs r0, #4       \n"
        "mov  r1, lr       \n"
        "tst  r0, r1       \n"
        "beq  1f           \n"
        "mrs  r0, psp      \n"
        "b    2f           \n"
        "1: mrs r0, msp    \n"
        "2: ldr r1, [r0, #24] \n"   // stacked PC
        "   ldr r2, [r0, #20] \n"   // stacked LR
        "   mov r0, r1        \n"
        "   mov r1, r2        \n"
        "   b hardfault_handler_c \n"
    );
}

int fault_get(uint32_t *pc, uint32_t *lr, uint32_t *cfsr, uint32_t *hfsr) {
    if (fault_rec.magic != FAULT_MAGIC) return 0;
    *pc = fault_rec.pc; *lr = fault_rec.lr;
    *cfsr = fault_rec.cfsr; *hfsr = fault_rec.hfsr;
    fault_rec.magic = 0;
    return 1;
}
