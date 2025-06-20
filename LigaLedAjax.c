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
#include "font.h"
#include "pico/bootrom.h"
#include "lib/functions.h"

#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_SW 22
#define BOIA_ADC_PIN 28
#define BOMBA_ESVAZIAR_PIN 16
#define BOMBA_ENCHER_PIN 17

// --- Variáveis Globais do Sistema de Nível ---
int g_nivel_min_pc = 20;              // Nível mínimo padrão para ligar a bomba de encher
int g_nivel_max_pc = 40;              // Nível máximo padrão para desligar a bomba de encher
float g_nivel_boia_pc = 0.0f;         // Nível atual da boia em percentual
bool g_bomba_encher_ligada = false;   // Estado da bomba de encher
bool g_bomba_esvaziar_ligada = false; // Estado da bomba de esvaziar

float nivel_percentual_compat = 0.0f;
uint16_t limite_percentual_compat = 0;

volatile uint32_t last_time = 0;

#define XSTR(x) STR(x)
#define STR(x) #x

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Reservatorio</title>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 10px; margin: 0; background: #f9f9f9; }"
    ".barra { width: 30%; background: #ddd; border-radius: 6px; overflow: hidden; margin: 0 auto 15px auto; height: 20px; }"
    ".preenchimento { height: 100%; transition: width 0.3s ease; background: #2196F3; }"
    ".label { font-weight: bold; margin-bottom: 5px; display: block; }"
    "@media (max-width: 600px) { .barra { width: 80%; } }"
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
    "    document.getElementById('barra_x').style.width = percentual + '%';"
    "    let statusText = (percentual >= limite) ? 'Acima (Desligada)' : 'Abaixo (Ligada)';"
    "    document.getElementById('status').innerText = statusText;"
    "  }).catch(err => {"
    "    console.error('Erro:', err);"
    "    document.getElementById('x_valor').innerText = '--';"
    "    document.getElementById('barra_x').style.width = '0%';"
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

        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"x\":%.2f,\"min\":%d,\"max\":%d,\"limite\":%d}\r\n",
                                nivel_percentual_compat, 0, 100, (uint16_t)g_nivel_min_pc); // Usa g_nivel_min_pc para 'limite'

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // O endpoint GET /config do HTML original não será mais processado aqui.
    // O formulário no HTML antigo não terá efeito.
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

// --- Configuração dos Níveis Discretos da Boia ---
// Valores ADC que definem cada nível. Do nível 0 ao nível 12.
static const uint16_t ADC_LEVEL_VALUES[] = {
    200,  // Nível 0 (0%)
    1200, // Nível 1
    1300, // Nível 2
    1400, // Nível 3
    1500, // Nível 4
    1600, // Nível 5
    1700, // Nível 6
    1800, // Nível 7
    2000, // Nível 8
    2100, // Nível 9
    2200, // Nível 10
    2400, // Nível 11
    2800  // Nível 12 (80%)
};
static const int NUM_ADC_LEVELS = sizeof(ADC_LEVEL_VALUES) / sizeof(ADC_LEVEL_VALUES[0]);
static const float MAX_PERCENTAGE_MAPPED = 80.0f; // O último nível (2800) corresponde a 80%

// Função para converter leitura ADC da boia para percentual
int adc_para_percentual_boia(uint16_t adc_valor)
{
    // Se o valor ADC for menor ou igual ao primeiro nível, retorna 0%
    if (adc_valor <= ADC_LEVEL_VALUES[0])
    {
        return 0; // Percentual do nível 0
    }
    // Se o valor ADC for maior ou igual ao último nível, retorna o percentual máximo mapeado (80%)
    if (adc_valor >= ADC_LEVEL_VALUES[NUM_ADC_LEVELS - 1])
    {
        return (int)MAX_PERCENTAGE_MAPPED; // Percentual do último nível
    }

    for (int i = NUM_ADC_LEVELS - 2; i >= 0; --i)
    {
        if (adc_valor >= ADC_LEVEL_VALUES[i])
        {
            // O valor ADC atingiu ou ultrapassou o limiar do nível 'i'.
            // O percentual é (i / (total de intervalos)) * percentual_maximo
            float percentage = (i / (float)(NUM_ADC_LEVELS - 1)) * MAX_PERCENTAGE_MAPPED;
            return (int)percentage;
        }
    }
    return 0;
}

void gpio_irq_handler(uint gpio, uint32_t event)
{
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000) // Debounce de 200ms
    {
        if (gpio == BOTAO_A)
        {
            g_nivel_min_pc = 5;                        // Reseta para padrão
            g_nivel_max_pc = 25;                       // Reseta para padrão
            limite_percentual_compat = g_nivel_min_pc; // Atualiza para HTML
            printf("Niveis resetados para Min: %d%% Max: %d%%\n", g_nivel_min_pc, g_nivel_max_pc);
        }
        else if (gpio == BOTAO_B)
        {
            printf("Botao B pressionado: Entrando no modo bootloader USB...\n");
            reset_usb_boot(0, 0);
        }
        last_time = current_time;
    }
}

#define CREDENTIAL_BUFFER_SIZE 64 // Tamanho do buffer para armazenar as credenciais

char WIFI_SSID[CREDENTIAL_BUFFER_SIZE]; // Substitua pelo nome da sua rede Wi-Fi
char WIFI_PASS[CREDENTIAL_BUFFER_SIZE]; // Substitua pela senha da sua rede Wi-Fi

int main()
{
    // Inicializa as variáveis de compatibilidade com o HTML
    nivel_percentual_compat = 0;               // Valor inicial
    limite_percentual_compat = g_nivel_min_pc; // Usa o g_nivel_min_pc padrão

    stdio_init_all();
    sleep_ms(1000);

    adc_init();
    adc_gpio_init(BOIA_ADC_PIN);

    ssd1306_t ssd;
    init_Display(&ssd);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Entrar no", 0, 0);
    ssd1306_draw_string(&ssd, "Monitor Serial", 0, 14);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

    waitUSB();
    wifi_Credentials(WIFI_SSID, WIFI_PASS);

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

    // Inicializar GPIOs APÓS cyw43_arch_init() E APÓS TENTATIVA DE CONEXÃO WIFI
    // Isso garante que o Wi-Fi não sobrescreva as configurações dos GPIOs.
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled(BOTAO_B, GPIO_IRQ_EDGE_FALL, true);

    gpio_init(BOTAO_SW);
    gpio_set_dir(BOTAO_SW, GPIO_IN);
    gpio_pull_up(BOTAO_SW);
    gpio_set_irq_enabled(BOTAO_B, GPIO_IRQ_EDGE_FALL, true);

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();

    // Strings para o display OLED
    char str_nivel_boia_display[10];
    char str_niveis_config_display[25]; // "Min: XX% Max: YY%"
    char str_bomba_encher_status_display[10];
    char str_bomba_esvaziar_status_display[10];
    bool cor = true;

    gpio_init(BOMBA_ESVAZIAR_PIN);
    gpio_set_dir(BOMBA_ESVAZIAR_PIN, GPIO_OUT);
    gpio_put(BOMBA_ESVAZIAR_PIN, 1); // Começa desligada
    g_bomba_esvaziar_ligada = false;

    gpio_init(BOMBA_ENCHER_PIN);
    gpio_set_dir(BOMBA_ENCHER_PIN, GPIO_OUT);
    gpio_put(BOMBA_ENCHER_PIN, 1); // Começa desligada
    g_bomba_encher_ligada = false;

    while (true)
    {
        cyw43_arch_poll();

        adc_select_input(2); // Seleciona ADC2 (GPIO28) para leitura da boia
        uint16_t adc_valor_boia = adc_read();
        g_nivel_boia_pc = (float)adc_para_percentual_boia(adc_valor_boia);

        // Atualiza variáveis de compatibilidade para o HTML
        nivel_percentual_compat = g_nivel_boia_pc;
        // limite_percentual_compat (para o HTML) é g_nivel_min_pc, atualizado no reset pelo Botão A

        // Lógica de Histerese para BOMBA DE ENCHER
        if (g_bomba_encher_ligada)
        {
            if (g_nivel_boia_pc >= g_nivel_max_pc)
            {
                g_bomba_encher_ligada = false;
                gpio_put(BOMBA_ENCHER_PIN, 1);
            }
        }
        else
        {
            if (g_nivel_boia_pc < g_nivel_min_pc)
            {
                g_bomba_encher_ligada = true;
                gpio_put(BOMBA_ENCHER_PIN, 0);
            }
        }

        if (gpio_get(BOTAO_SW) == 0)
        {
            g_bomba_esvaziar_ligada = true;
        }
        else
        {
            g_bomba_esvaziar_ligada = false;
        }

        gpio_put(BOMBA_ESVAZIAR_PIN, !g_bomba_esvaziar_ligada);

        // Preparar strings para o display OLED
        sprintf(str_nivel_boia_display, "%.1f%%", g_nivel_boia_pc);
        sprintf(str_niveis_config_display, "Min:%d%% Max:%d%%", g_nivel_min_pc, g_nivel_max_pc);
        sprintf(str_bomba_encher_status_display, g_bomba_encher_ligada ? "ON" : "OFF");
        sprintf(str_bomba_esvaziar_status_display, g_bomba_esvaziar_ligada ? "ON" : "OFF");

        ssd1306_fill(&ssd, !cor);
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);
        ssd1306_line(&ssd, 3, 30, 123, 30, cor);
        ssd1306_draw_string(&ssd, str_niveis_config_display, 5, 6);
        ssd1306_draw_string(&ssd, ip_str, 10, 20);
        ssd1306_draw_string(&ssd, "Nivel:", 5, 35);
        ssd1306_draw_string(&ssd, str_nivel_boia_display, 55, 35);
        ssd1306_draw_string(&ssd, "Ench:", 5, 52);
        ssd1306_draw_string(&ssd, str_bomba_encher_status_display, 45, 52);
        ssd1306_draw_string(&ssd, "Esv:", 75, 52);
        ssd1306_draw_string(&ssd, str_bomba_esvaziar_status_display, 105, 52);
        ssd1306_send_data(&ssd);

        sleep_ms(200);
    }
    cyw43_arch_deinit();
    return 0;
}