
//header files
#include "led.h"

int main(void){
	led_init();

	while(1){
		led_on();
		for(int i = 0; i < 90000; i++){			//create short dealy by looping 90,000 times
		}
		led_off();
		for(int i = 0; i < 90000; i++){
		}
	}
}
