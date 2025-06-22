#ifndef BUZZER_H
#define BUZZER_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"

// Inicializa o buzzer: configura o pino como PWM e o clock divisor
void buzzer_init(uint BUZZER_PIN)
{
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_clkdiv(slice_num, 1.0f); 
}


void buzzer_play(uint BUZZER_PIN, uint freq_hz)
{
    if (freq_hz == 0) { // Se a frequência for 0, desligue o buzzer
        uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
        pwm_set_enabled(slice_num, false);
        return;
    }

    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint channel = pwm_gpio_to_channel(BUZZER_PIN);

    uint top = 125000000 / freq_hz;

    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, channel, top / 2); // 50% duty cycle
    pwm_set_enabled(slice_num, true); // Liga o PWM
}

#endif