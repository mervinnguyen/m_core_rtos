#include "led.h"
#include "uart.h"
#include <stdio.h>
#include "timebase.h"
#include "osKernel.h"

#define		QUANTA 	10

typedef uint32_t TaskProfiler;

TaskProfiler Task0_Profiler, Task1_Profiler, Task2_Profiler, pTask1_Profiler;

void motor_run(void);
void motor_stop(void);
void valve_open(void);
void valve_close(void);

void task0(void){
	while(1){
		Task0_Profiler++;
		osThreadYield();
	}
}

void task1(void){
	while(1){
		Task1_Profiler++;
		valve_open();
	}
}

void task2(void){
	while(1){
		Task2_Profiler++;
	}
}

void task3(void){
	while(1){
		pTask1_Profiler++;
	}
}

int main(){

	/*Initialize UART*/
	uart_tx_init();

	/* Initialize Kernel*/
	osKernelInit();

	/*Add Thread*/
	osKernelAddThreads(&task0, &task1, &task2);

	/*Set RoundRobin time quanta*/
	osKernelLaunch(QUANTA);

	while(1){

	}
}
void motor_run(void){
	printf("Motor is starting ...\n");
}

void motor_stop(void){
	printf("Motor is stopping ...\n\r");
}

void valve_open(void){
	printf("Valve is opening...\n");
}

void valve_close(void){
	printf("Valve is closing...\n\r");
}

