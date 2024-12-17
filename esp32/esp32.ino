#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <espnow.h>
#include <DHTesp.h>
#include <Servo.h>
#include <MQ2.h>
#include <time.h>

#define FAN_PIN D8
#define DHT_PIN D1
#define MQ2_PIN A0        // Menggunakan pin analog untuk MQ2
#define PIR_SENSOR_PIN D8
#define FLAME_SENSOR_PIN D5
#define SERVO_PIN D6
#define MAGNETIC_SENSOR_PIN D7
#define BUZZER_PIN D3

#define FLAME_THRESHOLD 1000
#define GAS_THRESHOLD 1000

const char* WIFI_SSID = "MENDO4N";
const char* WIFI_PASS = "12345678";
const String SERVER_NAME = "http://192.168.7.120/smarthome/";
const char* MQTT_SERVER = "test.mosquitto.org";
const char* NTP_SERVER = "pool.ntp.org";

const long GMT_OFFSET_SEC = 25200;  // GMT+7
const int DAYLIGHT_OFFSET_SEC = 0;

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;
Servo servoMotor;
MQ2 mq2(MQ2_PIN);

long lastMsg = 0;
char msg[100];
float temperature = 0.0, humidity = 0.0;
int gasValue = 0;

String doorStatus = "close";
String lockerStatus = "locked";
String fanStatus = "off";
bool locked = true;

uint8_t esp32CamAddress[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

void setup() {
  Serial.begin(115200);
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

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(esp32CamAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void lockServo() {
  servoMotor.write(0);
  locked = true;
  lockerStatus = "locked";
}

void unlockServo() {
  servoMotor.write(90);
  locked = false;
  lockerStatus = "unlocked";
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];

  if (String(topic) == "lockerStatus") {
    if (message == "lock") lockServo();
    else if (message == "unlock") unlockServo();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP8266Client")) {
      client.subscribe("lockerStatus");
      Serial.println("MQTT connected.");
    } else {
      delay(5000);
    }
  }
}

void sendData() {
  HTTPClient http;
  String url = SERVER_NAME + "addhome.php?suhu=" + String(temperature) +
               "&humid=" + String(humidity) + "&gas=" + String(gasValue) +
               "&security=" + "safe" + "&locker=" + lockerStatus +
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
  if (gasValue > GAS_THRESHOLD) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void sendDataToServer(String securityStatus) {
  HTTPClient http;
  String url = SERVER_NAME + "addhome.php?suhu=" + String(temperature) +
               "&humid=" + String(humidity) + "&gas=" + String(gasValue) +
               "&security=" + securityStatus + "&locker=" + lockerStatus +
               "&door=" + doorStatus + "&fan=" + fanStatus;

  http.begin(espClient, url.c_str());
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.println("HTTP Response: " + String(httpCode));
  } else {
    Serial.println("Error sending data to server!");
  }
  http.end();
}

void checkDoorStatus() {
  int magneticState = digitalRead(MAGNETIC_SENSOR_PIN);
  doorStatus = (magneticState == HIGH) ? "close" : "open";
}

void loop() {
   if (!client.connected()) reconnectMQTT();
  client.loop();

  static long previousMillis = 0;
  long currentMillis = millis();

  // Bacaan sensor setiap detik
  if (currentMillis - previousMillis > 1000) {
    previousMillis = currentMillis;

    readSensors();
    checkDoorStatus();

    // Kipas aktif jika suhu > 27
    if (temperature > 27) {
      digitalWrite(FAN_PIN, HIGH);
      fanStatus = "on";
    } else {
      digitalWrite(FAN_PIN, LOW);
      fanStatus = "off";
    }

    // Deteksi Gas
    if (gasValue > GAS_THRESHOLD) {
      client.publish("securityStatus", "Gas Detected!");
      sendDataToServer("Gas Detected");
    }

    // Deteksi Api
    if (digitalRead(FLAME_SENSOR_PIN) == LOW) {
      client.publish("securityStatus", "Flame Detected!");
      sendDataToServer("Flame Detected");
    }

    // Deteksi Penyusup
    long state = digitalRead(PIR_SENSOR_PIN);
    if (state == HIGH) {
      Serial.println("Intruder detected!");
      client.publish("securityStatus", "Intruder Detected!");

      // Kirim perintah ke ESP32-CAM untuk ambil foto
      String message = "TAKE_PHOTO";
      esp_now_send(esp32CamAddress, (uint8_t*)message.c_str(), message.length());

      // Kirim status ke server
      sendDataToServer("Intruder Detected");
    }

    // Kirim suhu dan kelembapan setiap detik
    client.publish("temperature&humidity", ("Temp: " + String(temperature) + "C, Hum: " + String(humidity) + "%").c_str());
    client.publish("mq2", String(gasValue).c_str());
  }

  // Kirim data HTTP setiap 1 menit
  static long lastHTTP = 0;
  if (currentMillis - lastHTTP > 60000) {
    lastHTTP = currentMillis;
    sendData();
  }
}

