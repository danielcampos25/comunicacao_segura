
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "psa/crypto.h"
#include "ra01s.h"

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
    static const uint8_t message[] =
        "Mensagem secreta via LoRa!";

    uint8_t nonce[NONCE_SIZE];
    uint8_t ciphertext[MAX_CIPHER_SIZE];
    uint8_t plaintext[MAX_CIPHER_SIZE];

    size_t ciphertext_len = 0;
    size_t plaintext_len = 0;

    psa_status_t status;
    psa_key_id_t key_id = 0;
    uint8_t lora_payload[LORA_PAYLOAD_SIZE];

    printf("\n=== TESTE DE COMUNICACAO SEGURA ===\n");

    // 1. Inicializar o transceptor e verificar a comunicacao SPI.
    LoRaInit();
    // O Wio-SX1262 usa o TCXO controlado pelo DIO3.
    int16_t lora_status = LoRaBegin(lora_frequency_hz(), 17, 3.3f, true);
    if (lora_status != ERR_NONE) {
        printf("ERRO ao inicializar o SX126x (%d)\n", lora_status);
        return;
    }

    LoRaConfig(7, SX126X_LORA_BW_125_0, SX126X_LORA_CR_4_5,
               8, 0, true, false);
    LoRaDebugPrint(true);

    // 2. Inicializar a biblioteca criptografica.
    status = psa_crypto_init();

    if (status != PSA_SUCCESS) {
        printf("ERRO: psa_crypto_init (%ld)\n", (long)status);
        return;
    }

    // 3. Gerar nonce aleatorio para esta mensagem.
    status = psa_generate_random(nonce, sizeof(nonce));

    if (status != PSA_SUCCESS) {
        printf("ERRO ao gerar nonce (%ld)\n", (long)status);
        return;
    }

    // 4. Configurar uma chave AES de 128 bits.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT
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
    printf("Mensagem original: %s\n", (const char *)message);

    // 5. Criptografar e autenticar a mensagem.
    status = psa_aead_encrypt(
        key_id,
        PSA_ALG_GCM,
        nonce,
        sizeof(nonce),
        NULL,
        0,
        message,
        sizeof(message) - 1,
        ciphertext,
        sizeof(ciphertext),
        &ciphertext_len
    );

    if (status != PSA_SUCCESS) {
        printf("ERRO na criptografia (%ld)\n", (long)status);
        psa_destroy_key(key_id);
        return;
    }

    printf("\n--- TRANSMISSAO LORA ---\n");
    print_hex("Nonce: ", nonce, sizeof(nonce));
    print_hex("Dados cifrados + tag: ", ciphertext, ciphertext_len);

    // O receptor precisa do nonce e do ciphertext+tag no mesmo pacote.
    if (sizeof(nonce) + ciphertext_len > sizeof(lora_payload)) {
        printf("ERRO: payload LoRa excede o buffer configurado\n");
        psa_destroy_key(key_id);
        return;
    }
    memcpy(lora_payload, nonce, sizeof(nonce));
    memcpy(lora_payload + sizeof(nonce), ciphertext, ciphertext_len);

    if (!LoRaSend(lora_payload, (int16_t)(sizeof(nonce) + ciphertext_len),
                  SX126x_TXMODE_SYNC)) {
        printf("ERRO: transmissao LoRa falhou\n");
        psa_destroy_key(key_id);
        return;
    }
    printf("Pacote LoRa transmitido (%u bytes).\n",
           (unsigned)(sizeof(nonce) + ciphertext_len));

    // 6. Descriptografar e verificar a autenticidade localmente.
    status = psa_aead_decrypt(
        key_id,
        PSA_ALG_GCM,
        nonce,
        sizeof(nonce),
        NULL,
        0,
        ciphertext,
        ciphertext_len,
        plaintext,
        sizeof(plaintext),
        &plaintext_len
    );

    if (status != PSA_SUCCESS) {
        printf("ERRO na autenticacao/descriptografia (%ld)\n",
               (long)status);
        psa_destroy_key(key_id);
        return;
    }

    printf("\n--- RECEPCAO SIMULADA ---\n");
    printf("Mensagem recuperada: %.*s\n",
           (int)plaintext_len, (const char *)plaintext);

    // 6. Confirmar que o texto recuperado e o original sao iguais
    if (plaintext_len == sizeof(message) - 1 &&
        memcmp(plaintext, message, plaintext_len) == 0) {
        printf("\nSUCESSO: mensagem recuperada corretamente!\n");
    } else {
        printf("\nFALHA: mensagens diferentes!\n");
    }

    printf("\n--- TESTE DE ADULTERACAO ---\n");

    ciphertext[0] ^= 0x01;

    status = psa_aead_decrypt(
        key_id, PSA_ALG_GCM,
        nonce, sizeof(nonce),
        NULL, 0,
        ciphertext, ciphertext_len,
        plaintext, sizeof(plaintext),
        &plaintext_len
    );

    if (status != PSA_SUCCESS) {
        printf("SUCESSO: adulteracao detectada!\n");
        printf("Codigo de erro PSA: %ld\n", (long)status);
    } else {
        printf("FALHA: mensagem adulterada foi aceita!\n");
    }

    psa_destroy_key(key_id);
}
