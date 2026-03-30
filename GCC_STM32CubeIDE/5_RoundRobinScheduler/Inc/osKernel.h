#ifndef __OSKERNEL_H__
#define __OSKERNEL_H__
#include <stdint.h>
#include "stm32f4xx.h"

void osKernelStackInit(int i);
void osKernelLaunch(uint32_t quanta);
uint8_t osKernelAddThreads(void(*task0)(void), void(*task1)(void), void(*task2)(void));

#endif
