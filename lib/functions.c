#include "functions.h"


#define CREDENTIAL_BUFFER_SIZE 64 // Tamanho do buffer para armazenar as credenciais


void waitUSB()
{
    printf("Aguardando a comunicação USB...\n");

    // Espera até que a comunicação USB esteja conectada
    while (!stdio_usb_connected())
    {
        // O dispositivo USB ainda não foi conectado
        sleep_ms(100); // Espera 100ms antes de checar novamente
    }

    // Quando a comunicação USB for detectada, a execução continua
    printf("Comunicação USB detectada!\n");
}

void wifi_Credentials(char *WIFI_SSID,
                      char *WIFI_PASSWORD)
{
    printf("\n\t***Selecione LF no Line ending do terminal\n\n");
    printf("Digite o nome da rede Wi-Fi: ");
    fgets(WIFI_SSID, CREDENTIAL_BUFFER_SIZE, stdin);
    printf("Digite a senha da rede Wi-Fi: ");
    fgets(WIFI_PASSWORD, CREDENTIAL_BUFFER_SIZE, stdin);


    // Deleta o fim de linha da string
    WIFI_SSID[strcspn(WIFI_SSID, "\n")] = '\0';
    WIFI_PASSWORD[strcspn(WIFI_PASSWORD, "\n")] = '\0';
    printf("Credenciais salvas com sucesso!\n");
}