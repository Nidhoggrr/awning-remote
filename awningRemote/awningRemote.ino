#include <Basecamp.hpp>
#include <Esp.h>
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

static const bool debug = false;

Basecamp iot;
static const int outPin = 25;
static const int stopPin = 26;
static const int inPin = 27;
static const int ledPin = 22;
int triggeredPin = 22;
//in ms / 100
static const int totalTravelTime = 180;

unsigned long TriggerTimer = 0;
int i = 0;
bool moving = false;

TimerHandle_t movementStopTimer;
TimerHandle_t sendStatusTimer;
TimerHandle_t mqttWatchdog;

String mqttTopic = "homie/empty/percent";
String mqttTopicWD = "homie/empty/watchdog/set";
int percentage = 1;

int getValidNumber(String str) {
  if (strcmp(str.substring(0,2).c_str(),"0.") == 0) {
    str=str.substring(2,4);
  }
  if (strcmp(str.substring(0,3).c_str(),"1.0") == 0) {
    str="100";
  }
  for (int i = 0; i < str.length(); i++)
  {
    if (!isDigit(str[i])) {
      return 0;
    }
  }
  return str.toInt();
}

void triggerPin(int Pin) {
  TriggerTimer = millis();
  triggeredPin = Pin;
  digitalWrite(Pin, LOW);
}

void onMqttConnect(bool sessionPresent) {
  if (debug) Serial.println("Connected to MQTT.");
  iot.mqtt.subscribe((mqttTopic + "/set").c_str(), 2);
  iot.mqtt.subscribe((mqttTopicWD + "/set").c_str(), 2);
  iot.mqtt.publish(("homie/" + iot.hostname + "/$homie").c_str(), 1, true, "3.0" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$name").c_str(), 1, true, "Markise" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$state").c_str(), 1, true, "init" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$localip").c_str(), 1, true, iot.wifi.getIP().toString().c_str() );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$mac").c_str(), 1, true, iot.wifi.getHardwareMacAddress().c_str() );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$fw/name").c_str(), 1, true, "adke.awning" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$fw/version").c_str(), 1, true, "0.4" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$implementation").c_str(), 1, true, "esp32" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$stats").c_str(), 1, true, "" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$stats/interval").c_str(), 1, true, "120" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$stats/uptime").c_str(), 1, true, String(millis()).c_str() );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$nodes").c_str(), 1, true, "status" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/$name").c_str(), 1, true, "Prozent" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/$properties").c_str(), 1, true, "percent,watchdog" );

  iot.mqtt.publish(("homie/" + iot.hostname + "/status/percent/$datatype").c_str(), 1, true, "integer" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/percent").c_str(), 1, true, "0" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/percent/$name").c_str(), 1, true, "Abdeckung" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/percent/$unit").c_str(), 1, true, "%" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/percent/$settable").c_str(), 1, true, "true" );

  iot.mqtt.publish(("homie/" + iot.hostname + "/status/watchdog/$datatype").c_str(), 1, true, "string" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/watchdog").c_str(), 1, true, "0" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/watchdog/$settable").c_str(), 1, true, "true" );
  iot.mqtt.publish(("homie/" + iot.hostname + "/status/watchdog/$name").c_str(), 1, true, "Watchdog Tick" );

  iot.mqtt.publish(("homie/" + iot.hostname + "/$state").c_str(), 1, true, "ready" );
  iot.mqtt.setWill(("homie/" + iot.hostname + "/$state").c_str(), 1, true, "lost" );
  if (debug) Serial.println("Subscribing to " + mqttTopic);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if ( strcmp(topic, (mqttTopic + "/set").c_str()) == 0 ) {
    if (debug) Serial.println("Got command " + String(payload));
    int p = getValidNumber(payload);
    if ( p >= 0 && p <= 100) {
      setPercentage(p);
    }
  }
  xTimerReset(mqttWatchdog, 0);
}

void setPercentage(int gotoPercentage) {
  if (!xTimerIsTimerActive(movementStopTimer)) {
    if (debug) Serial.println("Starting movement to: " + String(gotoPercentage));
    int travelTime = totalTravelTime * abs(gotoPercentage - percentage);
    xTimerChangePeriod(movementStopTimer, pdMS_TO_TICKS(travelTime), 0);
    if ( gotoPercentage < percentage ) {
      triggerPin(inPin);
    } else if ( gotoPercentage > percentage ) {
      triggerPin(outPin);
    }
    if (gotoPercentage != percentage) {
      digitalWrite(ledPin, LOW);
      percentage = gotoPercentage;
      xTimerStart(movementStopTimer, 0);
    }
  }
}

void movementStop() {
  digitalWrite(ledPin, HIGH);
  if ( percentage != 0 && percentage != 100 ) {
    if (debug) Serial.println("triggerPin(stopPin);");
    triggerPin(stopPin);
  }
  moving = false;
  if (debug) Serial.println("Done going to " + String(percentage));
  sendStatus();
}

void sendStatus() {
  iot.mqtt.publish(mqttTopic.c_str(), 1, true, String(percentage).c_str() );
  iot.mqtt.publish(("homie/" + iot.hostname + "/$stats/uptime").c_str(), 1, true, String(millis()).c_str() );
}

void reset() {
  ESP.restart();
}

void setup() {
  pinMode(outPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(outPin, HIGH);
  pinMode(stopPin, OUTPUT);
  digitalWrite(stopPin, HIGH);
  pinMode(inPin, OUTPUT);
  digitalWrite(inPin, HIGH);
  iot.begin();
  sendStatusTimer = xTimerCreate("statusTime", pdMS_TO_TICKS(120000), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(sendStatus));
  movementStopTimer = xTimerCreate("movementStopTimer", pdMS_TO_TICKS(totalTravelTime), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(movementStop));
  mqttWatchdog = xTimerCreate("WD", pdMS_TO_TICKS(122000), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(reset));

  mqttTopic = "homie/" + iot.hostname + "/status/percent";
  mqttTopicWD = "homie/" + iot.hostname + "/status/watchdog";

  setPercentage(0);
  iot.mqtt.onMessage(onMqttMessage);
  iot.mqtt.onConnect(onMqttConnect);
  iot.mqtt.connect();
  xTimerStart(sendStatusTimer, 0);
  xTimerStart(mqttWatchdog, 0);
}

void loop() {
  if (millis() - TriggerTimer >= 60UL) digitalWrite(triggeredPin, HIGH);
}
