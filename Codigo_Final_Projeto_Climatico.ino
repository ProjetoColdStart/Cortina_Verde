/*
  PROJETO CLIMATICO
  Arduino UNO R4 WiFi
  Sensor DHT22
  Modulo MicroSD HW-125

  Funcoes:
  - Medir temperatura e umidade
  - Obter data e horario via internet (NTP)
  - Manter horario pelo RTC interno
  - Gravar os dados em um arquivo CSV POR DIA (AAAAMMDD.CSV)
  - Realizar uma medicao a cada minuto

  DHT22:
  VCC  -> 5V
  DATA -> D2
  GND  -> GND

  MICROSD:
  VCC  -> 5V (confirmar compatibilidade do modulo)
  GND  -> GND
  MISO -> D12
  MOSI -> D11
  SCK  -> D13
  CS   -> D10
*/

#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include "RTC.h"
#include <time.h>

// ==========================================
// CONFIGURACOES
// ==========================================

// Preencha com os dados da sua rede Wi-Fi
const char NOME_WIFI[] = "REDMI Note 15 5G";
const char SENHA_WIFI[] = "22222222";

// Fuso horario de Brasilia
const long FUSO_HORARIO_S = -3L * 3600;

// TESTE: uma medicao a cada minuto
const unsigned long INTERVALO_MS = 3600000UL;

// Intervalo para tentar sincronizar novamente
const unsigned long TENTAR_DE_NOVO_MS = 3600000UL;

// Pinos
#define PINO_DHT 2
#define TIPO_DHT DHT22
#define PINO_CS_SD 10

// ==========================================
// OBJETOS E VARIAVEIS
// ==========================================

DHT dht(PINO_DHT, TIPO_DHT);

WiFiUDP udp;

bool sdOk = false;
bool horaAcertada = false;

// Nome do arquivo do dia (formato AAAAMMDD.CSV)
char nomeArquivo[13];

unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaHora = 0;

// ==========================================
// CONFIGURACAO INICIAL
// ==========================================

void setup() {

  Serial.begin(9600);
  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println("   PROJETO CLIMATICO");
  Serial.println("================================");

  // Inicia sensor
  dht.begin();

  // Inicia relogio interno
  RTC.begin();

  // Define uma data inicial de seguranca
  if (!RTC.isRunning()) {

    RTCTime inicial(
      1,
      Month::JANUARY,
      2026,
      0,
      0,
      0,
      DayOfWeek::THURSDAY,
      SaveLight::SAVING_TIME_INACTIVE
    );

    RTC.setTime(inicial);
  }

  // Tenta acertar a hora pela internet
  horaAcertada = acertarHoraPelaInternet();

  ultimaTentativaHora = millis();

  // Inicializa cartao SD
  Serial.println();
  Serial.print("Iniciando cartao SD... ");

  if (SD.begin(PINO_CS_SD)) {

    sdOk = true;
    Serial.println("OK!");

  } else {

    Serial.println("FALHA!");
    Serial.println("Verifique o cartao e as ligacoes.");
  }

  Serial.println();
  Serial.println("Sistema pronto!");
  Serial.println("Intervalo: 1 minuto");

  Serial.println(
    "Aguardando primeira medicao..."
  );

  Serial.println();

  delay(2000);
}

// ==========================================
// LOOP PRINCIPAL
// ==========================================

void loop() {

  // Permite ajuste manual do horario
  verificarAjusteDeHora();

  // Tenta sincronizar novamente se necessario
  if (
    !horaAcertada &&
    millis() - ultimaTentativaHora >= TENTAR_DE_NOVO_MS
  ) {

    Serial.println("Tentando sincronizar horario...");

    horaAcertada = acertarHoraPelaInternet();

    ultimaTentativaHora = millis();
  }

  // Realiza a primeira leitura imediatamente
  // e depois a cada minuto

  if (
    ultimaLeitura == 0 ||
    millis() - ultimaLeitura >= INTERVALO_MS
  ) {

    ultimaLeitura = millis();

    registrarLeitura();
  }
}

// ==========================================
// SINCRONIZACAO DE HORARIO VIA INTERNET
// ==========================================

bool acertarHoraPelaInternet() {

  Serial.print("Conectando ao Wi-Fi... ");

  WiFi.begin(NOME_WIFI, SENHA_WIFI);

  unsigned long inicio = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < 20000UL
  ) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("Falha na conexao Wi-Fi.");

    WiFi.end();

    return false;
  }

  Serial.println("Wi-Fi conectado!");

  delay(1000);

  unsigned long epoch = 0;

  for (
    int tentativa = 0;
    tentativa < 5 && epoch == 0;
    tentativa++
  ) {

    epoch = pedirHoraNTP();
  }

  // Desliga Wi-Fi apos a tentativa
  WiFi.end();

  if (epoch == 0) {

    Serial.println("Servidor NTP nao respondeu.");

    return false;
  }

  // Converte UTC para horario de Brasilia
  RTCTime agora(
    (time_t)(epoch + FUSO_HORARIO_S)
  );

  RTC.setTime(agora);

  char data[11];
  char horario[9];

  lerDataHora(data, horario);

  Serial.print("Horario sincronizado: ");
  Serial.print(data);
  Serial.print(" ");
  Serial.println(horario);

  return true;
}

// ==========================================
// SOLICITACAO AO SERVIDOR NTP
// ==========================================

unsigned long pedirHoraNTP() {

  const int TAMANHO = 48;

  byte pacote[TAMANHO] = {0};

  // Solicitacao NTP: versao 4, modo cliente
  pacote[0] = 0x23;

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

  while (millis() - inicio < 3000UL) {

    int tamanhoRecebido = udp.parsePacket();

    if (tamanhoRecebido >= TAMANHO) {

      udp.read(pacote, TAMANHO);

      udp.stop();

      unsigned long segundos1900 =
        ((unsigned long)pacote[40] << 24) |
        ((unsigned long)pacote[41] << 16) |
        ((unsigned long)pacote[42] << 8) |
        (unsigned long)pacote[43];

      // Converte data NTP para Unix
      return segundos1900 - 2208988800UL;
    }

    delay(50);
  }

  udp.stop();

  return 0;
}

// ==========================================
// NOME DO ARQUIVO DO DIA
// ==========================================

void definirNomeArquivo() {

  if (horaAcertada) {

    RTCTime agora;
    RTC.getTime(agora);

    snprintf(
      nomeArquivo,
      sizeof(nomeArquivo),
      "%04d%02d%02d.CSV",
      agora.getYear(),
      Month2int(agora.getMonth()),
      agora.getDayOfMonth()
    );

  } else {

    // Sem horario sincronizado
    snprintf(nomeArquivo, sizeof(nomeArquivo), "SEM_DATA.CSV");
  }
}

bool prepararArquivoDoDia() {

  definirNomeArquivo();

  if (SD.exists(nomeArquivo)) {
    return true;
  }

  File arquivo = SD.open(nomeArquivo, FILE_WRITE);

  if (!arquivo) {
    return false;
  }

  arquivo.println("data,horario,temperatura_C,umidade_%");
  arquivo.close();

  Serial.print("Novo arquivo criado: ");
  Serial.println(nomeArquivo);

  return true;
}

// ==========================================
// LEITURA E GRAVACAO DO SENSOR
// ==========================================

void registrarLeitura() {

  // Realiza leitura do DHT22
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  // Verifica se o sensor realizou a leitura corretamente
  if (isnan(temperatura) || isnan(umidade)) {

    Serial.println("ERRO: Falha na leitura DHT22!");

    return;
  }

  // Prepara os campos de data e horario
  char data[11];
  char horario[9];

  // Se o horario estiver sincronizado, utiliza o RTC
  if (horaAcertada) {

    lerDataHora(data, horario);

  } else {

    // Se nao houver Wi-Fi, registra sem data e horario
    snprintf(data, sizeof(data), "SEM_DATA");
    snprintf(horario, sizeof(horario), "SEM_HORA");

    Serial.println("AVISO: Sem horario sincronizado.");
  }

  // Mostra os resultados no Monitor Serial
  Serial.println("--------------------------------");

  Serial.print("Data: ");
  Serial.println(data);

  Serial.print("Horario: ");
  Serial.println(horario);

  Serial.print("Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.println(" C");

  Serial.print("Umidade: ");
  Serial.print(umidade, 1);
  Serial.println(" %");

  // Verifica se o cartao SD esta disponivel
  if (!sdOk) {

    Serial.println("ERRO: Cartao SD indisponivel.");

    return;
  }

  // Garante que o arquivo do dia existe (cria se for um dia novo)
  if (!prepararArquivoDoDia()) {

    Serial.println("ERRO: Nao foi possivel criar o arquivo do dia!");
    return;
  }

  File arquivo = SD.open(nomeArquivo, FILE_WRITE);

  if (arquivo) {

    arquivo.print(data);
    arquivo.print(",");

    arquivo.print(horario);
    arquivo.print(",");

    arquivo.print(temperatura, 1);
    arquivo.print(",");

    arquivo.println(umidade, 1);

    // Fecha o arquivo para salvar os dados
    arquivo.close();

    Serial.print("DADOS GRAVADOS EM: ");
    Serial.println(nomeArquivo);

  } else {

    Serial.println("ERRO: Nao foi possivel abrir o arquivo CSV!");
  }

  Serial.println("--------------------------------");
}

// ==========================================
// AJUSTE MANUAL DO HORARIO
// ==========================================

void verificarAjusteDeHora() {

  if (!Serial.available()) {
    return;
  }

  String texto = Serial.readStringUntil('\n');

  texto.trim();

  int dia, mes, ano;
  int hora, minuto, segundo;

  if (
    sscanf(
      texto.c_str(),
      "%d/%d/%d %d:%d:%d",
      &dia,
      &mes,
      &ano,
      &hora,
      &minuto,
      &segundo
    ) == 6
  ) {

    // Validacao basica dos campos
    if (
      ano < 2020 || ano > 2099 ||
      mes < 1 || mes > 12 ||
      dia < 1 || dia > 31 ||
      hora < 0 || hora > 23 ||
      minuto < 0 || minuto > 59 ||
      segundo < 0 || segundo > 59
    ) {

      Serial.println("Data ou horario fora do intervalo.");

      return;
    }

    // Validacao da quantidade de dias no mes
    const int diasMes[] = {
      31, 28, 31, 30, 31, 30,
      31, 31, 30, 31, 30, 31
    };

    bool bissexto =
      (ano % 4 == 0 && ano % 100 != 0) ||
      (ano % 400 == 0);

    int limiteDias = diasMes[mes - 1];

    if (mes == 2 && bissexto) {
      limiteDias = 29;
    }

    if (dia > limiteDias) {

      Serial.println("Dia invalido para este mes.");

      return;
    }

    struct tm t = {};

    t.tm_mday = dia;
    t.tm_mon = mes - 1;
    t.tm_year = ano - 1900;

    t.tm_hour = hora;
    t.tm_min = minuto;
    t.tm_sec = segundo;

    RTCTime novo(t);

    RTC.setTime(novo);

    horaAcertada = true;

    char data[11];
    char horario[9];

    lerDataHora(data, horario);

    Serial.print("Horario ajustado manualmente: ");

    Serial.print(data);
    Serial.print(" ");
    Serial.println(horario);

  } else {

    Serial.println("Formato invalido!");

    Serial.println(
      "Use: DD/MM/AAAA HH:MM:SS"
    );
  }
}

// ==========================================
// LEITURA DA DATA E DO HORARIO DO RTC
// ==========================================

void lerDataHora(
  char* data,
  char* horario
) {

  RTCTime agora;

  RTC.getTime(agora);

  snprintf(
    data,
    11,
    "%02d/%02d/%04d",
    agora.getDayOfMonth(),
    Month2int(agora.getMonth()),
    agora.getYear()
  );

  snprintf(
    horario,
    9,
    "%02d:%02d:%02d",
    agora.getHour(),
    agora.getMinutes(),
    agora.getSeconds()
  );
}