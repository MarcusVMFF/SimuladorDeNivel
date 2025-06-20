#include "matrizLed.h"
#include "generated/ws2812.pio.h"

// Ajuste pio0 e sm=0 se você estiver usando outros
#define WS2812_PIO pio0
#define WS2812_SM 0

void matriz_init(uint8_t WS2812_PIN)
{
    // inicialização da matriz de led
    uint offset = pio_add_program(WS2812_PIO, &ws2812_program);
    ws2812_program_init(WS2812_PIO, WS2812_SM, offset, WS2812_PIN, 800000, IS_RGBW);
}

uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(WS2812_PIO, WS2812_SM, pixel_grb << 8u);
}

void set_one_led(uint8_t r, uint8_t g, uint8_t b, bool led_buffer[])
{
    uint32_t color = urgb_u32(r, g, b);
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        put_pixel(led_buffer[i] ? color : 0);
    }
}