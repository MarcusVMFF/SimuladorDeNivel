#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

// pinos da matriz de led
#define IS_RGBW false
#define NUM_PIXELS 25

#ifdef __cplusplus
extern "C"
{
#endif

    void matriz_init(uint8_t WS2812_PIN);
    uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b);
    void put_pixel(uint32_t pixel_grb);
    void set_one_led(uint8_t r, uint8_t g, uint8_t b, bool led_buffer[]);

#ifdef __cplusplus
}
#endif
