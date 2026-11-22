# Estação IoT — MQTT com Mosquitto

**Integrantes:** Milton Roberto e Marcelo Telles.

Implementação do enunciado AMF 2026-02 usando ESP32, DHT11, LED e serviço Python.
Para rodar sem placa física, veja a versão em `wokwi/README.md` e o
[projeto salvo no Wokwi](https://wokwi.com/projects/481337832642259969).
Como a pasta inicial estava vazia, esta implementação foi criada a partir dos requisitos;
o código-base mencionado no enunciado não foi fornecido.

## 1. Montagem

| Componente | Ligação no ESP32 |
| --- | --- |
| DHT11 VCC | 3V3 |
| DHT11 GND | GND |
| DHT11 DATA | GPIO 4 |
| LED externo: ânodo | GPIO 2, por resistor de 220 Ω |
| LED externo: cátodo | GND |

Para DHT11 sem módulo, use pull-up de aproximadamente 10 kΩ entre DATA e 3V3.
O LED embutido pode substituir o externo se a sua placa o tiver no GPIO 2.
Confira a pinagem da sua placa; ajuste `LED_PIN` e `LED_ACTIVE_HIGH` se necessário.

## 2. Firmware

1. Na Arduino IDE, instale o pacote **esp32 by Espressif Systems** e selecione sua placa/porta.
2. Instale **PubSubClient**, **DHT sensor library by Adafruit** e **Adafruit Unified Sensor**.
3. Copie `esp32/estacao_conexao/config.example.h` para `config.h`, na mesma pasta.
4. Preencha Wi-Fi de 2,4 GHz e `MQTT_NOME` com um identificador próprio, por exemplo `aluno-amf-01`.
5. Abra `esp32/estacao_conexao/estacao_conexao.ino`, compile e grave.
6. Abra o Monitor Serial a **115200 baud**.

O `config.h` é ignorado pelo Git. O serviço e a placa devem usar exatamente o mesmo nome.
O broker público e a porta 1883 não fornecem autenticação nem criptografia.
SSID, IP e leituras serão públicos: use uma rede de laboratório e não publique segredos.

## 3. Serviço Python

O ambiente `.venv` deste computador foi preparado em 22/11/2026. Para abrir o
serviço já configurado para esta entrega, execute na raiz do projeto:

```powershell
.\iniciar-servico.cmd
```

Também é possível abrir `iniciar-servico.cmd` com um duplo clique. Ele usa o nome
MQTT da simulação e registra as mensagens em `servico/registros/wokwi.jsonl`.
Para começar os prints, inicie o serviço e depois clique em iniciar no Wokwi.

### Instalação em outro computador

No PowerShell, na raiz deste projeto, com Python 3.10 ou superior:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r servico\requirements.txt
.\.venv\Scripts\python.exe servico\servico.py --nome aluno-amf-01 --log servico\registros\sessao.jsonl
```

Substitua `aluno-amf-01` pelo `MQTT_NOME` do firmware. Não é preciso ativar o ambiente.
Neste computador, o Python 3.11 foi encontrado, mas o launcher `py` não o reconheceu.
Se aparecer `No installed Python found`, substitua o primeiro comando por:

```powershell
& 'C:\Users\borto\AppData\Local\Programs\Python\Python311\python.exe' -m venv .venv
```

O serviço lê todos os tópicos desse prefixo, mostra os valores e aceita comandos pelo terminal.
As mensagens podem aparecer enquanto você digita, pois a recepção roda em outra thread.

| Digite | Mensagem enviada em `comando` | Efeito |
| --- | --- | --- |
| `25` ou `limite:25` | `limite:25.0` | Ativa modo automático: LED ligado se temperatura > 25 °C |
| `on` | `led:on` | Força LED ligado durante a janela acordada |
| `off` | `led:off` | Força LED desligado |
| `auto` | `auto` | Volta à regra de temperatura com o limite atual |
| `limpar` | payload vazio retido | Remove o último comando do broker |
| `sair` | — | Encerra o serviço |

O limite aceito é de 0 a 50 °C, faixa nominal do DHT11. O separador decimal pode ser ponto ou vírgula.
O Python confirma que o broker recebeu o comando (QoS 1); só `/ack` confirma que o ESP32 o aplicou.

### Comandos durante o sono

O serviço publica o comando com `retain=True`. O broker guarda **apenas o último comando** desse tópico
e o entrega a cada nova assinatura da placa. Não é uma fila: se enviar `25` e depois `off`
antes do despertar, apenas `off` será entregue. Para aplicar ambos, espere o `/ack` da regra
antes de enviar o comando seguinte. Os comandos são idempotentes e podem ser reaplicados.
`limpar` remove o comando retido; não altera a regra já armazenada na memória RTC da placa.

O contador, o limite e o modo sobrevivem ao deep sleep por `RTC_DATA_ATTR`.
Não há persistência garantida após desligar a alimentação ou reset comum; os valores
iniciais são limite 25 °C e modo automático, e o comando retido poderá ser aplicado novamente.

## 4. Tópicos

Todos começam com `sis1a/SEU-NOME/`. A placa publica leituras e estados retidos;
`erro` e `ack` são eventos sem retenção.

| Sufixo | Conteúdo |
| --- | --- |
| `temp` | Temperatura em °C, uma casa decimal |
| `umid` | Umidade relativa em %, uma casa decimal |
| `led` | `ON` ou `OFF`; publicado no início de cada ciclo e em cada mudança |
| `wifi` | SSID |
| `rssi` | Intensidade do sinal em dBm |
| `log/wifi` | JSON: `ssid`, `rssi`, `ip`, `ms` |
| `hora` | Data e hora UTC−3 ou `indisponivel` |
| `log/tls` | JSON: `hora`, `relogio_ok`, `transporte`, `fuso` |
| `log/sistema` | JSON: `ciclos`, `heap` em bytes, `acordar`, `reset_reason` numérico do ESP-IDF e `sono_ms` |
| `regra` | JSON com `limite` e `modo` (`auto` ou `manual`) |
| `sensor` | `ok` ou `falha` |
| `status` | `acordado`, `dormindo` ou LWT `desconectado` |
| `erro` | Diagnóstico pontual |
| `ack` | Texto do comando aplicado pelo firmware |
| `comando` | Entrada do ESP32, publicada pelo serviço |

Valores retidos são a última observação, não uma leitura contínua. Verifique `status`, `hora`
e `ciclos` para avaliar a atualização. Em falha de DHT11 os valores retidos de temp/umid
são removidos, publica-se `sensor=falha` e o modo automático desliga o LED.
`relogio_ok` significa que há data válida no relógio; não comprova sincronização NTP nova
em cada ciclo nem uma sessão TLS. Esta conexão usa TCP comum.

## 5. Ciclo e escolhas

Cada `setup()` incrementa o contador, tenta Wi-Fi, obtém hora por NTP, conecta MQTT,
assina comandos, lê DHT11 e publica os tópicos. Depois atende MQTT por 5 segundos,
desliga o LED, publica `dormindo`, desconecta e dorme por 30 segundos.
O período entre leituras é **30 segundos mais todo o tempo acordado**, não 30 segundos exatos.
Amostragem e regra automática são executadas uma vez por ciclo; os comandos atualizam o LED
durante a janela usando a leitura mais recente.

O LED é desligado antes do sono; não usamos retenção elétrica do GPIO. Um comando `on`
volta a ligar o LED no próximo despertar, conforme o modo salvo. Assim a indicação física
ocorre enquanto o ESP32 está acordado. `led=OFF` antes de `status=dormindo` representa isso.
Ligar um atuador continuamente durante o sono exigiria outra escolha de hardware/firmware.

As tentativas de Wi-Fi e MQTT têm limites para não manter o equipamento ligado indefinidamente;
em falha, a placa dorme e tenta no próximo ciclo. Falhas de rede durante a janela são recuperadas
no próximo despertar. As publicações do PubSubClient usam QoS 0 e podem se perder em falhas de rede.
O retorno `enviado` no Serial confirma o envio local, não um ACK do broker.

## 6. Conferência e entrega

Siga o roteiro em `ENTREGA.md`, preencha identificação e anexe as capturas reais em `evidencias/`.
Não inclua a senha do Wi-Fi na entrega pública. A configuração de exemplo é suficiente
para outro avaliador preencher sua rede. Para esta entrega sem placa física, use a
simulação e as evidências descritas em `wokwi/README.md`.

## Verificações locais

Com as dependências instaladas no ambiente virtual:

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s servico -v
```

Os testes cobrem validação dos comandos, nomes MQTT e o fluxo do serviço com cliente
simulado (assinatura, publicação e registro JSONL). Eles não substituem o teste com a placa.
O firmware foi compilado para `esp32:esp32:esp32` com configuração de exemplo;
a compilação não grava o dispositivo nem verifica a montagem elétrica.

## Referências técnicas

- [Mosquitto público: portas, acesso e limitações](https://test.mosquitto.org/)
- [API PubSubClient](https://pubsubclient.knolleary.net/api)
- [Paho MQTT Python: cliente e callbacks versão 2](https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html)
- [Espressif: deep sleep no Arduino ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/deepsleep.html)
