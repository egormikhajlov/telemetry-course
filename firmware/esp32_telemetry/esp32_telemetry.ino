#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ================= НАСТРОЙКИ WiFi и MQTT =================
const char* WIFI_SSID = "iPhone(Егор)";
const char* WIFI_PASSWORD = "pipise4ka";
const char* MQTT_SERVER = "172.20.10.14"; 
const int MQTT_PORT = 1883;

// ================= ПИНЫ ESP32 =================
const int TEMP_PIN = 35;      // Термистор (через делитель 10кОм)
const int LED_PIN = 2;        // Встроенный LED (индикация)
const int FAN_PIN = 26;       // Вентилятор (сейчас не используется)

// ================= ПАРАМЕТРЫ =================
const float TEMP_THRESHOLD = 30.0;  // Порог срабатывания (°C)

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
bool fanState = false;

// ================= ЧТЕНИЕ ТЕМПЕРАТУРЫ =================
float readTemperature() {
  const int samples = 5;
  float sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(TEMP_PIN);
    delay(10);
  }
  float adc = sum / samples; // Усредняем шум
  
  float voltage = adc * (3.3 / 4095.0);
  float resistance = 10000.0 * (voltage / (3.3 - voltage));
  float temp = 1.0 / (log(resistance / 100000.0) / 3950.0 + 1.0 / 298.15) - 273.15;
  return temp;
}

// ================= ПОДКЛЮЧЕНИЕ К WiFi =================
void setup_wifi() {
  Serial.println();
  Serial.print(" WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✅ WiFi подключен");
  Serial.print(" IP: ");
  Serial.println(WiFi.localIP());
}

// ================= ОБРАБОТКА MQTT СООБЩЕНИЙ =================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  // Управление через MQTT (опционально)
  if (String(topic) == "telemetry/control") {
    if (msg == "fan:on") {
      digitalWrite(FAN_PIN, HIGH);
      fanState = true;
      Serial.println(" Вентилятор ВКЛ (MQTT) + LED загорелся");
    } else if (msg == "fan:off") {
      digitalWrite(FAN_PIN, LOW);
      fanState = false;
      Serial.println(" Вентилятор ВЫКЛ (MQTT) + LED выключен");
    }
  }
}

// ================= ПЕРЕПОДКЛЮЧЕНИЕ К MQTT =================
void reconnect() {
  while (!client.connected()) {
    Serial.print(" MQTT...");
    
    if (client.connect("ESP32-Hotend-01")) {
      Serial.println("✅");
      client.subscribe("telemetry/control");
    } else {
      Serial.print("rc=");
      Serial.print(client.state());
      Serial.println(" (3s)");
      delay(3000);
    }
  }
}

// ================= ОТПРАВКА ДАННЫХ НА СЕРВЕР =================
void sendTelemetry(float temp) {
  StaticJsonDocument<256> doc;
  
  doc["device_id"] = "ESP32-Hotend-01";
  doc["temperature_c"] = temp;
  doc["fan_state"] = fanState;
  doc["threshold"] = TEMP_THRESHOLD;
  doc["timestamp"] = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  if (client.publish("telemetry/temperature", buffer)) {
    Serial.print(" ");
    Serial.println(buffer);
  }
}

// ================= ИНИЦИАЛИЗАЦИЯ =================
void setup() {
  Serial.begin(115200);
  
  // Настраиваем пины
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  
  // Настройка ADC (12 бит, полное напряжение 3.3V)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  Serial.println("\n=== СИСТЕМА ТЕЛЕМЕТРИИ ХОТЭНДА ===");
  Serial.println(" Термистор: GPIO 35");
  Serial.println(" LED: GPIO 2");
  Serial.println(" Вентилятор");
  Serial.println("==============================\n");
  
  setup_wifi();
  
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  delay(2000);
  Serial.println("✅ Система готова!\n");
}

// ================= ГЛАВНЫЙ ЦИКЛ =================
void loop() {
  // Проверяем подключение к MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // Читаем температуру
  float temp = readTemperature();
  
  // Автоматическое управление (гистерезис 2°C)
  if (temp > TEMP_THRESHOLD && !fanState) {
    digitalWrite(LED_PIN, HIGH);
    fanState = true;
    Serial.println(" ТЕМПЕРАТУРА ВЫШЕ ПОРОГА! LED ВКЛ");
  } 
  else if (temp < (TEMP_THRESHOLD - 2.0) && fanState) {
    digitalWrite(LED_PIN, LOW);
    fanState = false;
    Serial.println(" Температура упала. LED ВЫКЛ");
  }
  
  // Отправка данных каждые 3 секунды
  unsigned long now = millis();
  if (now - lastMsg > 3000) {
    lastMsg = now;
    
    Serial.printf(" %.1f°C | LED: %s\n", temp, fanState ? "ON" : "OFF");
    sendTelemetry(temp);
  }
  
  delay(100);
}