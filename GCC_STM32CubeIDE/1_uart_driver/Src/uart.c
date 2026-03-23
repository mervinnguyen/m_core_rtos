#include "uart.h"
#include "stm32f4xx.h"

#define GPIO_EN			(1u << 0)		// 0b 0000 0000 0000 0000 0000 0000 0000 0001

void uart_tx_init(void){	//sending byte data
	/* Enable class access to GPIO A*/
	RCC -> AHB1ENR |= GPIO_EN;		//set to bit 0 to 1 to enable UART TX
	/* Set PA2 mode to alternate function mode*/
	GPIOA -> MODER &= ~(1u << 4);		//set bit 4 to 0
	GPIOA -> MODER |= (1u << 5);		//set bit 5 to 1

	/* Set alternate function type to AF7 (UART2_TX)*/

	/* Enable clock access to UART*/
	/* Configure baudrate*/
	/* Configure transfer direction*/
	/* Enable UART module*/
	GPIOA -> AF07 =
}

void uart_rx_init(void){	//receiving byte data

}
