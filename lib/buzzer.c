#ifndef BUZZER_H
#define BUZZER_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"

// Inicializa o buzzer (configura o pino como PWM)
void buzzer_init(uint BUZZER_PIN)
{
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
}

// Emite um som no buzzer com a frequência e duração desejadas
void buzzer_play(uint BUZZER_PIN, uint freq, uint duration_ms)
{
    // Obtém o número do slice PWM correspondente ao pino
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);

    // Calcula o valor do contador TOP para alcançar a frequência desejada
    uint top = 125000000 / freq;

    // Define o valor máximo do contador PWM (wrap)
    pwm_set_wrap(slice_num, top);

    // Define o duty cycle
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(BUZZER_PIN), (top * 5) / 100);

    // Ativa o PWM
    pwm_set_enabled(slice_num, true);

    // Toca o som pelo tempo especificado
    sleep_ms(duration_ms);

    // Desativa o PWM
    pwm_set_enabled(slice_num, false);

    // Pausa entre os tons
    sleep_ms(20);
}

#endif
