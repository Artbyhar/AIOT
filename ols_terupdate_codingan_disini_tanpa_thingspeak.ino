// === Library ===
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// === WiFi & MQTT ===
const char* ssid = "xxxxx";
const char* password = "xxxxxx";
const char* mqtt_server = "test.mosquitto.org"; 

// === Supabase ===
const char* SUPABASE_PROJECT_URL = "https://xxxxxx";
const char* SUPABASE_API_KEY = "xxxxxxxxxx";  

WiFiClient espClient;
PubSubClient client(espClient);

// === Pin ===
const int relayH1 = 27;
const int relayH2 = 26;
const int sensorH1 = 36;
const int sensorH2 = 39;
const int mq135Pin = 35;

#define DHTPIN 32
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// === Kalibrasi Sensor ===
const int threshold = 30;
const int nilaiKering_H1 = 4095;
const int nilaiBasah_H1  = 1700;
const int nilaiKering_H2 = 4095;
const int nilaiBasah_H2  = 1700;

// === Averaging Buffer ===
const size_t windowSize = 30;
uint16_t bufH1[windowSize] = {0};
uint16_t bufH2[windowSize] = {0};
unsigned long totalH1 = 0;
unsigned long totalH2 = 0;
size_t idx = 0;

// === Variabel Kontrol ===
int control1 = 0; 
int control2 = 0;
float kelembapanH2_SFA = -1;
unsigned long lastPumpTime = 0;

// === OLED ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === Setup WiFi ===
void setup_wifi() {
  Serial.println("Menghubungkan ke WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung");
  Serial.printf("IP ESP32: %s\n", WiFi.localIP().toString().c_str());
}

// === Callback MQTT ===
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String message = String((char*)payload);

  if (String(topic) == "kuliah_aiot_fsm_uksw") {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
      Serial.println("Gagal parsing JSON dari MQTT");
      return;
    }

    if (doc.containsKey("Kelembaban_tanah_Pot_2")) {
      kelembapanH2_SFA = doc["Kelembaban_tanah_Pot_2"];
      Serial.printf(">> Terima dari SF_A: H2 = %.1f%%\n", kelembapanH2_SFA);
    }
  }
}

// === MQTT Reconnect ===
void reconnect() {
  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi belum tersambung. Coba ulang WiFi...");
      setup_wifi();
    }

    Serial.print("Menghubungkan ke MQTT broker... ");
    String clientId = "SF_B_Client-" + WiFi.macAddress();

    if (client.connect(clientId.c_str())) {
      Serial.println("BERHASIL!");
      client.subscribe("kuliah_aiot_fsm_uksw");
      client.subscribe("SF_B/control/H2"); // optional topic tambahan
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" – coba lagi 5 detik");
      delay(10000);
    }
  }
}

// === Setup Awal ===
void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  pinMode(relayH1, OUTPUT);
  pinMode(relayH2, OUTPUT);
  digitalWrite(relayH1, HIGH);
  digitalWrite(relayH2, HIGH);

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED tidak ditemukan"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SF_B Monitoring...");
  display.display();
  delay(2000);

  for (size_t i = 0; i < windowSize; i++) {
    bufH1[i] = analogRead(sensorH1);
    totalH1 += bufH1[i];
    bufH2[i] = analogRead(sensorH2);
    totalH2 += bufH2[i];
  }
}

// === LOOP ===
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  uint16_t valH1 = analogRead(sensorH1);
  uint16_t valH2 = analogRead(sensorH2);

  totalH1 = totalH1 - bufH1[idx] + valH1;
  bufH1[idx] = valH1;
  totalH2 = totalH2 - bufH2[idx] + valH2;
  bufH2[idx] = valH2;
  idx = (idx + 1) % windowSize;

  float avgH1 = totalH1 / (float)windowSize;
  float avgH2 = totalH2 / (float)windowSize;

  float kelembapanH1 = constrain(map(avgH1, nilaiKering_H1, nilaiBasah_H1, 0, 100), 0, 100);
  float kelembapanH2 = constrain(map(avgH2, nilaiKering_H2, nilaiBasah_H2, 0, 100), 0, 100);

  float adcH1 = avgH1;
  float adcH2 = avgH2;

  float T = map(analogRead(mq135Pin), 0, 4095, 0, 100);
  float prediksiB2 = 0.85 * kelembapanH1 + 5.0;
  float persenAir = -1;

  Serial.println("========= DATA SF_B =========");
  Serial.printf("ADC H1 = %.0f | Kelembapan H1 = %.1f%%\n", adcH1, kelembapanH1);
  Serial.printf("ADC H2 = %.0f | Kelembapan H2 = %.1f%%\n", adcH2, kelembapanH2);
  Serial.printf("Referensi H2 dari SF_A = %.1f%%\n", kelembapanH2_SFA);
  Serial.printf("Gas (MQ135) = %.1f\n", T);
  Serial.println("================================");

  unsigned long now = millis();
  if (kelembapanH1 < threshold && now - lastPumpTime > 30000) {
    Serial.println("Pompa Aktif! Tanah kering.");
    digitalWrite(relayH1, LOW);
    delay(3000);
    digitalWrite(relayH1, HIGH);
    lastPumpTime = now;
    control1 = 1;
  } else {
    digitalWrite(relayH1, HIGH);
    control1 = 0;
  }

  control2 = (kelembapanH2 < threshold) ? 1 : 0;

  // === Kirim MQTT ===
  String jsonMQTT = "{";
  jsonMQTT += "\"NomorTim\":1,";
  jsonMQTT += "\"SmartFarmingGroup\":\"SF_B\",";
  jsonMQTT += "\"Kelembaban_tanah_Pot_1\":" + String(kelembapanH1, 1) + ",";
  jsonMQTT += "\"Kelembaban_tanah_Pot_2\":" + String(kelembapanH2, 1) + ",";
  jsonMQTT += "\"Aktuator_1\":" + String(control1) + ",";
  jsonMQTT += "\"Aktuator_2\":" + String(control2) + ",";
  jsonMQTT += "\"Status_Kran_1\":" + String(control1) + ",";
  jsonMQTT += "\"Status_Kran_2\":" + String(control2);
  jsonMQTT += "}";
  client.publish("kuliah_aiot_fsm_uksw", jsonMQTT.c_str());

  // === Kirim Supabase ===
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(SUPABASE_PROJECT_URL) + "/rest/v1/sensor_sf_b";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", SUPABASE_API_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_API_KEY));

    String payload = "{";
    payload += "\"grup\":\"SF_B\",";
    payload += "\"node\":\"SF_B\",";
    payload += "\"t\":" + String(T, 1) + ",";
    payload += "\"h1\":" + String(kelembapanH1, 1) + ",";
    payload += "\"h2\":" + String(kelembapanH2, 1) + ",";
    payload += "\"adc_h1\":" + String(adcH1, 0) + ",";
    payload += "\"adc_h2\":" + String(adcH2, 0) + ",";
    payload += "\"prediksi_b2\":" + String(prediksiB2, 1) + ",";
    payload += "\"persen_air\":" + String(persenAir, 1) + ",";
    payload += "\"control1\":" + String(control1) + ",";
    payload += "\"control2\":" + String(control2) + ",";
    payload += "\"h2_sfa\":" + String(kelembapanH2_SFA, 1);
    payload += "}";
    int code = http.POST(payload);
    Serial.printf("[Supabase] Status: %d\n", code);
    http.end();
  }

  // === OLED Tampilan ===
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("H1: %.1f%% (%d)", kelembapanH1, (int)adcH1);
  display.setCursor(0, 16);
  display.printf("H2: %.1f%% (%d)", kelembapanH2, (int)adcH2);
  display.setCursor(0, 32);
  display.printf("H2 SF_A: %.1f%%", kelembapanH2_SFA);
  display.display();

  delay(10000);
}
