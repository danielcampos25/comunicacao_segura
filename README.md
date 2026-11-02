# comunicação_segura

Aplicação embarcada de comunicação ponto a ponto usando **LoRa** sobre um
transceptor da família **SX126x** (por exemplo, um módulo RA-01S/SX1268) e
criptografia autenticada **AES-128-GCM**. O projeto possui dois modos de
execução:

- **Transmissor**: gera uma mensagem, cifra o conteúdo, acrescenta um nonce
  aleatório e transmite o pacote pelo rádio LoRa.
- **Receptor**: permanece escutando, valida o pacote LoRa, autentica e
  descriptografa a mensagem e mantém um histórico das últimas mensagens
  recebidas.

O projeto foi desenvolvido com **ESP-IDF**, FreeRTOS e a API PSA Crypto
fornecida pelo ambiente criptográfico do ESP-IDF. O alvo atualmente gravado
no projeto é um **ESP32-S3**. A configuração presente no repositório seleciona
o transmissor; o receptor é selecionado por uma opção do menu de configuração.

> **Importante:** LoRa fornece a camada física de rádio, mas não fornece, por
> si só, criptografia de aplicação, autenticação de identidade, confirmação de
> entrega ou prevenção contra repetição. Este projeto adiciona AES-GCM no
> nível da aplicação, mas ainda deve ser considerado uma implementação
> experimental até que gerenciamento de chaves, controle de mensagens
> repetidas, confirmação de entrega e testes de campo sejam adicionados.

## 1. Objetivo da aplicação

O objetivo é demonstrar a transmissão de mensagens curtas entre duas placas:

```text
Aplicação transmissora
        |
        | AES-128-GCM
        v
nonce + ciphertext + tag
        |
        | LoRa / SX126x
        v
Aplicação receptora
        |
        | validação GCM
        v
mensagem original
```

O fluxo não usa um servidor, gateway, Wi-Fi, Bluetooth ou protocolo LoRaWAN.
É uma comunicação LoRa direta, ponto a ponto, entre dois rádios configurados
com os mesmos parâmetros físicos.

## 2. Estrutura do repositório

```text
.
├── CMakeLists.txt
├── sdkconfig
├── sdkconfig.ci
├── sdkconfig.old
├── README.md
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── transmissor_lora.c
│   └── receptor_lora.c
└── components/
    └── ra01s/
        ├── CMakeLists.txt
        ├── Kconfig.projbuild
        ├── ra01s.c
        └── ra01s.h
```

### `main/transmissor_lora.c`

Implementa o firmware transmissor. Ele:

1. inicializa a USB Serial/JTAG;
2. inicializa o SX126x;
3. configura o rádio;
4. inicializa PSA Crypto;
5. importa a chave AES;
6. gera uma mensagem fixa;
7. gera um nonce aleatório;
8. cifra a mensagem com AES-GCM;
9. monta o pacote LoRa;
10. transmite de forma síncrona;
11. aguarda dez segundos e repete.

### `main/receptor_lora.c`

Implementa o firmware receptor. Ele:

1. inicializa o SX126x;
2. configura o rádio na mesma frequência e nos mesmos parâmetros do
   transmissor;
3. inicializa PSA Crypto;
4. importa a mesma chave AES;
5. entra em recepção contínua;
6. consulta periodicamente os IRQs do SX126x;
7. lê os pacotes que sinalizam `RX_DONE`;
8. separa nonce e ciphertext com tag;
9. valida e descriptografa usando AES-GCM;
10. imprime a mensagem autenticada;
11. salva as últimas dez mensagens em memória RAM.

### `components/ra01s/ra01s.c` e `ra01s.h`

São o componente local que adapta o driver do transceptor SX126x à API usada
pela aplicação. Entre outras funções, o componente implementa:

- inicialização do barramento SPI;
- reset do rádio;
- leitura e escrita de comandos do SX126x;
- configuração de frequência;
- configuração de modulação LoRa;
- envio e recepção de buffers;
- leitura e limpeza dos registradores de IRQ;
- controle opcional de pinos externos `TXEN` e `RXEN`.

## 3. Hardware utilizado

### 3.1 Microcontrolador

O `sdkconfig` deste workspace identifica o alvo como:

```text
ESP32-S3
```

O ESP32-S3 executa o firmware, oferece o controlador SPI usado pelo rádio e
disponibiliza a interface USB Serial/JTAG utilizada pelo transmissor para
diagnóstico. A placa específica do ESP32-S3 não é identificada pelo código;
portanto, o nome comercial da placa, alimentação disponível, conector da
antena e pinagem mecânica precisam ser confirmados no hardware real.

### 3.2 Transceptor LoRa

O componente foi escrito para um rádio da família **Semtech SX126x**. O nome
RA-01S aparece no componente e o código usa comandos e registradores
específicos do SX126x, como:

- `SET_PACKET_TYPE`;
- `SET_MODULATION_PARAMS`;
- `SET_PACKET_PARAMS`;
- `SET_RF_FREQUENCY`;
- `SET_TX`;
- `SET_RX`;
- `GET_IRQ_STATUS`;
- `READ_BUFFER`;
- `WRITE_BUFFER`.

O módulo físico exato não está declarado no projeto. Antes de montar o
hardware, confirme se o módulo é SX1261, SX1262 ou SX1268 e se a faixa de
frequência da antena corresponde à configuração escolhida. O driver usa
configuração de potência compatível com até +22 dBm, mas o limite legal,
térmico e elétrico depende do módulo, da alimentação, da antena e da
regulamentação local.

### 3.3 Ligações SPI e controle

Na configuração atual (`sdkconfig`), os sinais são:

| Sinal | GPIO atual | Função |
|---|---:|---|
| MISO | 8 | Dados do SX126x para o ESP32-S3 |
| MOSI | 9 | Dados do ESP32-S3 para o SX126x |
| SCLK | 7 | Clock SPI |
| NSS/CS | 41 | Seleção do dispositivo SPI |
| RESET | 42 | Reset do transceptor |
| BUSY | 40 | Indica que o SX126x está ocupado |
| TXEN | -1 | Não utilizado |
| RXEN | -1 | Não utilizado |

O periférico selecionado é `SPI2_HOST`, o SPI opera em modo 0 e a frequência
configurada no driver é 9 MHz.

Os sinais `TXEN` e `RXEN` ficam desabilitados (`-1`) na configuração atual.
Isso pressupõe que o módulo não precise de um controle externo separado para
o caminho RF. Se o módulo possuir um front-end com pinos de habilitação,
esses pinos deverão ser configurados no menuconfig e ligados corretamente.

### 3.4 Alimentação, nível lógico e antena

O código chama:

```c
LoRaBegin(frequencia, 17, 3.3f, true);
```

O valor `3.3f` representa a tensão lógica/controle esperada para o TCXO na
API do componente; porém, o projeto está com `CONFIG_USE_TCXO` desativado e o
driver não habilita o TCXO quando a tensão passada à função não é usada dessa
forma pela placa. Confirme no esquemático do módulo se existe TCXO, cristal,
regulador próprio e quais níveis de alimentação são permitidos.

Recomendações de hardware:

- alimentar o módulo dentro da tensão especificada pelo fabricante;
- utilizar uma fonte capaz de fornecer os picos de corrente de transmissão;
- conectar uma antena adequada à faixa configurada antes de transmitir;
- não operar o transmissor sem carga RF quando isso puder danificar o
  módulo;
- manter o terra do ESP32-S3 e do rádio comum e curto;
- confirmar com multímetro e documentação da placa que os GPIOs não possuem
  conflitos com flash, PSRAM, USB ou outros periféricos.

O código não implementa medição de RSSI, controle automático de potência,
controle de bateria ou monitoramento de tensão.

## 4. Configuração de compilação

O arquivo [`main/CMakeLists.txt`](main/CMakeLists.txt) escolhe a fonte da
aplicação:

```cmake
if(CONFIG_APP_MODE_RECEIVER)
    set(APP_SOURCE "receptor_lora.c")
else()
    set(APP_SOURCE "transmissor_lora.c")
endif()
```

Portanto, o mesmo projeto pode produzir os dois firmwares. A opção é definida
em `main/Kconfig.projbuild`:

```text
Configuracao da aplicacao
  Compilar firmware receptor LoRa
```

- opção desativada: compila `transmissor_lora.c`;
- opção ativada: compila `receptor_lora.c`.

O `sdkconfig` atualmente contém:

```ini
# CONFIG_APP_MODE_RECEIVER is not set
CONFIG_433MHZ=y
CONFIG_SPI2_HOST=y
CONFIG_MISO_GPIO=8
CONFIG_SCLK_GPIO=7
CONFIG_MOSI_GPIO=9
CONFIG_NSS_GPIO=41
CONFIG_RST_GPIO=42
CONFIG_BUSY_GPIO=40
CONFIG_TXEN_GPIO=-1
CONFIG_RXEN_GPIO=-1
```

As duas placas devem utilizar a mesma frequência, o mesmo conjunto de
parâmetros LoRa e a mesma chave AES. A opção
`APP_MODE_RECEIVER` pode ser diferente entre as placas, naturalmente.

## 5. Protocolo LoRa utilizado

### 5.1 O que é LoRa neste projeto

LoRa é a modulação de rádio de longo alcance baseada em espalhamento espectral
chirp. Neste projeto, ela é utilizada diretamente entre dois SX126x. Não há
LoRaWAN, join procedure, servidor de rede, gateway, endereçamento de
aplicação ou confirmação automática.

A biblioteca configura:

| Parâmetro | Valor |
|---|---:|
| Frequência padrão | 433 MHz |
| Spreading Factor | SF7 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Preâmbulo | 8 símbolos |
| CRC LoRa | Ativado |
| IQ | Padrão, não invertido |
| Cabeçalho | Explícito/variável |
| Recepção | Contínua |

O transmissor e o receptor chamam `LoRaConfig()` com os mesmos argumentos:

```c
LoRaConfig(
    7,
    SX126X_LORA_BW_125_0,
    SX126X_LORA_CR_4_5,
    8,
    0,
    true,
    false
);
```

O argumento `payloadLen = 0` seleciona cabeçalho explícito e tamanho
variável. Isso permite que o SX126x informe o comprimento do pacote recebido.

### 5.2 Transmissão

`LoRaSend()`:

1. atualiza o tamanho do pacote no cabeçalho explícito;
2. limpa os IRQs pendentes;
3. escreve os bytes no FIFO de transmissão;
4. inicia o modo TX com timeout de 500 ms;
5. espera `TX_DONE` quando o modo síncrono é solicitado;
6. retorna o rádio para recepção contínua.

O transmissor usa `SX126x_TXMODE_SYNC`, portanto a aplicação só continua
depois que o rádio informa `TX_DONE` ou `TIMEOUT`.

### 5.3 Recepção

`LoRaReceive()` consulta o registrador de IRQ. Quando encontra
`SX126X_IRQ_RX_DONE`, limpa os IRQs e chama `ReadBuffer()` para obter o
comprimento e os bytes do FIFO RX.

O receptor consulta essa função a cada 10 ms. Os IRQs são consultados
adicionalmente a cada dois segundos para diagnóstico.

### 5.4 O que CRC LoRa garante

O CRC habilitado ajuda a rejeitar pacotes corrompidos no nível do rádio. Ele
não autentica a origem nem protege o conteúdo contra leitura. A autenticação
de aplicação é feita posteriormente pelo AES-GCM.

## 6. Criptografia AES-128-GCM

### 6.1 Parâmetros

| Item | Valor |
|---|---:|
| Algoritmo | AES-GCM |
| Tamanho da chave | 128 bits |
| Chave | 16 bytes |
| Nonce/IV | 12 bytes |
| Tag de autenticação | 16 bytes |
| AAD | Nenhum |
| API | PSA Crypto |

A chave está embutida estaticamente nos dois firmwares:

```text
10 21 32 43 54 65 76 87 98 A9 BA CB DC ED FE 0F
```

Essa chave é apenas uma chave de demonstração. Ela não deve ser utilizada em
um produto real, pois qualquer pessoa com acesso ao firmware ou ao repositório
pode obtê-la.

### 6.2 Função do AES-GCM

AES-GCM oferece simultaneamente:

- confidencialidade: o texto original não é transmitido diretamente;
- integridade: alterações no ciphertext são detectadas;
- autenticação do conhecimento da chave: um receptor sem a chave não consegue
  produzir uma tag válida.

No transmissor, a chamada é:

```c
psa_aead_encrypt(
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
```

O `ciphertext` retornado pela PSA Crypto contém o ciphertext seguido pela tag
de autenticação. Como não há dados autenticados adicionais, `additional_data`
é `NULL` e seu tamanho é zero.

No receptor, a chamada correspondente é:

```c
psa_aead_decrypt(
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
```

A mensagem só é exibida e armazenada quando o retorno é `PSA_SUCCESS`.
Qualquer falha de autenticação descarta o conteúdo como mensagem válida.

### 6.3 Nonce

Cada transmissão chama `psa_generate_random()` para produzir um nonce de
12 bytes. O nonce não precisa ser secreto, por isso ele é enviado no início
do pacote. Ele precisa ser único para cada mensagem cifrada com a mesma chave.

O projeto usa um nonce aleatório, mas não registra valores utilizados nem
implementa persistência após reinicialização. Em um produto, deve-se avaliar
um mecanismo que garanta unicidade mesmo após reset, falha de energia ou
restauração de backup. Reutilizar nonce com AES-GCM e a mesma chave é uma
falha criptográfica grave.

## 7. Formato do pacote

O pacote LoRa transmitido tem este formato:

```text
+-------------------+----------------------+----------------+
| nonce (12 bytes)  | ciphertext            | tag (16 bytes)|
+-------------------+----------------------+----------------+
```

Na implementação, `ciphertext` e `tag` são contíguos no buffer retornado por
`psa_aead_encrypt()`. Portanto, o layout efetivo é:

```text
lora_payload =
    nonce[0..11] ||
    ciphertext[0..plaintext_length-1] ||
    tag[0..15]
```

O receptor calcula:

```text
ciphertext_len = received_len - NONCE_SIZE
```

e passa todo o restante à função de descriptografia. A PSA Crypto separa
internamente o ciphertext da tag conforme o formato AEAD da API.

Para uma mensagem com `N` bytes:

```text
tamanho do payload = 12 + N + 16
                   = N + 28 bytes
```

Exemplo para `"transmissor conectado"`:

```text
plaintext: 20 bytes
nonce:     12 bytes
tag:       16 bytes
payload:   48 bytes
```

O tamanho exato deve ser confirmado pelo log, pois a contagem depende da
mensagem e não inclui terminadores C (`'\0'`).

## 8. Comportamento atual do transmissor

Apesar de algumas mensagens exibidas no código indicarem entrada pelo monitor
serial, a implementação atual não lê uma mensagem digitada. Dentro do laço
principal ela define:

```c
const char *automatic_message = "transmissor conectado";
```

e envia essa mesma mensagem a cada 10 segundos.

O fluxo real é:

1. imprimir `Mensagem original: transmissor conectado`;
2. gerar nonce aleatório;
3. cifrar a mensagem;
4. imprimir nonce e ciphertext mais tag em hexadecimal;
5. transmitir o pacote;
6. imprimir o resultado de `TX_DONE`;
7. aguardar 10 segundos;
8. repetir.

Para trocar a mensagem, altere `automatic_message` e recompile o transmissor.
Para implementar entrada interativa, seria necessário restaurar ou manter uma
rotina de leitura da USB Serial/JTAG que aguarde `Enter` antes de cifrar.

## 9. Histórico do receptor

O receptor mantém:

```c
#define HISTORY_SIZE       10
#define MAX_MESSAGE_SIZE   128
```

O histórico é um buffer circular em RAM:

- armazena no máximo dez mensagens;
- quando a décima primeira chega, substitui a mais antiga;
- não é persistente;
- é perdido após reset ou falta de energia;
- só armazena mensagens que passaram na autenticação GCM.

O histórico é impresso a cada 30 segundos. A mensagem recebida também é
impressa imediatamente:

```text
Mensagem recebida: transmissor conectado
```

## 10. Limites de tamanho

Os buffers principais são:

```c
#define MAX_CIPHER_SIZE 128
#define LORA_PAYLOAD_SIZE (NONCE_SIZE + MAX_CIPHER_SIZE)
```

O buffer de saída criptográfica tem 128 bytes, mas AES-GCM acrescenta uma tag
de 16 bytes ao plaintext. Assim, o maior plaintext que cabe com segurança
nesse buffer é aproximadamente:

```text
128 - 16 = 112 bytes
```

Na implementação atual do transmissor, a mensagem fixa é curta e não atinge
esse limite. Se uma futura entrada de usuário aceitar mensagens longas, o
código deve validar explicitamente:

```text
message_length <= MAX_CIPHER_SIZE - TAG_SIZE
```

Antes de chamar `psa_aead_encrypt()`.

O receptor possui `plaintext[MAX_CIPHER_SIZE + 1]`, o que reserva espaço para
o terminador C. O terminador não é transmitido nem faz parte da autenticação.

## 11. Compilação e gravação

### 11.1 Pré-requisitos

- ESP-IDF instalado e configurado;
- `IDF_PATH` definido;
- Python e ferramentas do ESP-IDF disponíveis;
- duas placas compatíveis com ESP32-S3, se a demonstração usar dois nós;
- dois módulos SX126x compatíveis;
- antenas adequadas;
- cabo USB para cada placa.

Verifique o ambiente com:

```bash
idf.py --version
```

### 11.2 Configurar o transmissor

Para compilar o transmissor:

```bash
idf.py menuconfig
```

Deixe desmarcada a opção:

```text
Configuracao da aplicacao
  [ ] Compilar firmware receptor LoRa
```

Confirme também:

- frequência correta;
- GPIOs SPI corretos;
- `SPI2_HOST` ou o periférico realmente ligado;
- opção de TCXO de acordo com o hardware;
- `TXEN` e `RXEN` de acordo com o módulo.

Compile e grave:

```bash
idf.py build
idf.py -p PORTA flash
idf.py -p PORTA monitor
```

Substitua `PORTA` pela porta serial real da placa.

### 11.3 Configurar o receptor

Em uma segunda cópia/configuração do projeto:

```bash
idf.py menuconfig
```

Ative:

```text
Configuracao da aplicacao
  [*] Compilar firmware receptor LoRa
```

Use os mesmos parâmetros RF, a mesma chave e a mesma pinagem. Depois:

```bash
idf.py build
idf.py -p PORTA_RECEPTOR flash
idf.py -p PORTA_RECEPTOR monitor
```

### 11.4 Cuidados com `sdkconfig`

O `sdkconfig` é específico do alvo e da configuração local. Ao alternar entre
transmissor e receptor, confirme o valor efetivo de:

```ini
CONFIG_APP_MODE_RECEIVER
```

Não basta editar somente o arquivo-fonte: o `main/CMakeLists.txt` escolhe o
arquivo compilado com base no símbolo Kconfig gerado.

## 12. Logs esperados

### Transmissor

Um início normal deve conter mensagens equivalentes a:

```text
=== TRANSMISSOR LORA COM AES-GCM ===
Criptografia inicializada.
Mensagem original: transmissor conectado
--- TRANSMISSAO LORA ---
Nonce: ...
Dados cifrados + tag: ...
SUCESSO: pacote LoRa transmitido (...)
Proxima transmissao em 10 segundos...
```

O componente também pode imprimir logs do SPI e do SX126x durante a
inicialização.

### Receptor

Um início normal deve conter:

```text
=== RECEPTOR LORA COM AES-GCM ===
Criptografia inicializada.
Frequencia: 433000000 Hz
Aguardando pacotes LoRa...
```

Depois de um pacote válido:

```text
--- PACOTE RECEBIDO ---
Tamanho: ...
Nonce: ...
Dados cifrados + tag: ...
Mensagem recebida: transmissor conectado
```

### Diagnóstico de falhas

| Sintoma | Interpretação provável |
|---|---|
| Receptor imprime `TRANSMISSOR` | Firmware errado; `APP_MODE_RECEIVER` não foi ativado |
| Nenhum `RX_DONE` | Frequência, antena, pinagem, alimentação ou parâmetros LoRa incompatíveis |
| `CRC_ERR` | Pacote corrompido, interferência ou parâmetros físicos diferentes |
| `RX_DONE` e mensagem não autenticada | Chave, nonce, comprimento ou bytes do pacote incompatíveis |
| `ERRO ao inicializar o SX126x` | SPI, reset, BUSY, GPIO ou módulo incompatível |
| `ERRO na criptografia` | Buffer insuficiente, chave inválida ou erro PSA |
| `TX_TIMEOUT` | Rádio não concluiu a transmissão ou hardware RF/alimentação está inadequado |

O transmissor e o receptor devem estar com a mesma frequência. Uma placa em
433 MHz não recebe corretamente uma placa em 866 MHz ou 915 MHz. Além disso,
as placas precisam usar o mesmo SF, bandwidth, coding rate, preâmbulo, CRC e
polaridade IQ.

## 13. Modelo de segurança

### O que é protegido

- o texto da mensagem não é enviado em claro;
- alterações no ciphertext são detectadas;
- alterações no nonce normalmente fazem a autenticação falhar;
- um atacante sem a chave não consegue gerar uma tag GCM válida.

### O que não é protegido atualmente

- a chave está fixa no firmware;
- não existe provisionamento seguro de chaves;
- não existe rotação ou revogação de chaves;
- não existe identificação criptográfica de múltiplos dispositivos;
- não existe contador de sequência;
- não existe proteção contra replay;
- o nonce não é persistido;
- nonce e ciphertext são impressos em logs;
- não existe confirmação de entrega;
- não existe assinatura ou autenticação de firmware descrita no projeto;
- o rádio pode revelar metadados como atividade, frequência e tamanho do
  pacote.

O pacote autenticado também não possui um cabeçalho de protocolo contendo
versão, dispositivo de origem, destino, contador ou tipo de mensagem. Para uma
versão de produção, recomenda-se adicionar um cabeçalho autenticado como AAD,
um contador monotônico e uma política explícita para mensagens repetidas.

## 14. Melhorias recomendadas

1. **Gerenciamento de chaves**
   - remover a chave fixa do código;
   - provisionar chaves por dispositivo;
   - armazená-las em armazenamento seguro quando disponível;
   - usar rotação e revogação.

2. **Proteção contra replay**
   - incluir contador monotônico no pacote;
   - autenticar o contador como AAD;
   - persistir o maior contador recebido;
   - rejeitar contadores repetidos ou antigos.

3. **Validação de comprimento**
   - validar o comprimento antes da cifra;
   - rejeitar pacotes menores que `nonce + tag`;
   - verificar o tamanho máximo antes de copiar ou imprimir dados.

4. **Confirmação de entrega**
   - adicionar ACK autenticado;
   - incluir identificador da mensagem;
   - controlar retransmissões e duplicatas.

5. **Diagnóstico RF**
   - registrar RSSI e SNR;
   - mostrar motivo do IRQ;
   - separar CRC, timeout e header error;
   - testar com distância, antena e potência controladas.

6. **Robustez**
   - tratar falhas de SPI sem bloquear indefinidamente;
   - evitar `assert` como única resposta a falha em produto;
   - liberar `key_id` com `psa_destroy_key()` quando a aplicação terminar;
   - adicionar testes de tamanhos de mensagem e pacotes corrompidos.

7. **Entrada do usuário**
   - decidir se o transmissor será automático ou controlado pelo monitor
     serial;
   - documentar o limite real de caracteres;
   - rejeitar mensagens vazias e longas com diagnóstico claro.


## 16. Resumo técnico

```text
Microcontrolador: ESP32-S3
Framework:       ESP-IDF + FreeRTOS
Rádio:           SX126x / módulo RA-01S compatível
SPI:             SPI2_HOST, modo 0, 9 MHz
Frequência:      433 MHz por padrão
LoRa:            SF7, BW 125 kHz, CR 4/5, preâmbulo 8
CRC LoRa:        ativado
Criptografia:    AES-128-GCM
Nonce:           12 bytes, aleatório por transmissão
Tag:             16 bytes
AAD:             nenhum
Formato:         nonce || ciphertext || tag
Modo atual TX:   "transmissor conectado" a cada 10 s
Modo RX:         recepção contínua e histórico circular de 10 mensagens
```
