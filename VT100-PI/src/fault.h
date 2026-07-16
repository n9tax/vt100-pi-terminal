#ifndef FAULT_H
#define FAULT_H
#include <stdint.h>
// If the previous boot ended in a hard fault, fill *pc/*lr/*cfsr/*hfsr and
// return 1 (clearing the record); otherwise return 0.
int fault_get(uint32_t *pc, uint32_t *lr, uint32_t *cfsr, uint32_t *hfsr);
#endif
