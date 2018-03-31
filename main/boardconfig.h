#pragma once

#define GPIO_IRLED 4
#define GPIO_OUTPUT_PIN_SEL (1ULL << GPIO_IRLED)

#define GPIO_IRRX 2
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_IRRX)

#ifdef __cplusplus
extern "C" {
#endif

void initBoard();

#ifdef __cplusplus
}
#endif
