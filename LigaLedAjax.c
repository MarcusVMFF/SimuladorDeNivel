#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // Necessário para malloc e free
// #include "lib/matrizLed.h" // Removido
// #include "lib/buzzer.h"    // Removido
#include "lib/ssd1306.h"
#include "font.h"
#include "pico/bootrom.h"

#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_SW 22 // Botão SW do Joystick
// #define LED_BLUE_PIN 12   // Removido
#define BOIA_ADC_PIN 28 // GPIO28 é o ADC2
#define BOMBA1_PIN 16
#define BOMBA2_PIN 17

// #define JOYSTICK_X 26     // Removido
// #define JOYSTICK_Y 27     // Removido
// #define WS2812_PIN 7      // Removido
// #define NUM_PIXELS 25     // Removido
// #define BUZZER_PIN 21     // Removido

#define WIFI_SSID ""
#define WIFI_PASS ""

// #define STEP 0.1 // Removido
// #define LIMIT_MAX_RESET 25.0 // Removido

// uint16_t limite_percentual = LIMIT_MAX_RESET; // Será atualizado pela faixa
// float nivel_percentual = 20; // Será atualizado pela boia

// Estrutura para pontos de calibração da boia (ADC, Percentual)
typedef struct
{
    uint16_t adc_val;
    int percent_val;
} PontoCalibracaoBoia;

// Estrutura para faixas de operação
typedef struct
{
    int percent_min;
    int percent_max;
    const char *nome_faixa; // Para exibição
} FaixaOperacao;

// --- Variáveis Globais do Sistema de Nível ---
static const PontoCalibracaoBoia pontos_calibracao_boia[] = {
    {200, 0}, {1200, 10}, {1300, 20}, {1400, 30}, {1500, 40}, {1600, 50}, {1700, 60}, {1800, 70}, {2000, 75}, {2100, 76}, {2200, 77}, {2400, 78}, {2800, 80}

};
static const int num_pontos_calibracao = sizeof(pontos_calibracao_boia) / sizeof(pontos_calibracao_boia[0]);

static const FaixaOperacao faixas_disponiveis[] = {
    {0, 10, "0-10%"}, {10, 20, "10-20%"}, {20, 30, "20-30%"}, {30, 40, "30-40%"}, {40, 50, "40-50%"}, {50, 60, "50-60%"}, {60, 70, "60-70%"}, {70, 80, "70-80%"}};
static const int num_faixas_disponiveis = sizeof(faixas_disponiveis) / sizeof(faixas_disponiveis[0]);

int g_indice_faixa_selecionada = 3; // Padrão: Faixa 30-40% (índice 3)
float g_nivel_boia_pc = 0.0f;       // Nível atual da boia em percentual
bool g_bombas_ligadas = false;      // Estado atual das bombas (true = ligadas, false = desligadas)

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
        // Para compatibilidade com o HTML, usamos nivel_percentual_compat e limite_percentual_compat
        // que são atualizados no loop principal com base na boia e na faixa selecionada.

        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"x\":%.2f,\"min\":%d,\"max\":%d,\"limite\":%d}\r\n",
                                nivel_percentual_compat, 0, 100, limite_percentual_compat);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // O endpoint GET /config foi removido, pois a seleção de faixa substitui essa funcionalidade.
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

// Função para converter leitura ADC da boia para percentual
int adc_para_percentual_boia(uint16_t adc_valor)
{
    if (adc_valor <= pontos_calibracao_boia[0].adc_val)
    {
        return pontos_calibracao_boia[0].percent_val;
    }
    for (int i = num_pontos_calibracao - 1; i >= 0; i--)
    {
        if (adc_valor >= pontos_calibracao_boia[i].adc_val)
        {
            return pontos_calibracao_boia[i].percent_val;
        }
    }
    return pontos_calibracao_boia[0].percent_val;
}

// Função para controlar as bombas (define o estado dos GPIOs)
void controlar_bombas(bool ligar)
{
    gpio_put(BOMBA1_PIN, ligar ? 0 : 1); // Lógica invertida: 0 = LIGADO, 1 = DESLIGADO
    gpio_put(BOMBA2_PIN, ligar ? 0 : 1);
    g_bombas_ligadas = ligar;
}

// Inicializa os pinos das bombas
void init_bombas_gpio()
{
    gpio_init(BOMBA1_PIN);
    gpio_set_dir(BOMBA1_PIN, GPIO_OUT);
    gpio_put(BOMBA1_PIN, 1); // Começa desligada

    gpio_init(BOMBA2_PIN);
    gpio_set_dir(BOMBA2_PIN, GPIO_OUT);
    gpio_put(BOMBA2_PIN, 1); // Começa desligada
    g_bombas_ligadas = false;
}

void gpio_irq_handler(uint gpio, uint32_t event)
{
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000) // Debounce de 200ms
    {
        if (gpio == BOTAO_A)
        {
            g_indice_faixa_selecionada = (g_indice_faixa_selecionada + 1) % num_faixas_disponiveis;
            printf("Faixa de operacao alterada para: %s\n", faixas_disponiveis[g_indice_faixa_selecionada].nome_faixa);
            limite_percentual_compat = faixas_disponiveis[g_indice_faixa_selecionada].percent_min;
        }
        else if (gpio == BOTAO_B)
        {
            printf("Botao B pressionado: Entrando no modo bootloader USB...\n");
            reset_usb_boot(0, 0);
        }
        else if (gpio == BOTAO_SW)
        {
            g_indice_faixa_selecionada--;
            if (g_indice_faixa_selecionada < 0) {
                g_indice_faixa_selecionada = num_faixas_disponiveis - 1;
            }
            printf("Faixa de operacao (SW) alterada para: %s\n", faixas_disponiveis[g_indice_faixa_selecionada].nome_faixa);
            limite_percentual_compat = faixas_disponiveis[g_indice_faixa_selecionada].percent_min;
        }
        last_time = current_time;
    }
}

int main()
{
    // Inicializa as variáveis de compatibilidade com o HTML
    nivel_percentual_compat = 0; // Valor inicial
    limite_percentual_compat = faixas_disponiveis[g_indice_faixa_selecionada].percent_min;

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_init(BOTAO_SW);
    gpio_set_dir(BOTAO_SW, GPIO_IN);
    gpio_pull_up(BOTAO_SW);
    gpio_set_irq_enabled_with_callback(BOTAO_SW, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    init_bombas_gpio(); // Inicializa GPIOs das bombas
    // gpio_init(LED_BLUE_PIN); // Removido
    // gpio_set_dir(LED_BLUE_PIN, GPIO_OUT); // Removido

    stdio_init_all();
    sleep_ms(1000);

    adc_init();
    adc_gpio_init(BOIA_ADC_PIN); // Configura GPIO28 (ADC2) para a boia
    // adc_gpio_init(JOYSTICK_X); // Removido
    // adc_gpio_init(JOYSTICK_Y); // Removido

    // matriz_init(WS2812_PIN); // Removido
    // buzzer_init(BUZZER_PIN); // Removido

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

    // Strings para o display OLED
    char str_nivel_boia[10];
    char str_faixa_op[15];
    char str_bombas_status[15];
    bool cor = true;

    while (true)
    {
        cyw43_arch_poll();

        adc_select_input(2); // Seleciona ADC2 (GPIO28) para leitura da boia
        uint16_t adc_valor_boia = adc_read();
        g_nivel_boia_pc = (float)adc_para_percentual_boia(adc_valor_boia);

        // Atualiza variáveis de compatibilidade para o HTML
        nivel_percentual_compat = g_nivel_boia_pc;
        // limite_percentual_compat é atualizado na IRQ do botão A

        // Lógica de Histerese para controle das bombas
        FaixaOperacao faixa_atual = faixas_disponiveis[g_indice_faixa_selecionada];
        int nivel_min_pc_faixa = faixa_atual.percent_min;
        int nivel_max_pc_faixa = faixa_atual.percent_max;

        if (g_bombas_ligadas)
        {
            if (g_nivel_boia_pc >= nivel_max_pc_faixa)
            {
                controlar_bombas(false); // Desliga
            }
        }
        else
        {
            if (g_nivel_boia_pc < nivel_min_pc_faixa)
            {
                controlar_bombas(true); // Liga
            }
        }

        // Preparar strings para o display OLED
        sprintf(str_nivel_boia, "%.1f%%", g_nivel_boia_pc);
        sprintf(str_faixa_op, "%s", faixa_atual.nome_faixa);
        sprintf(str_bombas_status, g_bombas_ligadas ? "LIGADAS" : "DESLIGADAS");

        ssd1306_fill(&ssd, !cor);
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);
        ssd1306_line(&ssd, 3, 30, 123, 30, cor);
        ssd1306_draw_string(&ssd, "Faixa:", 10, 6);
        ssd1306_draw_string(&ssd, str_faixa_op, 55, 6);
        ssd1306_draw_string(&ssd, ip_str, 10, 20);
        ssd1306_draw_string(&ssd, "Nivel:", 10, 35);
        ssd1306_draw_string(&ssd, str_nivel_boia, 60, 35);
        ssd1306_draw_string(&ssd, "Bombas:", 10, 52);
        ssd1306_draw_string(&ssd, str_bombas_status, 60, 52);
        ssd1306_send_data(&ssd);

        sleep_ms(200);
    }
    cyw43_arch_deinit();
    return 0;
}
