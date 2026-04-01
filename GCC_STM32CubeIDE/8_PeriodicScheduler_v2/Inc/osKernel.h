#ifndef __OSKERNEL_H__
#define __OSKERNEL_H__
#include <stdint.h>
#include "stm32f4xx.h"

#define PERIOD    100		/*Period is 100 *quanta, e.g. quanta = 10 -> 100*10 = 1000*/
#define SR_UIF		(1u << 0)

void osKernelStackInit(int i);
void osKernelLaunch(uint32_t quanta);
uint8_t osKernelAddThreads(void(*task0)(void), void(*task1)(void), void(*task2)(void));
void osSchedulerLaunch(void);
void osKernelInit(void);
void osThreadYield(void);
void task3(void);
void osSchedulerRoundRobin(void);
void tim2_1hz_interrupt_init(void);


#endif
