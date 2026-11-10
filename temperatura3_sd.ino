// Coleta de temperatura, umidade, data e horário com gravação em microSD
// Placa: Arduino UNO R4 WiFi | Sensor DHT22 | Módulo MicroSD Card Adapter
//
// A hora é acertada sozinha pela internet (servidor NTP) usando o roteador
// do celular (dados móveis). Depois de acertar, o Wi-Fi é desligado e o
// Arduino segue marcando a hora pelo relógio interno.
// Se faltar energia, ao religar ele tenta acertar a hora de novo.
//
// Biblioteca necessária: "DHT sensor library" (Adafruit)
// WiFiS3, SD, SPI e RTC já vêm com a placa.
//
// Ligações:
//   DHT22:   + -> 5V   | out -> D2  | - -> GND
//   MicroSD: VCC -> 5V | GND -> GND | MISO -> D12 | MOSI -> D11 | SCK -> D13 | CS -> D10

#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "RTC.h"

// ====== CONFIGURE AQUI ======
const char NOME_WIFI[]  = "S24 FE de Ana";
const char SENHA_WIFI[] = "94860270";
const long FUSO_HORARIO_S = -3L * 3600;      // Brasília: UTC-3
const unsigned long INTERVALO_MS = 10000;    // tempo entre leituras (mínimo 2000 ms)
// ============================

#define PINO_DHT 2
#define TIPO_DHT DHT22
#define PINO_CS_SD 10

const char NOME_ARQUIVO[] = "DADOS.CSV";
const unsigned long TENTAR_DE_NOVO_MS = 600000;  // se não acertou a hora, tenta a cada 10 min

DHT dht(PINO_DHT, TIPO_DHT);
WiFiUDP udp;
bool sdOk = false;
bool horaAcertada = false;
unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaHora = 0;

void setup() {
  Serial.begin(9600);
  delay(1500);
  dht.begin();
  RTC.begin();

  if (!RTC.isRunning()) {
    RTCTime inicial(1, Month::JANUARY, 2026, 0, 0, 0,
                    DayOfWeek::THURSDAY, SaveLight::SAVING_TIME_INACTIVE);
    RTC.setTime(inicial);
  }

  horaAcertada = acertarHoraPelaInternet();
  ultimaTentativaHora = millis();

  Serial.print("Iniciando cartao SD... ");
  if (SD.begin(PINO_CS_SD)) {
    sdOk = true;
    Serial.println("OK");
    if (!SD.exists(NOME_ARQUIVO)) {
      File arquivo = SD.open(NOME_ARQUIVO, FILE_WRITE);
      if (arquivo) {
        arquivo.println("data,horario,temperatura_C,umidade_%");
        arquivo.close();
      }
    }
  } else {
    Serial.println("FALHOU - confira as ligacoes e se o cartao esta em FAT32");
  }

  Serial.println("Para acertar a hora manualmente, envie: DD/MM/AAAA HH:MM:SS");
  delay(2000);  // tempo para o DHT22 estabilizar
}

void loop() {
  // Permite acertar a hora manualmente pelo Monitor Serial
  verificarAjusteDeHora();

  // Se ainda não conseguiu acertar a hora, tenta de novo de tempos em tempos
  if (!horaAcertada && millis() - ultimaTentativaHora >= TENTAR_DE_NOVO_MS) {
    horaAcertada = acertarHoraPelaInternet();
    ultimaTentativaHora = millis();
  }

  if (ultimaLeitura == 0 || millis() - ultimaLeitura >= INTERVALO_MS) {
    ultimaLeitura = millis();
    registrarLeitura();
  }
}

// ---------- Hora pela internet (NTP) ----------

bool acertarHoraPelaInternet() {
  Serial.print("Conectando ao Wi-Fi ");
  Serial.print(NOME_WIFI);

  WiFi.begin(NOME_WIFI, SENHA_WIFI);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Nao conectou ao Wi-Fi. A hora nao foi acertada.");
    WiFi.end();
    return false;
  }

  delay(1000);  // espera pegar IP
  unsigned long epoch = 0;
  for (int tentativa = 0; tentativa < 5 && epoch == 0; tentativa++) {
    epoch = pedirHoraNTP();
  }
  WiFi.end();   // desliga o Wi-Fi, não precisa mais

  if (epoch == 0) {
    Serial.println("Servidor de hora nao respondeu.");
    return false;
  }

  RTCTime agora((time_t)(epoch + FUSO_HORARIO_S));
  RTC.setTime(agora);

  char data[11], horario[9];
  lerDataHora(data, horario);
  Serial.print("Hora acertada pela internet: ");
  Serial.print(data);
  Serial.print(" ");
  Serial.println(horario);
  return true;
}

unsigned long pedirHoraNTP() {
  const int TAMANHO = 48;
  byte pacote[TAMANHO] = {0};
  pacote[0] = 0b11100011;   // pedido padrão NTP
  pacote[2] = 6;
  pacote[3] = 0xEC;
  pacote[12] = 49;
  pacote[13] = 0x4E;
  pacote[14] = 49;
  pacote[15] = 52;

  udp.begin(2390);
  udp.beginPacket("pool.ntp.org", 123);
  udp.write(pacote, TAMANHO);
  udp.endPacket();

  unsigned long inicio = millis();
  while (millis() - inicio < 3000) {
    if (udp.parsePacket() >= TAMANHO) {
      udp.read(pacote, TAMANHO);
      udp.stop();
      unsigned long seg1900 = ((unsigned long)pacote[40] << 24) |
                              ((unsigned long)pacote[41] << 16) |
                              ((unsigned long)pacote[42] << 8)  |
                               (unsigned long)pacote[43];
      return seg1900 - 2208988800UL;   // converte para hora Unix (desde 1970)
    }
    delay(50);
  }
  udp.stop();
  return 0;
}

// ---------- Leitura e gravação ----------

void registrarLeitura() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro ao ler o DHT22");
    return;
  }

  char data[11], horario[9];
  lerDataHora(data, horario);

  Serial.print(data);
  Serial.print("  ");
  Serial.print(horario);
  Serial.print("  |  Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.print(" C  |  Umidade: ");
  Serial.print(umidade, 1);
  Serial.println(" %");

  if (sdOk) {
    File arquivo = SD.open(NOME_ARQUIVO, FILE_WRITE);
    if (arquivo) {
      arquivo.print(data);
      arquivo.print(',');
      arquivo.print(horario);
      arquivo.print(',');
      arquivo.print(temperatura, 1);
      arquivo.print(',');
      arquivo.println(umidade, 1);
      arquivo.close();
    } else {
      Serial.println("Erro ao abrir o arquivo no SD");
    }
  }
}

// ---------- Ajuste manual pelo Monitor Serial ----------

void verificarAjusteDeHora() {
  if (!Serial.available()) return;

  String texto = Serial.readStringUntil('\n');
  texto.trim();

  int dia, mes, ano, hora, minuto, segundo;
  if (sscanf(texto.c_str(), "%d/%d/%d %d:%d:%d",
             &dia, &mes, &ano, &hora, &minuto, &segundo) == 6) {
    struct tm t = {};
    t.tm_mday = dia;
    t.tm_mon  = mes - 1;
    t.tm_year = ano - 1900;
    t.tm_hour = hora;
    t.tm_min  = minuto;
    t.tm_sec  = segundo;
    RTCTime novo(t);
    RTC.setTime(novo);
    horaAcertada = true;   // não precisa mais tentar pela internet

    char data[11], horario[9];
    lerDataHora(data, horario);
    Serial.print("Hora ajustada manualmente: ");
    Serial.print(data);
    Serial.print(" ");
    Serial.println(horario);
  } else {
    Serial.println("Formato invalido. Use: DD/MM/AAAA HH:MM:SS");
  }
}

void lerDataHora(char* data, char* horario) {
  RTCTime agora;
  RTC.getTime(agora);
  snprintf(data, 11, "%02d/%02d/%04d",
           agora.getDayOfMonth(), Month2int(agora.getMonth()), agora.getYear());
  snprintf(horario, 9, "%02d:%02d:%02d",
           agora.getHour(), agora.getMinutes(), agora.getSeconds());
}
