#pragma once

#define GPIO_IRLED 4
#define GPIO_OUTPUT_PIN_SEL (1ULL << GPIO_IRLED)

#ifdef __cplusplus
extern "C" {
#endif

void initBoard();

#ifdef __cplusplus
}
#endif
