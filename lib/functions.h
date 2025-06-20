#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"

#define CREDENTIAL_BUFFER_SIZE 64 // Tamanho do buffer para armazenar as credenciais


void waitUSB();
void wifi_Credentials(char *WIFI_SSID,
                      char *WIFI_PASSWORD);