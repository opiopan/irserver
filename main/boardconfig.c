#include "esp_ota_ops.h"
#include "driver/gpio.h"

#include "boardconfig.h"

void initBoard(){
    gpio_config_t  output= {
	.intr_type = GPIO_PIN_INTR_DISABLE,
	.pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
	.mode = GPIO_MODE_OUTPUT,
	.pull_down_en = 0,
	.pull_up_en = 0
    };
    gpio_config(&output);

    gpio_config_t input = {
	.intr_type = GPIO_PIN_INTR_DISABLE,
	.pin_bit_mask = GPIO_INPUT_PIN_SEL,
	.mode = GPIO_MODE_INPUT,
	.pull_down_en = 0,
	.pull_up_en = 0
    };
    gpio_config(&input);
}
