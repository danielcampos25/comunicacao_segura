
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "driver/usb_serial_jtag.h"
#include "psa/crypto.h"
#include "ra01s.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define KEY_SIZE        16
#define NONCE_SIZE      12
#define TAG_SIZE        16
#define MAX_CIPHER_SIZE 128
#define LORA_PAYLOAD_SIZE (NONCE_SIZE + MAX_CIPHER_SIZE)

static const uint8_t key[KEY_SIZE] = {
    0x10, 0x21, 0x32, 0x43,
    0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB,
    0xDC, 0xED, 0xFE, 0x0F
};

static void print_hex(const char *label,
                      const uint8_t *data,
                      size_t len)
{
    printf("%s", label);

    for (size_t i = 0; i < len; i++) {
        printf("%02X", (unsigned int)data[i]);
    }

    printf("\n");
}

static int read_serial_message(char *buffer, size_t buffer_size)
{
    size_t index = 0;
    uint8_t received_char;

    while (1) {

        int bytes_read = usb_serial_jtag_read_bytes(
            &received_char,
            1,
            portMAX_DELAY
        );

        if (bytes_read <= 0) {
            continue;
        }

        // Enter encerra a mensagem.
        if (received_char == '\r' || received_char == '\n') {

            if (index == 0) {
                continue;
            }

            buffer[index] = '\0';

            printf("\n");
            return (int)index;
        }

        // Backspace permite apagar caracteres.
        if (received_char == '\b' || received_char == 127) {

            if (index > 0) {
                index--;
                printf("\b \b");
            }

            continue;
        }

        // Armazenar somente caracteres imprimíveis.
        if (received_char >= 32 && received_char <= 126) {

            if (index < buffer_size - 1) {
                buffer[index++] = (char)received_char;
                printf("%c", received_char);
            }
        }
    }
}


static uint32_t lora_frequency_hz(void)
{
#if CONFIG_433MHZ
    return 433000000UL;
#elif CONFIG_866MHZ
    return 866000000UL;
#elif CONFIG_915MHZ
    return 915000000UL;
#elif CONFIG_OTHER
    return (uint32_t)CONFIG_OTHER_FREQUENCY * 1000000UL;
#else
#error "Select a LoRa frequency in menuconfig"
#endif
}

void app_main(void)
{

        // Inicializa o driver USB Serial/JTAG para receber mensagens.
    usb_serial_jtag_driver_config_t usb_config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));

    printf("\nDriver USB Serial/JTAG inicializado.\n");
    char message[MAX_CIPHER_SIZE];
    uint8_t nonce[NONCE_SIZE];
    uint8_t ciphertext[MAX_CIPHER_SIZE];
    uint8_t lora_payload[LORA_PAYLOAD_SIZE];

    size_t ciphertext_len = 0;
    size_t message_len = 0;

    psa_status_t status;
    psa_key_id_t key_id = 0;

    printf("\n=== TRANSMISSOR LORA COM AES-GCM ===\n");

    // 1. Inicializar o transceptor LoRa.
    LoRaInit();

    int16_t lora_status = LoRaBegin(
        lora_frequency_hz(),
        17,
        3.3f,
        true
    );

    if (lora_status != ERR_NONE) {
        printf("ERRO ao inicializar o SX126x (%d)\n",
               lora_status);
        return;
    }

    LoRaConfig(
        7,
        SX126X_LORA_BW_125_0,
        SX126X_LORA_CR_4_5,
        8,
        0,
        true,
        false
    );

    LoRaDebugPrint(true);

    // 2. Inicializar a biblioteca criptográfica.
    status = psa_crypto_init();

    if (status != PSA_SUCCESS) {
        printf("ERRO: psa_crypto_init (%ld)\n", (long)status);
        return;
    }

    // 3. Importar a chave AES-128.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);

    status = psa_import_key(
        &attributes,
        key,
        sizeof(key),
        &key_id
    );

    psa_reset_key_attributes(&attributes);

    if (status != PSA_SUCCESS) {
        printf("ERRO ao importar chave (%ld)\n", (long)status);
        return;
    }

    printf("\nCriptografia inicializada.\n");
    printf("Digite uma mensagem no Monitor Serial.\n");

    
    // 4. Transmitir mensagens automaticamente a cada 10 segundos.
    while (1) {

        const char *automatic_message = "transmissor conectado";

        message_len = strlen(automatic_message);
        memcpy(message, automatic_message, message_len + 1);

        printf("\nMensagem original: %s\n", message);

        // 5. Gerar um nonce novo para cada mensagem.
        status = psa_generate_random(nonce, sizeof(nonce));

        if (status != PSA_SUCCESS) {
            printf("ERRO ao gerar nonce (%ld)\n", (long)status);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        // 6. Criptografar e autenticar a mensagem.
        ciphertext_len = 0;

        status = psa_aead_encrypt(
            key_id,
            PSA_ALG_GCM,
            nonce,
            sizeof(nonce),
            NULL,
            0,
            (const uint8_t *)message,
            message_len,
            ciphertext,
            sizeof(ciphertext),
            &ciphertext_len
        );

        if (status != PSA_SUCCESS) {
            printf("ERRO na criptografia (%ld)\n", (long)status);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        // 7. Montar o pacote: nonce + ciphertext + tag.
        if (sizeof(nonce) + ciphertext_len > sizeof(lora_payload)) {
            printf("ERRO: payload LoRa excede o buffer.\n");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        memcpy(lora_payload, nonce, sizeof(nonce));
        memcpy(lora_payload + sizeof(nonce), ciphertext, ciphertext_len);

        printf("\n--- TRANSMISSAO LORA ---\n");

        print_hex("Nonce: ", nonce, sizeof(nonce));
        print_hex("Dados cifrados + tag: ", ciphertext, ciphertext_len);

        // 8. Transmitir o pacote LoRa.
        if (!LoRaSend(
                lora_payload,
                (int16_t)(sizeof(nonce) + ciphertext_len),
                SX126x_TXMODE_SYNC
            )) {

            printf("ERRO: transmissao LoRa falhou.\n");

        } else {

            printf(
                "SUCESSO: pacote LoRa transmitido (%u bytes).\n",
                (unsigned)(sizeof(nonce) + ciphertext_len)
            );
        }

        printf("Proxima transmissao em 10 segundos...\n");

        vTaskDelay(pdMS_TO_TICKS(10000));
    }

}
