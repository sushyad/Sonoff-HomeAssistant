#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include "EmonLib.h"
#include "config.h"
#include "debug.h"

#define BUTTON          0                                    // (Don't Change for Sonoff)
#define SWITCH          14                                   // Light flip switch
#define RELAY           12                                   // (Don't Change for Sonoff)
#define LED             13                                   // (Don't Change for Sonoff)

#define MQTT_CLIENT     "Sonoff_Living_Room_v1.0p"           // mqtt client_id (Must be unique for each Sonoff)
#define MQTT_SERVER     "192.168.0.3"                      // mqtt server
#define MQTT_PORT       1883                                 // mqtt port
#define MQTT_TOPIC      "wsn/light1"          // mqtt topic (Must be unique for each Sonoff)
#define MQTT_USER       "user"                               // mqtt user
#define MQTT_PASS       "pass"                               // mqtt password

#define WIFI_SSID       "syadav_2.4GHz_F24B81"                           // wifi ssid
#define WIFI_PASS       "12762156"                           // wifi password

#define VERSION    "\n\n------------------  Sonoff Powerpoint v1.0p  -------------------"

extern "C" {
  #include "user_interface.h"
}

bool sendStatus = false;
bool requestRestart = false;

int kUpdFreq = 1;
int kRetries = 10;

unsigned long TTasks;
unsigned long count = 0;
unsigned long switchCount = 0;
bool prevSwitchState = false;
bool actionTaken = false;

EnergyMonitor dryer;                   // Create an instance

boolean wasDryerOn = false;
boolean isInitialized = false;

long lastMessageTime = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient, MQTT_SERVER, MQTT_PORT);
Ticker btn_timer;

void callback(const MQTT::Publish& pub) {
  if (pub.payload_string() == "stat") {
  }
  else if (pub.payload_string() == "on") {
    digitalWrite(LED, LOW);
    digitalWrite(RELAY, HIGH);
  }
  else if (pub.payload_string() == "off") {
    digitalWrite(LED, HIGH);
    digitalWrite(RELAY, LOW);
  }
  else if (pub.payload_string() == "reset") {
    requestRestart = true;
  }
  sendStatus = true;
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(BUTTON, INPUT);
  pinMode(SWITCH, INPUT_PULLUP);

  digitalWrite(LED, LOW);
  digitalWrite(RELAY, LOW);

  btn_timer.attach(0.01, button);

  mqttClient.set_callback(callback);

  dryer.current(A0, 6);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.begin(115200);
  Serial.println(VERSION);
  Serial.print("\nESP ChipID: ");
  Serial.print(ESP.getChipId(), HEX);
  Serial.print("\nConnecting to "); Serial.print(WIFI_SSID); Serial.print(" Wifi");
  while ((WiFi.status() != WL_CONNECTED) && kRetries --) {
    delay(500);
    Serial.print(" .");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" DONE");
    Serial.print("IP Address is: "); Serial.println(WiFi.localIP());
    Serial.print("Connecting to ");Serial.print(MQTT_SERVER);Serial.print(" Broker . .");
    delay(500);
    while (!mqttClient.connect(MQTT::Connect(MQTT_CLIENT).set_keepalive(90).set_auth(MQTT_USER, MQTT_PASS)) && kRetries --) {
      Serial.print(" .");
      delay(1000);
    }
    if(mqttClient.connected()) {
      Serial.println(" DONE");
      Serial.println("\n----------------------------  Logs  ----------------------------");
      Serial.println();
      mqttClient.subscribe(MQTT_TOPIC"/command");
      blinkLED(LED, 40, 8);
      digitalWrite(LED, HIGH);
    }
    else {
      Serial.println(" FAILED!");
      Serial.println("\n----------------------------------------------------------------");
      Serial.println();
    }
  }
  else {
    Serial.println(" WiFi FAILED!");
    Serial.println("\n----------------------------------------------------------------");
    Serial.println();
  }
}

void loop() {
  mqttClient.loop();
  timedTasks();
  checkStatus();
}

boolean isDryerOn()
{
  double irms_dryer = dryer.calcIrms(1480);  // Calculate Irms only
  double powerUsage = irms_dryer*240.0;  // Apparent power
  DEBUGLN(powerUsage);
  lastMessageTime = millis();
  return (powerUsage > 1000.0);
}

void reportDryerState(boolean isDryerOn) {
  if (isDryerOn) {
    mqttClient.publish(MQTT::Publish("wsn/washer/status", "ON").set_retain().set_qos(1));
   } else {
    mqttClient.publish(MQTT::Publish("wsn/washer/status", "OFF").set_retain().set_qos(1));
  }
}

void checkDryerStatus() {
  long currentTime = millis();
  if (currentTime - lastMessageTime > 10000) {
    boolean dryerOn = isDryerOn();

    if (!isInitialized || dryerOn || wasDryerOn != dryerOn) {
      isInitialized = true;
      reportDryerState(dryerOn);
    }

    wasDryerOn = dryerOn;

    lastMessageTime = currentTime;
  }
}

void blinkLED(int pin, int duration, int n) {
  for(int i=0; i<n; i++)  {
    digitalWrite(pin, HIGH);
    delay(duration);
    digitalWrite(pin, LOW);
    delay(duration);
  }
}

void button() {
  if (!digitalRead(BUTTON)) {
    count++;
  }
  else {
    if (count > 1 && count <= 200) {
      digitalWrite(LED, !digitalRead(LED));
      digitalWrite(RELAY, !digitalRead(RELAY));
      sendStatus = true;
    }
    else if (count >200){
      Serial.println("\n\nSonoff Rebooting . . . . . . . . Please Wait");
      requestRestart = true;
    }
    count=0;
  }

  bool switchState = digitalRead(SWITCH);
  if (switchState != prevSwitchState) {
    Serial.print("\n\nSonoff switch: "); Serial.println(switchState);
    prevSwitchState = switchState;
    switchCount = 0;
    actionTaken = false;
  } else {
    if (!actionTaken) {
      switchCount++;

      if (switchCount > 5) {
        actionTaken = true;
        Serial.println("\n\nSonoff toggling");
        digitalWrite(LED, !digitalRead(LED));
        digitalWrite(RELAY, !digitalRead(RELAY));
        sendStatus = true;
      }
    }
  }
}

void checkConnection() {
  if (WiFi.status() == WL_CONNECTED)  {
    if (mqttClient.connected()) {
      Serial.println("mqtt broker connection . . . . . . . . . . OK");
    }
    else {
      Serial.println("mqtt broker connection . . . . . . . . . . LOST");
      requestRestart = true;
    }
  }
  else {
    Serial.println("WiFi connection . . . . . . . . . . LOST");
    requestRestart = true;
  }
}

void checkStatus() {
  checkDryerStatus();

  if (sendStatus) {
    if(digitalRead(LED) == LOW)  {
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/status", "on").set_retain().set_qos(1));
      Serial.println("Relay . . . . . . . . . . . . . . . . . . ON");
    } else {
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/status", "off").set_retain().set_qos(1));
      Serial.println("Relay . . . . . . . . . . . . . . . . . . OFF");
    }
    sendStatus = false;
  }
  if (requestRestart) {
    blinkLED(LED, 400, 4);
    ESP.restart();
  }
}

void timedTasks() {
  if ((millis() > TTasks + (kUpdFreq*60000)) || (millis() < TTasks)) {
    TTasks = millis();
    checkConnection();
  }
}
