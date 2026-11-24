#ifndef MOCK_IOP_KERNEL_H
#define MOCK_IOP_KERNEL_H
#include <stdint.h>
unsigned sceSifGetReg(unsigned reg);
void sceSifSetReg(unsigned reg, unsigned value);
void sceSifStopDma(void);
#endif
