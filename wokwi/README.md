# Simulação Wokwi

Projeto construído e salvo em:
https://wokwi.com/projects/481337832642259969

Clique no botão verde de iniciar a simulação.

Esta versão usa ESP32 DevKitC V4, DHT22 com DATA no **GPIO 15**, LED no GPIO 2
e resistor de 220 Ω. O GPIO 15 foi validado no simulador, seguindo a configuração
do exemplo de DHT22 indicado pela documentação do Wokwi. A versão física em
`../esp32/estacao_conexao/` continua usando DHT11 e GPIO 4.

A rede virtual é `Wokwi-GUEST`, sem senha. Não coloque a senha do Wi-Fi real neste projeto público.
O código tem a configuração dentro do `sketch.ino`, sem depender de `config.h`.
`libraries.txt` contém PubSubClient e DHT sensor library for ESPx.

## Serviço no computador

Na raiz do projeto, após instalar as dependências conforme o README principal:

```powershell
.\iniciar-servico.cmd
```

O ambiente Python deste computador já foi preparado em 22/11/2026.

Digite `50`, `on`, `off` ou `25` e aguarde o tópico `ack`.
O prefixo é `sis1a/milton-amf-iot4-481337832642259969/`.

## Resultado observado em 21/11/2026

- Compilação e execução do firmware no Wokwi confirmadas.
- Recebimento pelo Mosquitto de `temp=27.8`, `umid=61.0`, `sensor=ok`, logs de Wi-Fi/hora/sistema e estado do LED.
- Confirmações de aplicação recebidas para `limite:50.0`, `led:on`, `led:off` e `limite:25.0`.
- Comandos retidos de teste removidos do broker ao terminar.
- Projeto salvo e simulação deixada parada, pronta para iniciar novamente.

### Correção do sono no simulador

**Validação aprovada em 21/11/2026:** o broker recebeu ciclos 1 → 2 → 3.
Nos ciclos 2 e 3: `acordar=timer`, `reset_reason=8`, `sono_ms=31492`.
O Serial confirmou `wakeup=4` e `DEEPSLEEP_RESET`. Evidências:
`../evidencias/validacao-sono.jsonl`, `../evidencias/wokwi-serial-sono.txt`
e `../evidencias/wokwi-deep-sleep.jpg`.

O reinício anterior por watchdog (`TG1WDT_SYS_RESET`, `reset_reason=5`) foi
corrigido na versão Wokwi. Imediatamente antes de `esp_deep_sleep_start()`,
`pausarWatchdogsParaSonoWokwi()` desativa os dois watchdogs dos grupos de timers
que estavam interrompendo o sono no simulador. Eles são reconfigurados no boot
seguinte. O ajuste não foi incluído no firmware da placa física.

O código continua usando o timer de 30 segundos, sono profundo e memória RTC;
não substitui o sono por `delay`. O log inclui `sono_ms`, medido com o relógio
RTC antes do sono e no início do próximo `setup()`, antes da sincronização NTP.
Essa medida inclui o tempo do boot até o `setup()`, portanto fica um pouco acima
dos 30 segundos configurados. O intervalo entre publicações inclui também Wi-Fi,
MQTT, leitura e janela de comandos.

Para repetir a validação, execute o comando abaixo antes de iniciar a simulação:

```powershell
.\.venv\Scripts\python.exe servico\validar_sono.py --nome milton-amf-iot4-481337832642259969 --log evidencias\validacao-sono.jsonl
```

O verificador ignora mensagens antigas retidas e exige três ciclos novos, contador
consecutivo e dois despertares com `acordar=timer`, `reset_reason=8` e medida
entre 29,5 e 35 segundos. Registra as mensagens reais recebidas do broker.

Evidências reais: `../evidencias/wokwi-circuito.jpg` e `../evidencias/wokwi-mqtt.jsonl`.
O registro MQTT corresponde à simulação; não é um teste de hardware físico.

Referências: [DHT22 no Wokwi](https://docs.wokwi.com/parts/wokwi-dht22),
[rede virtual](https://docs.wokwi.com/guides/esp32-wifi).
