#include "led.h"

#define GPIO_EN			(1u << 0)		// 0b 0000 0000 0000 0000 0000 0000 0000 0001
#define GPIO_OUT		(1u << 10)		// 0b 0000 0000 0000 0000 0000 0100 0000 0000
#define GPIO_OUT2		(1u << 11)		// 0b 0000 0000 0000 0000 0000 1000 0000 0000
#define LED_PIN 		(1u << 5)		// 0b 0000 0000 0000 0000 0000 0000 0010 0000

void led_init(void){
	/* Enable clock access to led port (Port A) */
	RCC -> AHB1ENR |= GPIO_EN;		//set to bit 0 to 1 to enable led port

	/* Set led pin as output */
	GPIOA -> MODER |= GPIO_OUT;		//set MODER to 1
	
	GPIOA -> MODER &= ~GPIO_OUT2;	//clear MODER bit (0)
}

void led_on(void){
	/* Set led pin HIGH (PA5) */
	GPIOA -> ODR |= LED_PIN;			//set LED_Pin to 1
}

void led_off(void){
	/* Set led pin LOW (PA5) */
	GPIOA -> ODR &= ~LED_PIN;			//set LED_Pin to 0
}
