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
#include "lib/buzzer.h"
#include "font.h"
#include "pico/bootrom.h"
#include "lib/functions.h"
#include "lib/matrizLed.h"

#define BOTAO_A 5
#define BOTAO_B 6
#define BUZZER_PIN 21
#define BOTAO_SW 22
#define BOIA_ADC_PIN 28
#define BOMBA_ESVAZIAR_PIN 16
#define BOMBA_ENCHER_PIN 17
#define LED_BLUE_PIN 12
#define LED_RED_PIN 13

#define WS2812_PIN 7
#define NUM_PIXELS 25
#define BUZZER_PIN 21

// --- Variáveis Globais do Sistema de Nível ---
int g_nivel_min_pc = 20;              // Nível mínimo padrão para ligar a bomba de encher
int g_nivel_max_pc = 50;              // Nível máximo padrão para desligar a bomba de encher
float g_nivel_boia_pc = 0.0f;         // Nível atual da boia em percentual
bool g_bomba_encher_ligada = false;   // Estado da bomba de encher
bool g_bomba_esvaziar_ligada = false; // Estado da bomba de esvaziar

float nivel_percentual_compat = 0.0f;
uint16_t limite_percentual_compat = 0;

volatile uint32_t last_time = 0;

#define XSTR(x) STR(x)
#define STR(x) #x

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

const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Reservatorio</title>"
    "<style>"
    ".botao { font-size: 20px; padding: 10px 30px; margin: 10px; border: none; border-radius: 8px; }"
    ".on { background: #4CAF50; color: white; }"
    ".off { background: #f44336; color: white; }"
    "body { font-family: sans-serif; text-align: center; padding: 10px; margin: 0; background: #f9f9f9; }"
    ".barra { width: 250px; height: 340px; background: #ddd; border-radius: 6px; overflow: hidden; margin: 25px auto 25px auto; display: flex; flex-direction: column-reverse; }"
    ".preenchimento { width: 100%; transition: height 0.3s ease; background: #2196F3; }"
    ".label { font-weight: bold; margin-bottom: 5px; display: block; }"
    "@media (max-width: 600px) { .barra { height: 150px; } }"
    "label { margin-right: 10px; }"
    "input { margin-right: 10px; }"
    "</style>"
    "<script>"
    "function sendCommand(cmd) { fetch('/consumo/' + cmd); }"
    "function atualizar() {"
    "   fetch('/estado').then(res => res.json()).then(data => {"
    "    document.getElementById('state').innerText = data.led ? 'Ligado' : 'Desligado';"
    "     let x = Number(data.x);"
    "     let min = Number(data.min);"
    "     let max = Number(data.max);"
    "     let limite = Number(data.limite);"
    "     let percentual = x;"
    "     document.getElementById('x_valor').innerText = x;"
    "     document.getElementById('barra_x').style.height = percentual + '%';"
    "     let statusText = (limite) ? 'Ligada' : 'Desligado';"
    "     document.getElementById('status').innerText = statusText;"
    " document.getElementById('limiteMin').max = max - 15;"
    "   }).catch(err => {"
    "     console.error('Erro:', err);"
    "     document.getElementById('x_valor').innerText = '--';"
    "     document.getElementById('barra_x').style.height = '0%';"
    "     document.getElementById('status').innerText = '--';"
    "   });"
    "}"
    "function enviarConfigMax() {"
    "   const limiteMax = document.getElementById('limiteMax').value;"
    "   fetch(`/configmax?limite=${limiteMax}`).then(res => {"
    "     if (res.ok) { "
    "alert('Limite MÁXIMO atualizado!');"
    "  atualizar();}"
    "     else alert('Erro ao atualizar limite MÁXIMO.');"
    "   });"
    "}"
    "function enviarConfigMin() {"
    "   const limiteMin = document.getElementById('limiteMin').value;"
    "   fetch(`/configmin?limite=${limiteMin}`).then(res => {"
    "     if (res.ok) { "
    "alert('Limite MÍNIMO atualizado!');"
    "  atualizar();}"
    "     else alert('Erro o limite MÍNIMO precisar ser menor que o limite MÁXIMO em 8 unidades.');"
    "   });"
    "}"
    "setInterval(atualizar, 1000);"
    "</script></head><body>"

    "<h1>Monitor do Reservatório</h1>"
    "<p class='label'>Nível da água: <span id='x_valor'>--</span>%</p>"
    "<div class='barra'><div id='barra_x' class='preenchimento'></div></div>"
    "<p class='label'>Status da Bomba: <span id='status'>--</span></p>"

    "<hr><h2>Configurar Limite Percentual</h2>"

    "<form onsubmit='enviarConfigMax(); return false;'>"
    "<label for='limiteMax'>Limite MÁXIMO(%):</label>"
    "<input type='number' id='limiteMax' name='limiteMax' value='50' min='0' max='100'>"
    "<button type='submit'>Atualizar Limite MÁXIMO</button></form>"
    "<br><br>"

    "<form onsubmit='enviarConfigMin(); return false;'>"
    "<label for='limiteMin'>Limite MÍNIMO(%):</label>"
    "<input type='number' id='limiteMin' name='limiteMin' value='20' min='0' max='100'>"
    "<button type='submit'>Atualizar Limite MÍNIMO</button></form>"

    "<hr style='margin-top: 20px;'>"

    "<p class='label'>Status do Consumo (Bomba 2): <span id='state'>--</span></p>"
    "<button class='botao on' onclick=\"sendCommand('on')\">Ligar</button>"
    "<button class='botao off' onclick=\"sendCommand('off')\">Desligar</button>"

    "<hr style='margin-top: 20px;'>"
    "<p style='font-size: 15px; color: #336699; font-style: italic; max-width: 90%; margin: 10px auto;'>"
    "No momento que o limite mínimo é atingido, a bomba enche o reservatório até atingir o limite máximo."
    "</p></body></html>";

struct http_state
{
    char response[4095];
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
        printf("PBUF nulo, fechando conexão.\n"); // Debug
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;

    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        printf("Erro: Falha ao alocar http_state.\n"); // Debug
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }

    hs->sent = 0;

    if (strncmp(req, "GET /consumo/on", strlen("GET /consumo/on")) == 0)
    {
        // LIGAR a bomba de esvaziar
        gpio_put(BOMBA_ESVAZIAR_PIN, 0); // Assumindo que 0 LIGA (low-active)
        g_bomba_esvaziar_ligada = true;  // Atualiza o estado global
        const char *txt = "Ligado";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    else if (strncmp(req, "GET /consumo/off", strlen("GET /consumo/off")) == 0)
    {
        // DESLIGAR a bomba de esvaziar
        gpio_put(BOMBA_ESVAZIAR_PIN, 1); // Assumindo que 1 DESLIGA (low-active)
        g_bomba_esvaziar_ligada = false; // Atualiza o estado global
        const char *txt = "Desligado";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    else if (strncmp(req, "GET /estado", strlen("GET /estado")) == 0) // <--- Corrigido aqui
    {

        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"led\":%d,\"x\":%.0f,\"min\":%d,\"max\":%d,\"limite\":%d, \"bomba2_ligada\":%d}\r\n",                  // Adicione "bomba2_ligada"
                                g_bomba_esvaziar_ligada, nivel_percentual_compat, g_nivel_min_pc, g_nivel_max_pc, g_bomba_encher_ligada); // Passe o estado da bomba de esvaziar

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }

    else if (strncmp(req, "GET /configmax", strlen("GET /configmax")) == 0) // <--- Corrigido aqui
    {
        printf("Requisição teste GET /config\n");
        int novo_limite = get_param_val(req, "limite");

        if (novo_limite >= 0 && novo_limite <= 100)
        {
            g_nivel_max_pc = novo_limite;
            printf("Novo g_nivel_max_pc: %d\n", g_nivel_max_pc);
        }
        else
        {
            printf("Limite recebido inválido: %d\n", novo_limite);
        }

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 2\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "OK");
        printf("Resposta /config gerada. Tamanho: %d\n", (int)hs->len);
    }
    else if (strncmp(req, "GET /configmin", strlen("GET /configmin")) == 0) // <--- Corrigido aqui
    {
        printf("Requisição esse q quero GET /configmin\n");
        int novo_limite = get_param_val(req, "limite");

        if (novo_limite >= 0 && novo_limite <= 100)
        {
            g_nivel_min_pc = novo_limite;
            printf("Novo g_nivel_min_pc: %d\n", g_nivel_min_pc);
        }
        else
        {
            printf("Limite recebido inválido: %d\n", novo_limite);
        }

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 2\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "OK");
        printf("Resposta /configmin gerada. Tamanho: %d\n", (int)hs->len);
    }
    // Para a página principal (GET /)
    // Cuidado com o espaço após o '/'. Alguns navegadores enviam "GET / HTTP/1.1", outros "GET /HTTP/1.1"
    else if (strncmp(req, "GET / ", strlen("GET / ")) == 0 || strncmp(req, "GET /HTTP", strlen("GET /HTTP")) == 0) // <--- Corrigido aqui e adicionada uma segunda condição
    {
        printf("Requisição GET / (página principal)\n");
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);

        printf("Preparando para enviar resposta. Tamanho: %d\n", (int)hs->len);
    }
    else // Requisições não tratadas (ex: /favicon.ico)
    {
        printf("Requisição desconhecida/não tratada: %.*s\n", (int)p->len, req);
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 404 Not Found\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n"
                           "\r\n");
        printf("Resposta 404 Not Found gerada.\n");
    }

    pbuf_free(p);
    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    err_t write_err = tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK)
    {
        printf("Erro ao escrever dados TCP: %d (erro lwIP)\n", write_err);
    }
    else
    {
    }
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
    printf("Tentando criar PCB TCP...\n"); // Debug
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n"); // Mensagem original
        return;
    }
    printf("PCB TCP criado. Tentando ligar na porta 80...\n"); // Debug
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n"); // Mensagem original
        tcp_close(pcb);                                   // Garante que o PCB seja liberado se a ligação falhar
        return;
    }
    printf("Servidor TCP ligado. Tentando escutar...\n"); // Debug
    pcb = tcp_listen(pcb);
    if (!pcb)
    {                                                   // Verifica se tcp_listen retornou NULL (erro)
        printf("Erro ao iniciar escuta do servidor\n"); // Debug
        return;
    }
    printf("Servidor TCP escutando. Configurando callback de aceitação...\n"); // Debug
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n"); // Mensagem original
}

// --- Configuração dos Níveis Discretos da Boia ---
// Valores ADC que definem cada nível. Do nível 0 ao nível 12.
static const uint16_t ADC_LEVEL_VALUES[] = {
    200,  // Nível 0 (0%)
    1200, // Nível 1 8%
    1300, // Nível 2 16%
    1400, // Nível 3  25%
    1500, // Nível 4    33%
    1600, // Nível 5    41%
    1700, // Nível 6    50%
    1800, // Nível 7
    2000, // Nível 8
    2100, // Nível 9
    2200, // Nível 10
    2400, // Nível 11
    2800  // Nível 12 (100%)
};
static const int NUM_ADC_LEVELS = sizeof(ADC_LEVEL_VALUES) / sizeof(ADC_LEVEL_VALUES[0]);
static const float MAX_PERCENTAGE_MAPPED = 100.0f; // O último nível (2800) corresponde a 100%

// Função para converter leitura ADC da boia para percentual
int adc_para_percentual_boia(uint32_t adc_valor)
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

volatile bool button_pressed = false;

void gpio_irq_handler(uint gpio, uint32_t event)
{
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (current_time - last_time > 200000) // Debounce de 200ms
    {
        if (gpio == BOTAO_A)
        {
            g_nivel_min_pc = 20;                       // Reseta para padrão
            g_nivel_max_pc = 50;                       // Reseta para padrão
            limite_percentual_compat = g_nivel_min_pc; // Atualiza para HTML
            printf("Niveis resetados para Min: %d%% Max: %d%%\n", g_nivel_min_pc, g_nivel_max_pc);
        }
        else if (gpio == BOTAO_B)
        {
            printf("Botao B pressionado: Entrando no modo bootloader USB...\n");
            reset_usb_boot(0, 0);
        }
        else if (gpio == BOTAO_SW)
        {
            button_pressed = true;
        }
        last_time = current_time;
    }
}

#define CREDENTIAL_BUFFER_SIZE 64 // Tamanho do buffer para armazenar as credenciais

char WIFI_SSID[CREDENTIAL_BUFFER_SIZE]; // Substitua pelo nome da sua rede Wi-Fi
char WIFI_PASS[CREDENTIAL_BUFFER_SIZE]; // Substitua pela senha da sua rede Wi-Fi

static bool switch_led_status = false;
void atualizar_leds()
{

    if (nivel_percentual_compat >= g_nivel_max_pc)
    {
        switch_led_status = false;
        set_one_led(0, 10, 0, leds_Normal);
        gpio_put(LED_BLUE_PIN, 1); // liga o LED azul
        gpio_put(LED_RED_PIN, 0);  // Desliga o LED vermelho
    }
    else if (nivel_percentual_compat <= g_nivel_min_pc || switch_led_status)
    {
        switch_led_status = true;
        set_one_led(10, 0, 0, leds_Alerta);
        gpio_put(LED_BLUE_PIN, 0); // Desliga o LED azul
        gpio_put(LED_RED_PIN, 1);  // Liga o LED vermelho
    }
}

int main()
{
    // Inicializa as variáveis de compatibilidade com o HTML
    nivel_percentual_compat = 0;               // Valor inicial
    limite_percentual_compat = g_nivel_min_pc; // Usa o g_nivel_min_pc padrão

    stdio_init_all();

    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    matriz_init(WS2812_PIN);

    adc_init();
    adc_gpio_init(BOIA_ADC_PIN);

    buzzer_init(BUZZER_PIN);
     uint slice_num_buzzer = pwm_gpio_to_slice_num(BUZZER_PIN); 

    ssd1306_t ssd;
    init_Display(&ssd);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Entrar no", 0, 0);
    ssd1306_draw_string(&ssd, "Monitor Serial", 0, 14);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

    waitUSB();
    printf("Tamanho do HTML_BODY: %d bytes\n", (int)strlen(HTML_BODY));
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
    gpio_set_irq_enabled(BOTAO_SW, GPIO_IRQ_EDGE_FALL, true);

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

    bool state = true;
    gpio_init(BOMBA_ESVAZIAR_PIN);
    gpio_set_dir(BOMBA_ESVAZIAR_PIN, state);
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
        // Faz a media de 100 valores para reduzir o ruído
        uint32_t adc_valor_boia = 0;
        for (int i = 0; i < 100; i++)
        {
            adc_valor_boia += adc_read();
        }
        adc_valor_boia = adc_valor_boia / 100.0;
        printf("ADC Valor Boia: %d\n", adc_valor_boia);
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

        if (button_pressed)
        {
            button_pressed = false;
            g_bomba_esvaziar_ligada = !g_bomba_esvaziar_ligada;
            state = !state;
            gpio_put(BOMBA_ESVAZIAR_PIN, state);
        }

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

        if (nivel_percentual_compat >= g_nivel_min_pc && nivel_percentual_compat <= g_nivel_max_pc)
        {
            buzzer_play(BUZZER_PIN, 1500); // Toca o buzzer a 1500 Hz
        }
        else // Se o nível estiver fora da faixa (muito baixo ou muito alto, ou outra condição que não queira o som)
        {
            pwm_set_enabled(slice_num_buzzer, false); // Desliga o PWM para o buzzer
        }

        atualizar_leds(); // Atualiza a matriz de LEDs

        sleep_ms(10);
    }
    cyw43_arch_deinit();
    return 0;
}