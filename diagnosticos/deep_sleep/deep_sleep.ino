#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>
RTC_DATA_ATTR int ciclos = 0;
RTC_DATA_ATTR int64_t inicioSono = 0;
void setup() {
  Serial.begin(115200);
  delay(100);
  struct timeval tv; gettimeofday(&tv, nullptr);
  int64_t agora = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  Serial.printf("DIAGNOSTICO ciclo=%d reset=%d wakeup=%d IDF=%s\n", ++ciclos, (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(), ESP.getSdkVersion());
  if (inicioSono) Serial.printf("SONO medido_us=%lld\n", agora-inicioSono);
  Serial.println("Entrando em deep sleep real por 30 segundos");
  Serial.flush();
  gettimeofday(&tv, nullptr); inicioSono=(int64_t)tv.tv_sec*1000000LL+tv.tv_usec;
  esp_err_t erro=esp_sleep_enable_timer_wakeup(30ULL*1000000ULL);
  if(erro!=ESP_OK) { Serial.printf("ERRO timer=%d\n",(int)erro); return; }
  esp_deep_sleep_start();
}
void loop() { delay(1000); }
