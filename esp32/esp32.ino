#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <espnow.h>
#include <DHTesp.h>
#include <Servo.h>
#include <MQ2.h>
#include <time.h>

#define FAN_PIN D0
#define DHT_PIN D3
#define MQ2_PIN A0
#define PIR_SENSOR_PIN D6
#define FLAME_SENSOR_PIN D5
#define SERVO_PIN D7
#define MAGNETIC_SENSOR_PIN D4
#define BUZZER_PIN D2

#define FLAME_THRESHOLD 1000
#define GAS_THRESHOLD 1000

const char* WIFI_SSID = "ICT_LAB";
const char* WIFI_PASS = "ICTLAB2023";

// const char* WIFI_SSID = "Aji_5G_Plus";
// const char* WIFI_PASS = "54175417";
const String SERVER_NAME = "http://192.168.1.63/smarthome/";
const char* MQTT_SERVER = "test.mosquitto.org";
const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 25200;  // GMT+7
const int DAYLIGHT_OFFSET_SEC = 0;

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;
Servo servoMotor;
MQ2 mq2(MQ2_PIN);

float temperature = 0.0, humidity = 0.0;
int gasValue = 0;
String doorStatus = "close";
String lockerStatus = "locked";
String fanStatus = "off";
bool locked = true;

uint8_t esp32CamAddress[] = {0xA0, 0xDD, 0x6C, 0xAF, 0xBD, 0x60};
String lastLockerMessage = "LOCK";

void setup() {
  Serial.begin(115200);
  // while (!Serial); // Tunggu sampai Serial Monitor siap (opsional, untuk beberapa board)
  // Serial.println("Program dimulai"); // Debug awal
  dht.setup(DHT_PIN, DHTesp::DHT22);
  mq2.begin();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MAGNETIC_SENSOR_PIN, INPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(PIR_SENSOR_PIN, INPUT);

  servoMotor.attach(SERVO_PIN);
  lockServo();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  Serial.print("Alamat MAC: ");
  Serial.println(WiFi.macAddress());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callbackDispatcher);

  setupEspNow(); // Memanggil fungsi untuk inisialisasi ESP-NOW
}

void setupEspNow() {
  // Pastikan mode WiFi diatur ke station (WIFI_STA)
  WiFi.mode(WIFI_STA);

  // Inisialisasi ESP-NOW
  if (esp_now_init() != 0) { // Jika inisialisasi gagal
    Serial.println("ESP-NOW initialization failed.");
    return;
  }

  Serial.println("ESP-NOW initialized successfully!");

  // Lanjutkan konfigurasi atau registrasi peer jika perlu
  esp_now_add_peer(esp32CamAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0); // Menambahkan peer ESP32-CAM
  esp_now_register_recv_cb(onEspNowReceive); // Callback untuk menerima data
}

void lockServo() {
  servoMotor.write(0);
  locked = true;
  lockerStatus = "locked";
}

void unlockServo() {
  servoMotor.write(180);
  locked = false;
  lockerStatus = "unlocked";
}

void callbackDispatcher(char* topic, byte* payload, unsigned int length) {
  String message = "";
  Serial.print("Received message on topic: ");
  Serial.println(topic);

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message: ");
  Serial.println(message);

  if (String(topic) == "lockerStatus") {
    handleLockerStatus(message);
  }
}

void handleLockerStatus(String message) {
  lastLockerMessage = message;
  Serial.println(lastLockerMessage);
  if (lastLockerMessage == "LOCK"){
    lockServo();
  }else if (lastLockerMessage == "UNLOCK"){
    unlockServo();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP8266Client")) {
      if (client.subscribe("lockerStatus")) {
        Serial.println("Successfully subscribed to lockerStatus");
      } else {
        Serial.println("Failed to subscribe to lockerStatus");
      }
      Serial.println("MQTT connected.");
    } else {
      Serial.println("Failed to connect to MQTT, retrying...");
      delay(5000);
    }
  }
}

void sendData(String secstat) {
  HTTPClient http;
  String url = SERVER_NAME + "addhome.php?suhu=" + String(temperature) +
               "&humid=" + String(humidity) + "&gas=" + String(gasValue) +
               "&security=" + secstat + "&locker=" + lockerStatus +
               "&door=" + doorStatus + "&fan=" + fanStatus;
  http.begin(espClient, url.c_str());
  int httpCode = http.GET();
  Serial.println("HTTP Response: " + String(httpCode));
  http.end();
}

void readSensors() {
  TempAndHumidity data = dht.getTempAndHumidity();
  temperature = data.temperature;
  humidity = data.humidity;

  gasValue = analogRead(MQ2_PIN);
  digitalWrite(BUZZER_PIN, gasValue > GAS_THRESHOLD ? HIGH : LOW);
}

void checkDoorStatus() {
  int magneticState = digitalRead(MAGNETIC_SENSOR_PIN);
  doorStatus = (magneticState == HIGH) ? "open" : "close";
}

bool isAllowedHour() {
  time_t now = time(nullptr);
  struct tm* timeInfo = localtime(&now);
  int hour = timeInfo->tm_hour;
  return hour >= 1 && hour <= 23 || hour == 0;
}

void onEspNowReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  Serial.print("Data received: ");
  for (int i = 0; i < len; i++) {
    Serial.print((char)data[i]);
  }
  Serial.println();
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  static long previousMillis = 0;
  long currentMillis = millis();

  if (currentMillis - previousMillis > 1000) {
    previousMillis = currentMillis;

    readSensors();
    checkDoorStatus();

    if (temperature > 27) {
      digitalWrite(FAN_PIN, HIGH);
      fanStatus = "on";
    } else {
      digitalWrite(FAN_PIN, LOW);
      fanStatus = "off";
    }

    if (lastLockerMessage == "LOCK") {
      lockServo();
    } else if (lastLockerMessage == "UNLOCK") {
      unlockServo();
    }

    if (gasValue > GAS_THRESHOLD) {
      client.publish("securityStatus", "Gas Detected!");
      sendData("Gas Detected");
    }

    if (digitalRead(FLAME_SENSOR_PIN) == LOW) {
      client.publish("securityStatus", "Flame Detected!");
      sendData("Flame Detected");
    }

    if (digitalRead(PIR_SENSOR_PIN) == HIGH && lockerStatus == "locked") {
      Serial.println("Intruder detected!");
      digitalWrite(BUZZER_PIN, HIGH);
      client.publish("securityStatus", "Intruder Detected!");

      String message = "TAKE_PHOTO";
      int result = esp_now_send(esp32CamAddress, (uint8_t*)message.c_str(), message.length());
      if (result == 0) {
          Serial.println("Message sent successfully");
      } else {
          Serial.println("Error sending the message");
          Serial.println(result);
      }
      sendData("Intruder detected");
    }

    client.publish("temperature&humidity", ("Temp: " + String(temperature) + "C, Hum: " + String(humidity) + "%").c_str());
    client.publish("mq2", String(gasValue).c_str());
    client.publish("doorStatus",String(doorStatus).c_str());
    client.publish("fanStatus",String(fanStatus).c_str());
  }

  static long lastHTTP = 0;
  if (currentMillis - lastHTTP > 60000 && isAllowedHour()) {
    lastHTTP = currentMillis;
    sendData("safe");
  }
}
