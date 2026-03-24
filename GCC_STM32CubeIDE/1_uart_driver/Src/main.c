
//header files
#include "led.h"
#include "uart.h"
#include <stdio.h>

int main(void){
	led_init();
	uart_tx_init();

	while(1){
		printf("Hello from STM32F4....\n\r");

		led_on();

		for(int i = 0; i < 90000; i++){			//create short delay by looping 90,000 times
		}

		led_off();

		for(int i = 0; i < 90000; i++){
		}

	}
}