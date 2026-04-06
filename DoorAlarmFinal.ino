/*
==========================================================
 ESP32 DOOR MONITOR - ADVANCED v2.1
==========================================================

FEATURES:
✔ Door detection (test mode: button logic)
✔ MQTT (clean topic structure)
✔ Presence automation (home / away)
✔ Telegram alerts
✔ Arduino OTA (IDE upload)
✔ HTTP OTA (GitHub update)
✔ MQTT-triggered OTA
✔ Heartbeat + version reporting

TOPICS:
home/door/state
home/door/heartbeat
home/door/presence
home/door/update
home/door/version

==========================================================
*/

// ===== LIBRARIES =====
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#include "credentials.h"

// ===== HARDWARE =====
#define DOOR_PIN 0   // test mode (button)
#define FW_VERSION "2.1.0"

// ===== OBJECTS =====
WiFiClientSecure espClient;
PubSubClient client(espClient);
WiFiClientSecure telegramClient;

// ===== SYSTEM STATE =====
bool doorOpen = false;
bool lastDoorState = false;
bool systemArmed = false;

// ===== TIMING =====
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 100;

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000;

// ===== MQTT CONNECT =====
void connectMQTT() {
  static unsigned long lastAttempt = 0;

  if (millis() - lastAttempt < 2000) return;
  lastAttempt = millis();

  espClient.setInsecure();

  if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
    Serial.println("MQTT connected");

    client.subscribe("home/door/presence");
    client.subscribe("home/door/update");

    // Send version at connect
  String payload = String("{\"version\":\"") + FW_VERSION + "\"}";
  client.publish("home/door/version", payload.c_str(), true);

  } else {
    Serial.println("MQTT failed");
  }
}

// ===== TELEGRAM =====
void sendTelegram(String message) {
  telegramClient.setInsecure();

  if (!telegramClient.connect("api.telegram.org", 443)) return;

  String url = "/bot" + String(BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(CHAT_ID) +
               "&text=" + message;

  telegramClient.print(String("GET ") + url + " HTTP/1.1\r\n" +
                       "Host: api.telegram.org\r\n"
                       "Connection: close\r\n\r\n");

  telegramClient.stop();
}

// ===== HTTP OTA =====
String getRemoteVersion() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.begin(client, versionURL);

  if (https.GET() == 200) {
    String v = https.getString();
    v.trim();
    return v;
  }
  return "";
}

void performUpdate() {
  WiFiClientSecure client;
  client.setInsecure();

  sendTelegram("⬆️ Updating firmware...");

  t_httpUpdate_return ret = httpUpdate.update(client, firmwareURL);

  if (ret == HTTP_UPDATE_OK) {
    Serial.println("Update OK");
  } else {
    Serial.println("Update failed");
    sendTelegram("❌ Update failed");
  }
}

void checkForUpdate() {
  String remote = getRemoteVersion();

  if (remote.length() == 0) return;

  Serial.printf("Local: %s | Remote: %s\n", FW_VERSION, remote.c_str());

  if (remote != FW_VERSION) {
    performUpdate();
  }
}

// ===== MQTT CALLBACK =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  String msg;
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("MQTT: " + msg);

  // ===== PRESENCE =====
  if (String(topic) == "home/door/presence") {

    if (msg.indexOf("home") >= 0) {
      systemArmed = false;
      Serial.println("System DISARMED");
      sendTelegram("🏠 System OFF (home)");
    }

    if (msg.indexOf("away") >= 0) {
      systemArmed = true;
      Serial.println("System ARMED");
      sendTelegram("🚨 System ON (away)");
    }
  }

  // ===== OTA TRIGGER =====
  if (String(topic) == "home/door/update") {

    if (msg.indexOf("start") >= 0) {
      Serial.println("MQTT OTA trigger");
      performUpdate();
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(DOOR_PIN, INPUT_PULLUP);

  // ===== WIFI =====
  WiFiManager wm;
  wm.autoConnect("DoorMonitor_Setup");

  Serial.println(WiFi.localIP());

  // ===== OTA (IDE) =====
  ArduinoOTA.setHostname("door-monitor");
  ArduinoOTA.begin();

  // ===== MQTT =====
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  // ===== SAFE BOOT OTA =====
  delay(5000);
  checkForUpdate();
}

// ===== LOOP =====
void loop() {

  ArduinoOTA.handle();

  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  // ===== DOOR SENSOR =====
  int reading = digitalRead(DOOR_PIN);

  if (reading != lastDoorState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {

    bool currentDoor = (reading == LOW);  // test mode

    if (currentDoor != doorOpen) {
      doorOpen = currentDoor;

      if (doorOpen) {
        Serial.println("Door OPEN");

        client.publish("home/door/state", "{\"state\":\"OPEN\"}", true);

        if (systemArmed) {
          sendTelegram("🚪 Door OPEN (ALERT)");
        }

      } else {
        Serial.println("Door CLOSED");

        client.publish("home/door/state", "{\"state\":\"CLOSED\"}", true);
      }
    }
  }

  lastDoorState = reading;

  // ===== HEARTBEAT =====
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();

    client.publish("home/door/heartbeat",
      "{\"status\":\"alive\"}", true);
  }

  delay(10);
}