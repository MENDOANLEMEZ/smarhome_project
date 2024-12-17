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
#define MQ2_PIN D4
#define PIR_SENSOR_PIN D2
#define FLAME_SENSOR_PIN D5
#define SERVO_PIN D6
#define FLAME_THRESHOLD 1000
#define MAGNETIC_SENSOR_PIN D7
#define BUZZER_PIN D3

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 25200;  // GMT+7
const int DAYLIGHT_OFFSET_SEC = 0;

const char* WIFI_SSID = "MENDO4N";
const char* WIFI_PASS = "12345678";
const String SERVER_NAME = "http://192.168.7.120/smarthome/";
const char* MQTT_SERVER = "test.mosquitto.org";

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;
Servo servoMotor;
MQ2 mq2(MQ2_PIN);

long lastMsg = 0;
char msg[100];
float temperature, humidity;
bool locked = true;
String doorstat = "close";
String lockerstat = "locked";
String fan_status = "off";
int nilaiGas = 0;
int batasGas = 1000;

uint8_t esp32CamAddress[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

void setup() {
  Serial.begin(9600);
  Serial.println("Hello, ESP8266!");

  mq2.begin(); // Kalibrasi sensor MQ2
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MAGNETIC_SENSOR_PIN, INPUT);
  pinMode(FAN_PIN, OUTPUT);
  servoMotor.attach(SERVO_PIN);
  pinMode(PIR_SENSOR_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.print("WiFi Connected at: ");
  Serial.println(WiFi.localIP());

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(esp32CamAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  lockServo();
}

void lockServo() {
  servoMotor.write(0);
  locked = true;
  lockerstat = "locked";
  Serial.println("Locker: Locked");
}

void unlockServo() {
  servoMotor.write(90);
  locked = false;
  lockerstat = "unlocked";
  Serial.println("Locker: Unlocked");
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("pesan diterima [");
  Serial.print(topic);
  Serial.print("]");
  
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == "lockerStatus") {
    if (message == "lock") {
      Serial.println("mengunci");
      lockServo();
    } else if (message == "unlock") {
      Serial.println("membuka");
      unlockServo();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      client.subscribe("lockerStatus");
      client.subscribe("temperature&humidity");
      client.subscribe("mq2");
      client.subscribe("securityStatus");
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.print("try again in 5 seconds");
      delay(5000);
    }
  }
}

void sendData(int temp, int hum, int gas, String status, String lockerstat, String doorstat, String fan_status) {
  HTTPClient http;
  String url = SERVER_NAME + "addhome.php?suhu=" + String(temp) + "&humid=" + String(hum) +
               "&gas=" + String(gas) + "&security=" + status + "&locker=" + lockerstat +
               "&door=" + doorstat + "&fan=" + fan_status;
  Serial.println("URL: " + url);

  http.begin(espClient, url.c_str());
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void readDHT(float &temp, float &hum) {
  TempAndHumidity data = dht.getTempAndHumidity();
  temp = data.temperature;
  hum = data.humidity;

  // Cetak ke Serial Monitor
  // Serial.print("Temp: ");
  // Serial.println(temp);
  // Serial.print("Hum: ");
  // Serial.println(hum);
}

bool detectGas() {
  float* gasValues = mq2.read(false);  // Membaca semua jenis gas
  
  if (gasValues == NULL) {  // Cek jika sensor gagal membaca
    Serial.println("Error membaca MQ2 sensor!");
    return false; 
  }

  nilaiGas = gasValues[1];  // Ambil nilai CO atau sesuaikan kebutuhan

  // Serial.print("Nilai Gas: ");
  // Serial.println(nilaiGas);

  if (nilaiGas > batasGas) {
    // Serial.println("Gas terdeteksi!");
    return true;  // Kembalikan true jika gas terdeteksi
  }

  return false;  // Jika nilai gas aman
}

bool detectFlame() {
  int flameValue = digitalRead(FLAME_SENSOR_PIN);  // Membaca nilai digital
  Serial.print("Flame sensor value: ");
  Serial.println(flameValue);

  if (flameValue == LOW) {  // Api terdeteksi (LOW bisa disesuaikan)
    Serial.println("Flame detected!");
    digitalWrite(BUZZER_PIN, HIGH);  // Aktifkan buzzer
    return true;  // Kembalikan true jika api terdeteksi
  } else {
    digitalWrite(BUZZER_PIN, LOW);  // Matikan buzzer
    return false;  // Tidak ada api
  }
}


bool detectMotion() {
  int sensorValue = digitalRead(PIR_SENSOR_PIN);
  if (lockerstat == "locked"){
    if (sensorValue == HIGH) {
      String message = "TAKE_PHOTO";
      esp_now_send(esp32CamAddress, (uint8_t *)message.c_str(), message.length());
      return true;
    }
  }
  return false;
}

void checkDoorStatus() {
  int magneticState = digitalRead(MAGNETIC_SENSOR_PIN);
  doorstat = (magneticState == HIGH) ? "close" : "open";
}

void checktime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int currentHour = timeinfo.tm_hour;     // Jam saat ini (0-23)
    int currentMinute = timeinfo.tm_min;   // Menit saat ini (0-59)
    int currentSecond = timeinfo.tm_sec;   // Detik saat ini (0-59)

    // Kirim data pada awal jam (menit dan detik = 0)
    if (currentMinute == 0 && currentSecond == 0) {
      sendData(temperature, humidity, nilaiGas, "safe", lockerstat, doorstat, fan_status);
      Serial.print("Data sent at hour: ");
      Serial.println(currentHour);
      delay(1000);  // Hindari pengiriman berulang di detik yang sama
    }
  } else {
    Serial.println("Failed to obtain time");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  readDHT(temperature, humidity);

  if (temperature >= 27) {
    digitalWrite(FAN_PIN, HIGH);
    fan_status = "on";
  } else {
    digitalWrite(FAN_PIN, LOW);
    fan_status = "off";
  }

  checkDoorStatus();

  if (detectFlame()) {
    client.publish("securityStatus", "flame");
    sendData(temperature, humidity, nilaiGas, "flame", lockerstat, doorstat, fan_status);
  } else if (detectGas()) {
    client.publish("securityStatus", "gas");
    // sendData(temperature, humidity, nilaiGas, "dangerGass", lockerstat, doorstat, fan_status);
  } else if (detectMotion()) {
    client.publish("securityStatus", "motion");
    sendData(temperature, humidity, nilaiGas, "Intruder", lockerstat, doorstat, fan_status);
  }
  checktime();
  long now = millis();
  if (now - lastMsg > 1000) {
    char messegdht[50]; 
    sprintf(messegdht, "Temp: %.2f Hum: %.2f", temperature, humidity); // Format suhu dan kelembapan dengan 2 desimal
    client.publish("temperature&humidity", messegdht);
    char gasValueStr[8]; 
    sprintf(gasValueStr, "%d", nilaiGas);  
    client.publish("mq2", gasValueStr);
  }
  delay(0);
}

