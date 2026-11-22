#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
// Ajuste restrito ao simulador: os MWDT continuam contando no sono no Wokwi.
#include "hal/wdt_hal.h"
// Configuracao do simulador Wokwi

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
// Use o mesmo nome no argumento --nome do servico.py, sem /, + ou #.
#define MQTT_NOME "milton-amf-iot4-481337832642259969"
#define MQTT_HOST "test.mosquitto.org"
#define MQTT_PORT 1883
#define DHT_PIN 15
#define LED_PIN 2
#define LED_ACTIVE_HIGH true
#define COMMAND_WINDOW_MS 5000UL
#define SLEEP_SECONDS 30ULL
#define WOKWI_SLEEP_WDT_WORKAROUND 1


WiFiClient rede;
PubSubClient mqtt(rede);
DHTesp dht;
RTC_DATA_ATTR int ciclos = 0;
RTC_DATA_ATTR float limite = 25.0f;
RTC_DATA_ATTR bool automatico = true;
RTC_DATA_ATTR bool ledManual = false;
RTC_DATA_ATTR int64_t inicioSonoUs = 0;
int64_t sonoMedidoUs = 0;
bool ledLigado = false;
float temperatura = NAN;
String prefixo;
unsigned long wifiMs = 0;

String topico(const char *sufixo) { return prefixo + "/" + sufixo; }

// PubSubClient publica em QoS 0: true indica envio local, nao ACK do broker.
bool publicar(const char *sufixo, const String &valor, bool reter = true) {
  bool ok = mqtt.publish(topico(sufixo).c_str(), valor.c_str(), reter);
  Serial.printf("PUB %s -> %s [%s]\n", topico(sufixo).c_str(), valor.c_str(),
                ok ? "enviado" : "FALHOU");
  return ok;
}

String jsonString(const String &s) {
  String resultado = "\"";
  for (size_t i = 0; i < s.length(); ++i) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') { resultado += '\\'; resultado += char(c); }
    else if (c < 32) {
      char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c);
      resultado += escape;
    } else resultado += char(c);
  }
  return resultado + "\"";
}

void definirLed(bool ligar, bool forcarPublicacao = false) {
  bool mudou = ligar != ledLigado;
  ledLigado = ligar;
  digitalWrite(LED_PIN, (ligar == LED_ACTIVE_HIGH) ? HIGH : LOW);
  if (mudou || forcarPublicacao) publicar("led", ligar ? "ON" : "OFF");
}

void aplicarRegra() {
  // Falha do sensor em modo automatico deixa o LED desligado.
  definirLed(automatico ? (isfinite(temperatura) && temperatura > limite) : ledManual);
}

void publicarRegra() {
  publicar("regra", String("{\"limite\":") + String(limite, 1) +
           ",\"modo\":\"" + (automatico ? "auto" : "manual") + "\"}");
}

void receber(char *topic, byte *payload, unsigned int tamanho) {
  if (String(topic) != topico("comando") || tamanho == 0 || tamanho > 64) return;
  String comando;
  for (unsigned int i = 0; i < tamanho; ++i) comando += char(payload[i]);
  comando.trim(); comando.toLowerCase();
  bool valido = true;
  if (comando == "led:on" || comando == "led:off") {
    automatico = false; ledManual = comando == "led:on";
  } else if (comando == "auto") {
    automatico = true;
  } else if (comando.startsWith("limite:")) {
    const char *inicio = comando.c_str() + 7;
    char *fim = nullptr;
    float novo = strtof(inicio, &fim);
    if (fim == inicio || *fim != '\0' || !isfinite(novo) || novo < 0 || novo > 50)
      valido = false;
    else { limite = novo; automatico = true; }
  } else valido = false;
  if (!valido) { publicar("erro", "Comando invalido", false); return; }
  Serial.printf("CMD recebido: %s\n", comando.c_str());
  aplicarRegra();
  publicarRegra();
  publicar("ack", comando, false);
}

void pausarWatchdogsParaSonoWokwi() {
#if WOKWI_SLEEP_WDT_WORKAROUND
  // Pausa somente ao entrar no sono. O boot seguinte reconfigura os watchdogs.
  // Nao usar este ajuste como recomendacao para firmware de producao.
  wdt_hal_context_t wdt0 = {};
  wdt0.inst = WDT_MWDT0; wdt0.mwdt_dev = &TIMERG0;
  wdt_hal_context_t wdt1 = {};
  wdt1.inst = WDT_MWDT1; wdt1.mwdt_dev = &TIMERG1;
  for (wdt_hal_context_t *wdt : {&wdt0, &wdt1}) {
    wdt_hal_write_protect_disable(wdt);
    wdt_hal_feed(wdt);
    wdt_hal_set_flashboot_en(wdt, false);
    wdt_hal_disable(wdt);
    wdt_hal_write_protect_enable(wdt);
  }
#endif
}

void dormir() {
  // O LED demonstra o comando durante a janela acordada; nao mantemos GPIO no sono.
  if (mqtt.connected()) {
    definirLed(false);
    publicar("status", "dormindo");
    mqtt.loop();
    delay(150);
    mqtt.disconnect();
  } else digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
  WiFi.disconnect(true);
  Serial.printf("Ciclo %d: deep sleep por %llu s\n", ciclos, SLEEP_SECONDS);
  Serial.flush();
  esp_err_t erro = esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
  if (erro != ESP_OK) {
    Serial.printf("ERRO ao configurar timer de sono: %d\n", (int)erro);
    while (true) delay(1000);
  }
  struct timeval antes;
  gettimeofday(&antes, nullptr);
  inicioSonoUs = (int64_t)antes.tv_sec * 1000000LL + antes.tv_usec;
  pausarWatchdogsParaSonoWokwi();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  struct timeval agora;
  gettimeofday(&agora, nullptr);
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && inicioSonoUs > 0)
    sonoMedidoUs = (int64_t)agora.tv_sec * 1000000LL + agora.tv_usec - inicioSonoUs;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH);
  ++ciclos;
  Serial.printf("BOOT ciclo=%d reset=%d wakeup=%d sono_ms=%lld IDF=%s\n",
    ciclos, (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(),
    sonoMedidoUs / 1000LL, ESP.getSdkVersion());
  prefixo = String("sis1a/") + MQTT_NOME;
  if (String(MQTT_NOME).length() == 0 || String(MQTT_NOME).indexOf('/') >= 0 ||
      String(MQTT_NOME).indexOf('+') >= 0 || String(MQTT_NOME).indexOf('#') >= 0) {
    Serial.println("Nome MQTT invalido. Corrija config.h."); dormir();
  }
  dht.setup(DHT_PIN, DHTesp::DHT22);
  unsigned long inicio = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000UL) delay(100);
  wifiMs = millis() - inicio;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi indisponivel; nova tentativa no proximo ciclo."); dormir();
  }

  // UTC-3 fixo para esta atividade; o relogio continua contando no deep sleep.
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com");
  struct tm hora;
  bool relogioOk = getLocalTime(&hora, 5000);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(receber);
  if (!mqtt.setBufferSize(768)) { Serial.println("Sem memoria para MQTT."); dormir(); }
  mqtt.setSocketTimeout(3);
  String cliente = "esp32-" + WiFi.macAddress();
  cliente.replace(":", "");
  inicio = millis();
  while (!mqtt.connected() && millis() - inicio < 12000UL) {
    mqtt.connect(cliente.c_str(), topico("status").c_str(), 0, true, "desconectado");
    if (!mqtt.connected()) delay(300);
  }
  if (!mqtt.connected()) { Serial.printf("MQTT falhou: %d\n", mqtt.state()); dormir(); }
  if (!mqtt.subscribe(topico("comando").c_str(), 1)) {
    Serial.println("Falha ao enviar assinatura."); dormir();
  }

  // DHT22 precisa estabilizar apos energizacao. Wi-Fi/NTP normalmente ja excedem 2 s.
  delay(2000);
  TempAndHumidity leitura = dht.getTempAndHumidity();
  temperatura = leitura.temperature;
  float umidade = leitura.humidity;
  if (isfinite(temperatura) && isfinite(umidade)) {
    publicar("temp", String(temperatura, 1));
    publicar("umid", String(umidade, 1));
    publicar("sensor", "ok");
  } else {
    temperatura = NAN;
    // Limpa valores retidos antigos para nao apresenta-los como uma leitura atual.
    publicar("temp", ""); publicar("umid", "");
    publicar("sensor", "falha");
    publicar("erro", "Falha ao ler DHT22", false);
  }
  publicar("led", "OFF");
  aplicarRegra();
  publicarRegra();
  publicar("wifi", WiFi.SSID());
  publicar("rssi", String(WiFi.RSSI()));
  publicar("log/wifi", String("{\"ssid\":") + jsonString(WiFi.SSID()) +
    ",\"rssi\":" + String(WiFi.RSSI()) + ",\"ip\":" + jsonString(WiFi.localIP().toString()) +
    ",\"ms\":" + String(wifiMs) + "}");
  char horario[24] = "indisponivel";
  if (relogioOk) strftime(horario, sizeof(horario), "%Y-%m-%d %H:%M:%S", &hora);
  publicar("hora", horario);
  publicar("log/tls", String("{\"hora\":") + jsonString(horario) +
    ",\"relogio_ok\":" + (relogioOk ? "true" : "false") +
    ",\"transporte\":\"TCP sem TLS\",\"fuso\":\"UTC-03:00\"}");
  publicar("log/sistema", String("{\"ciclos\":") + String(ciclos) +
    ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"acordar\":\"" +
    (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "reset/outro") +
    "\",\"reset_reason\":" + String((int)esp_reset_reason()) +
    ",\"sono_ms\":" + String((long)(sonoMedidoUs / 1000LL)) + "}");
  publicar("status", "acordado");

  unsigned long t0 = millis();
  while (millis() - t0 < COMMAND_WINDOW_MS) { mqtt.loop(); delay(10); }
  dormir();
}

void loop() { /* Cada despertar executa setup() novamente. */ }
