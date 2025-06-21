#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/ssd1306.h"
#include "lib/matrizLed.h"
#include "lib/buzzer.h"
#include "font.h"
#include "pico/bootrom.h"

#define BOTAO_A 5
#define BOTAO_B 6
#define LED_BLUE_PIN 12
#define JOYSTICK_X 26
#define JOYSTICK_Y 27
#define WS2812_PIN 7
#define NUM_PIXELS 25
#define BUZZER_PIN 21

#define WIFI_SSID "SSID"
#define WIFI_PASS "PASSWORD"

#define STEP 0.1
#define LIMIT_MAX_RESET 25.0

uint16_t limite_percentual = LIMIT_MAX_RESET;
float nivel_percentual = 20;
volatile uint32_t last_time = 0;

bool leds_Normal[NUM_PIXELS] = {
    0, 1, 1, 1, 0,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    1, 1, 1, 1, 1,
    0, 1, 1, 1, 0};

bool leds_Alerta[NUM_PIXELS] = {
    0, 0, 0, 0, 0,
    1, 1, 1, 1, 1,
    0, 1, 1, 1, 0,
    0, 0, 1, 0, 0,
    0, 0, 0, 0, 0};

#define XSTR(x) STR(x)
#define STR(x) #x

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Reservatorio</title>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 10px; margin: 0; background: #f9f9f9; }"
    ".barra { width: 250px; height: 340px; background: #ddd; border-radius: 6px 6px 6px 6px; overflow: hidden; margin: 25px auto 25px auto; display: flex; flex-direction: column-reverse; }"
    ".preenchimento { width: 100%; transition: height 0.3s ease; background: #2196F3; }"
    ".label { font-weight: bold; margin-bottom: 5px; display: block; }"
    "@media (max-width: 600px) { .barra { height: 150px; } }"
    "</style>"
    "<script>"
    "function atualizar() {"
    "  fetch('/estado').then(res => res.json()).then(data => {"
    "    let x = Number(data.x);"
    "    let min = Number(data.min);"
    "    let max = Number(data.max);"
    "    let limite = Number(data.limite);"
    "    if (isNaN(x)) x = min;"
    "    if (x < min) x = min;"
    "    if (x > max) x = max;"
    "    let percentual = ((x - min) / (max - min)) * 100;"
    "    percentual = Math.min(100, Math.max(0, percentual));"
    "    document.getElementById('x_valor').innerText = x;"
    "    document.getElementById('barra_x').style.height = percentual + '%';"
    "    let statusText = (percentual >= limite) ? 'Acima (Desligada)' : 'Abaixo (Ligada)';"
    "    document.getElementById('status').innerText = statusText;"
    "  }).catch(err => {"
    "    console.error('Erro:', err);"
    "    document.getElementById('x_valor').innerText = '--';"
    "    document.getElementById('barra_x').style.height = '0%';"
    "    document.getElementById('status').innerText = '--';"
    "  });"
    "}"
    "function enviarConfig() {"
    "  const limite = document.getElementById('limite').value;"
    "  fetch(`/config?limite=${limite}`).then(res => {"
    "    if (res.ok) alert('Limite atualizado!');"
    "    else alert('Erro ao atualizar limite.');"
    "  });"
    "}"
    "setInterval(atualizar, 1000);"
    "</script></head><body>"

    "<h1>Monitor do Reservatório</h1>"
    "<p class='label'>Nível da água: <span id='x_valor'>--</span>%</p>"
    "<div class='barra'><div id='barra_x' class='preenchimento'></div></div>"
    "<p class='label'>Status (Da Bomba): <span id='status'>--</span></p>"

    "<hr><h2>Configurar Limite Percentual</h2>"
    "<form onsubmit='enviarConfig(); return false;'>"
    "<label for='limite'>Limite (%):</label>"
    "<input type='number' id='limite' name='limite' value='50' min='0' max='100'><br><br>"
    "<button type='submit'>Atualizar Limite</button></form>"

    "<hr style='margin-top: 20px;'>"
    "<p style='font-size: 15px; color: #336699; font-style: italic; max-width: 90%; margin: 10px auto;'>"
    "Visualização do valor analógico do eixo X via rede Wi-Fi com BitDogLab"
    "</p></body></html>";

struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

int get_param_val(const char *query, const char *key)
{
    const char *p = strstr(query, key);
    if (!p)
        return -1;
    p += strlen(key);
    if (*p != '=')
        return -1;
    return atoi(p + 1);
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /estado"))
    {
        adc_select_input(0);
        uint16_t x = adc_read();

        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"x\":%.2f,\"min\":%d,\"max\":%d,\"limite\":%d}\r\n",
                                nivel_percentual, 0, 100, limite_percentual);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    else if (strstr(req, "GET /config"))
    {
        int novo_limite = get_param_val(req, "limite");

        if (novo_limite >= 0 && novo_limite <= 100)
        {
            limite_percentual = novo_limite;
        }

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 2\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "OK");
    }
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    pbuf_free(p);
    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

void atualizar_leds()
{
    if (nivel_percentual >= limite_percentual)
    {
        set_one_led(10, 0, 0, leds_Alerta);
        gpio_put(LED_BLUE_PIN, 0); // Desliga o LED azul
    }
    else
    {
        set_one_led(0, 10, 0, leds_Normal);
        gpio_put(LED_BLUE_PIN, 1); // liga o LED azul

    }
}
void gpio_irq_handler(uint gpio, uint32_t event)
{

    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000)
    {
        if (gpio == BOTAO_B)
        {
            reset_usb_boot(0, 0);
        }
        else
        {
            limite_percentual = LIMIT_MAX_RESET;
        }
        last_time = current_time;
    }
}

int main()
{
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);

    stdio_init_all();
    sleep_ms(1000);

    adc_init();
    adc_gpio_init(JOYSTICK_X); // GPIO26 = ADC0
    adc_gpio_init(JOYSTICK_Y); // GPIO27 = ADC1

    matriz_init(WS2812_PIN);

    buzzer_init(BUZZER_PIN);

    ssd1306_t ssd;
    init_Display(&ssd);

    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();
    char str_x[5]; // Buffer para armazenar a string
    char status[24];
    char limite_str[8];
    bool cor = true;

    atualizar_leds();
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);

    while (true)
    {
        cyw43_arch_poll();

        adc_select_input(0);
        uint16_t adc_value_x = adc_read();

        int16_t mudanca = adc_value_x - 2048; // Ajuste para calcular a diferença do centro do joystick

        if (abs(mudanca) > 500)
        {
            nivel_percentual += (mudanca > 0) ? STEP : -STEP;
        }

        atualizar_leds();
        if (nivel_percentual >= limite_percentual)
        {
            pwm_set_enabled(slice_num, true); // Ativa PWM para buzzer
            buzzer_play(BUZZER_PIN, 1000, 200);
            buzzer_play(BUZZER_PIN, 1500, 300);
        }
        else
        {
            pwm_set_enabled(slice_num, false); // Desativa PWM 
        }

        sprintf(status, (nivel_percentual >= limite_percentual) ? "ACIMA" : "ABAIXO");
        sprintf(str_x, "%.2f%%", nivel_percentual);
        sprintf(limite_str, "%d%%", limite_percentual);

        ssd1306_fill(&ssd, !cor);
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);
        ssd1306_line(&ssd, 3, 30, 123, 30, cor);
        ssd1306_draw_string(&ssd, "Limite: ", 10, 6);
        ssd1306_draw_string(&ssd, limite_str, 70, 6);
        ssd1306_draw_string(&ssd, ip_str, 10, 20);
        ssd1306_draw_string(&ssd, "NIVEL / STATUS", 10, 35);
        ssd1306_draw_string(&ssd, str_x, 10, 52);
        ssd1306_draw_string(&ssd, status, 70, 52);
        ssd1306_send_data(&ssd);

        sleep_ms(200);
    }

    cyw43_arch_deinit();
    return 0;

    cyw43_arch_deinit();
    return 0;
}
