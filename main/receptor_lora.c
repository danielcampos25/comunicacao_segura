
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "psa/crypto.h"
#include "ra01s.h"

#define KEY_SIZE        16
#define NONCE_SIZE      12
#define TAG_SIZE        16
#define MAX_CIPHER_SIZE 128
#define LORA_PAYLOAD_SIZE (NONCE_SIZE + MAX_CIPHER_SIZE)

#define HISTORY_SIZE       10
#define MAX_MESSAGE_SIZE   128

static char message_history[HISTORY_SIZE][MAX_MESSAGE_SIZE];

static size_t history_next = 0;
static size_t history_count = 0;

static const uint8_t key[KEY_SIZE] = {
    0x10, 0x21, 0x32, 0x43,
    0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB,
    0xDC, 0xED, 0xFE, 0x0F
};

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

static void save_message(const uint8_t *message, size_t length)
{
    if (length >= MAX_MESSAGE_SIZE) {
        length = MAX_MESSAGE_SIZE - 1;
    }

    memcpy(message_history[history_next], message, length);
    message_history[history_next][length] = '\0';

    history_next = (history_next + 1) % HISTORY_SIZE;

    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

static void print_history(void)
{
    printf("\n");
    printf("========================================\n");
    printf("       HISTORICO DE MENSAGENS LORA      \n");
    printf("========================================\n");

    if (history_count == 0) {
        printf("Nenhuma mensagem recebida.\n");
    }

    size_t oldest = (history_next + HISTORY_SIZE - history_count)
                    % HISTORY_SIZE;

    for (size_t i = 0; i < history_count; i++) {
        size_t index = (oldest + i) % HISTORY_SIZE;

        printf("[%zu] %s\n", i + 1, message_history[index]);
    }

    printf("========================================\n");
    printf("Total armazenado: %zu/%d\n",
           history_count, HISTORY_SIZE);
    printf("========================================\n\n");
}

void app_main(void)
{
    uint8_t lora_payload[LORA_PAYLOAD_SIZE];

    // Um byte adicional para o terminador '\0'.
    uint8_t plaintext[MAX_CIPHER_SIZE + 1];

    size_t plaintext_len = 0;
    psa_status_t status;
    psa_key_id_t key_id = 0;

    printf("\n=== RECEPTOR LORA COM AES-GCM ===\n");

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

    // Os parametros devem ser identicos aos do transmissor.
    LoRaConfig(
        7,
        SX126X_LORA_BW_125_0,
        SX126X_LORA_CR_4_5,
        8,
        0,
        true,
        false
    );

    LoRaDebugPrint(false);

    // 2. Inicializar a biblioteca criptografica.
    status = psa_crypto_init();

    if (status != PSA_SUCCESS) {
        printf("ERRO: psa_crypto_init (%ld)\n", (long)status);
        return;
    }

    // 3. Importar a mesma chave AES utilizada pelo transmissor.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_DECRYPT
    );
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

    printf("Criptografia inicializada.\n");
    printf("Frequencia: %lu Hz\n",
           (unsigned long)lora_frequency_hz());

    // 4. Colocar o radio em recepcao continua.
    ClearIrqStatus(SX126X_IRQ_ALL);
    SetRx(SX126X_RX_TIMEOUT_INF);

    printf("Aguardando pacotes LoRa...\n");

    TickType_t last_print_time = xTaskGetTickCount();

    while (1) {

                static TickType_t last_diag = 0;
        TickType_t current_tick = xTaskGetTickCount();

        if ((current_tick - last_diag) >= pdMS_TO_TICKS(2000)) {

            uint8_t radio_status = GetStatus();
            uint16_t irq_status = GetIrqStatus();

            printf(
                "[DIAGNOSTICO] Status: 0x%02X | IRQ: 0x%04X\n",
                radio_status,
                irq_status
            );

            last_diag = current_tick;
        }

        // Verificar se chegou o momento de imprimir o historico.
        TickType_t now = xTaskGetTickCount();

        if ((now - last_print_time) >= pdMS_TO_TICKS(30000)) {
            print_history();
            last_print_time = now;
        }

        // Consultar o radio.
        uint8_t received_len = LoRaReceive(
            lora_payload,
            sizeof(lora_payload)
        );

        if (received_len > 0) {

            printf("\n--- PACOTE RECEBIDO ---\n");
            printf("Tamanho: %u bytes\n",
                   (unsigned int)received_len);

            // O pacote precisa conter nonce + ciphertext + tag.
            if (received_len <= NONCE_SIZE + TAG_SIZE) {
                printf("ERRO: pacote muito pequeno.\n");
            } else {

                uint8_t *nonce = lora_payload;
                uint8_t *ciphertext = lora_payload + NONCE_SIZE;

                size_t ciphertext_len = received_len - NONCE_SIZE;

                print_hex("Nonce: ", nonce, NONCE_SIZE);
                print_hex("Dados cifrados + tag: ",
                          ciphertext,
                          ciphertext_len);

                // 6. Autenticar e descriptografar com AES-GCM.
                status = psa_aead_decrypt(
                    key_id,
                    PSA_ALG_GCM,
                    nonce,
                    NONCE_SIZE,
                    NULL,
                    0,
                    ciphertext,
                    ciphertext_len,
                    plaintext,
                    MAX_CIPHER_SIZE,
                    &plaintext_len
                );

                if (status == PSA_SUCCESS) {

                    plaintext[plaintext_len] = '\0';

                    printf("Mensagem recebida: %s\n", plaintext);

                    // Guardar somente mensagens autenticadas.
                    save_message(plaintext, plaintext_len);

                } else {

                    printf("\nFALHA: mensagem nao autenticada!\n");
                    printf("Codigo de erro PSA: %ld\n",
                           (long)status);
                }
            }

            printf("\nAguardando proximo pacote...\n");

            // Voltar a escutar depois de processar o pacote.
            ClearIrqStatus(SX126X_IRQ_ALL);
            SetRx(SX126X_RX_TIMEOUT_INF);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
