#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <espnow.h>
#include <DHTesp.h>
#include <Servo.h>

#define FAN_PIN D1
#define DHT_PIN D3
#define MQ2_PIN D8
#define PIR_SENSOR_PIN D6  // Pin for PIR sensor
#define FLAME_SENSOR_PIN D4
#define SERVO_PIN D5
#define FLAME_THRESHOLD 1000
#define MAGNETIC_SENSOR_PIN D2
#define BUZZER_PIN D7

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 25200; // Adjust for your timezone (GMT+7 for WIB is 25200 seconds)
const int DAYLIGHT_OFFSET_SEC = 0;

const char* WIFI_SSID = "MENDO4N";
const char* WIFI_PASS = "12345678";
const String SERVER_NAME = "http://192.168.144.120/smarthome/";
const char* MQTT_SERVER = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;
Servo servoMotor;

long lastMsg = 0;
char msg[100];
float temperature, humidity;
bool locked = true;
String doorstat = "close";
String lockerstat = "locked"; // Updated status variable

uint8_t esp32CamAddress[] = {0x4C, 0xEB, 0xD6, 0x1F, 0xA2, 0x7C}; // ESP32-CAM MAC address
// Declare motionDetected variable


void setup() {
  Serial.begin(9600);
  Serial.println("Hello, ESP8266!");

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
  servoMotor.write(0);  // Lock position
  locked = true;
  lockerstat = "locked";
  Serial.println("Locker: Locked");
}

void unlockServo() {
  servoMotor.write(90);  // Unlock position
  locked = false;
  lockerstat = "unlocked";
  Serial.println("Locker: Unlocked");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == "lockerStatus") {
    if (message == "lock") {
      lockServo();
    } else if (message == "unlock") {
      unlockServo();
    }
  }

  Serial.print("Message received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      client.subscribe("lockerStatus");
      client.subscribe("temperature&humidity");
      client.subscribe("securityStatus");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(1000);
    }
  }
}

void sendData(int temp, int hum, String status, String lockerstat, String doorstat) {
  HTTPClient http;
  String url = SERVER_NAME + "addhome.php?suhu=" + String(temp) + "&humid=" + String(hum) + "&security=" + status + "&locker=" + lockerstat + "&door=" + doorstat;
  Serial.println("URL: " + url);

  WiFiClient client;
  http.begin(client, url.c_str());
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
  Serial.print("Temperature: ");
  Serial.println(temp);
  Serial.print("Humidity: ");
  Serial.println(hum);
}

bool detectGas() {
  int gasValue = analogRead(MQ2_PIN);
  Serial.println("Gas sensor value: " + String(gasValue));
  if (gasValue > 300) {
    digitalWrite(BUZZER_PIN, HIGH);
    return true;
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    return false;
  }
}

bool detectFlame() {
  int flameValue = analogRead(FLAME_SENSOR_PIN);
  Serial.print("Flame sensor value: ");
  Serial.println(flameValue);
  if (flameValue < FLAME_THRESHOLD) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("Fire detected!");
    return true;
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    return false;
  }
}

bool detectmotion() {
 int sensorValue = digitalRead(PIR_SENSOR_PIN);
  Serial.print("PIR sensor value: ");
  Serial.println(sensorValue);  // Debug untuk melihat nilai sebenarnya
  if (sensorValue == HIGH) {
    Serial.println("Motion detected!");
    String message = "TAKE_PHOTO";
    esp_now_send(esp32CamAddress, (uint8_t *)message.c_str(), message.length());
    delay(5000); // Hindari trigger berulang
    return true;
  } else {
    return false;
  }

}

void checkDoorStatus() {
  int magneticState = digitalRead(MAGNETIC_SENSOR_PIN);
  if (magneticState == HIGH) {  // Door closed
    doorstat = "close";
  } else {  // Door open
    doorstat = "open";
    Serial.println("Door is open!");
  }
}

void loop() {
if (!client.connected()) {
    Serial.println("MQTT disconnected, reconnecting...");
    reconnect();  // Attempt to reconnect if disconnected
} else {
    client.loop();  // Only call loop if connected
}

  readDHT(temperature, humidity);

  if (temperature >= 27) {
    digitalWrite(FAN_PIN, HIGH);
  } else {
    digitalWrite(FAN_PIN, LOW);
  }

  checkDoorStatus();  // Check door status

  if (detectFlame()) {
    sendData(temperature, humidity, "flame", lockerstat, doorstat);
    client.publish("securityStatus", "flame");
  } else if (detectGas()) {
    sendData(temperature, humidity, "gas", lockerstat, doorstat);
    client.publish("securityStatus", "gas");
  } else if (detectmotion()) {
    sendData(temperature, humidity, "intruder", lockerstat, doorstat);
    client.publish("securityStatus", "intruder");
  }

  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
    snprintf(msg, 100, "Temp: %.2f, Humidity: %.2f", temperature, humidity);
    Serial.print("Publishing message: ");
    Serial.println(msg);
    client.publish("temperature&humidity", msg);
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    delay(1000);
    return;
  }

  int currentMinute = timeinfo.tm_min;
  int currentSecond = timeinfo.tm_sec;
  if (currentMinute == 0 && currentSecond == 0) {
    sendData(temperature, humidity, "safe", lockerstat, doorstat);
  }

  delay(1000);
}