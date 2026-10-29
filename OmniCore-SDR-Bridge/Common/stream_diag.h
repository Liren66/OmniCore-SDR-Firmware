#ifndef STREAM_DIAG_H
#define STREAM_DIAG_H
#include "ch32h417.h"
extern volatile uint32_t StreamDiag[64];
extern volatile uint32_t StreamEvents[256][8];
static inline uint32_t stream_cycles(void) {
    uint32_t n; __asm volatile("csrr %0, mcycle" : "=r"(n)); return n;
}
void StreamDiag_Event(uint32_t kind);
uint16_t StreamDiag_Read(uint8_t *out, uint16_t page, uint16_t reset);
#endif
