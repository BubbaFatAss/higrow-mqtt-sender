#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <DHT_U.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "device_id.h"
#include "mqtt.h"
#include "wifisetup.h"

const int DHT_PIN = 22;
const int DHT_TYPE = DHT11;
const int SOIL_PIN = 32;
const int LIGHT_PIN = 33;
const int INFO_LED_PIN = 16;

const String TOPIC_PREFIX = "higrow_plant_monitor/";
const String TOPIC_SUFFIX = "/state";
const int DEEP_SLEEP_SECONDS = 60 * 30;

const int WATER_MEASUREMENTS = 10;
const int WATER_MEASURE_INTERVAL_MS = 200;
// The following values were calibrated using a glass of water
const int NO_WATER = 3323;
const int FULL_WATER = 1389;

DHT_Unified dht(DHT_PIN, DHT_TYPE);

String device_id;
String client_id;
String topic;

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

struct DiscoveryDevices
{
  char const* m_deviceClass;
  char const* m_nameStrSuffix;
  char const* m_uniqueIDSuffix;
  char const* m_unitOfMeasurement;
  char const* m_valueTemplate;
  char const* m_icon;
};

DiscoveryDevices discoveryDevices[4] = {
  {"temperature", " temperature", "_temperature", "°C", "{{ value_json.temperature_celsius }}", nullptr},
  {"humidity", " humidity", "_humidity", "%", "{{ value_json.humidity_percent }}", nullptr},
  {nullptr, " water", "_water", "%", "{{ value_json.water }}", "mdi:water-percent"},
  {nullptr, " light", "_light", "%", "{{ value_json.light }}", "mdi:theme-light-dark"},
};

void print_info() {
  Serial.println(F("------------------------------------------------------------"));
  Serial.print(F("Device id:      "));
  Serial.println(device_id);
  Serial.print(F("Mqtt broker:    "));
  Serial.print(MQTT_BROKER_HOST);
  Serial.print(F(":"));
  Serial.println(MQTT_BROKER_PORT);
  #ifdef MQTT_BROKER_USER
  Serial.print(F("MQTT Username:  "));
  Serial.println(MQTT_BROKER_USER);
  #endif
  #ifdef MQTT_BROKER_PASS
  Serial.print(F("MQTT Password:  "));
  Serial.println(MQTT_BROKER_PASS);
  #endif
  Serial.print(F("Mqtt topic:     "));
  Serial.println(topic);
  Serial.print(F("Mqtt client id: "));
  Serial.println(client_id);
  Serial.println(F("------------------------------------------------------------"));
}

void addDeviceElement(DynamicJsonDocument& i_document)
{
  JsonObject deviceObject = i_document.createNestedObject("device");
  JsonArray identifiersArray = deviceObject.createNestedArray("identifiers");
  identifiersArray.add(client_id.c_str());
  deviceObject["manufacturer"] = "DIY More";
  deviceObject["model"] = "ESP32 DHT11 WIFI Bluetooth Soil Temperature Humidity Sensor18650";
  deviceObject["name"] = client_id.c_str();
  deviceObject["sw_version"] = "https://github.com/BubbaFatAss/higrow-mqtt-sender";
}

void sendMQTTDiscoveryTopic() {
  if(reconnect_mqtt(mqtt_client, client_id.c_str())) {
    uint const numDevices = sizeof(discoveryDevices) / sizeof(discoveryDevices[0]);
    Serial.print("Starting HomeAssistnt discovery topics for ");
    Serial.print(numDevices);
    Serial.println(" devices...");
    for (uint i = 0; i < numDevices; ++i)
    {
      DynamicJsonDocument configDocument(1024);
      addDeviceElement(configDocument);
      if( discoveryDevices[i].m_deviceClass != nullptr )
      {
        configDocument["device_class"] = discoveryDevices[i].m_deviceClass;
      }
      configDocument["json_attributes_topic"] = topic.c_str();
      String nameStr = client_id + discoveryDevices[i].m_nameStrSuffix;
      configDocument["name"] = nameStr.c_str();
      //configDocument["state_topic"] = topic.c_str();
      String uniqueID = client_id + discoveryDevices[i].m_uniqueIDSuffix;
      configDocument["unique_id"] = uniqueID.c_str();
      configDocument["unit_of_measurement"] = discoveryDevices[i].m_unitOfMeasurement;
      configDocument["value_template"] = discoveryDevices[i].m_valueTemplate;
      if( discoveryDevices[i].m_icon != nullptr )
      {
        configDocument["icon"] = discoveryDevices[i].m_icon;
      }

      String payload;
      serializeJson(configDocument, payload);
      String discoveryTopic = "homeassistant/sensor/";
      discoveryTopic += client_id;
      discoveryTopic += "/";
      String nameStrTrimmed = discoveryDevices[i].m_nameStrSuffix;
      nameStrTrimmed.trim();
      discoveryTopic += nameStrTrimmed;
      discoveryTopic += "/config";
      if (mqtt_client.beginPublish(discoveryTopic.c_str(), payload.length(), true)) {
        mqtt_client.print(payload);
        if( mqtt_client.endPublish() == 1 )
        {
          Serial.print("Published discovery topic '");
          Serial.print(discoveryTopic.c_str());
          Serial.println("' successfully.");
        } else {
          Serial.print("endPublish discovery topic '");
          Serial.print(discoveryTopic.c_str());
          Serial.println("' failed.");
        }
      } else {
        Serial.print("beginPublish discovery topic '");
        Serial.print(discoveryTopic.c_str());
        Serial.println("' failed.");
      }
    }
    mqtt_client.disconnect();
  } else {
    Serial.println("Failed to connect MQTT Client, skipping HomeAssistant discovery topics");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println(F("Hello :)\nHigrow MQTT sender\nhttps://github.com/tom-mi/higrow-mqtt-sender"));

  pinMode(INFO_LED_PIN, OUTPUT);

  device_id = get_device_id();
  client_id = "higrow_" + device_id;
  topic = TOPIC_PREFIX + device_id + TOPIC_SUFFIX;

  print_info();

  connect_wifi();

  mqtt_client.setServer(MQTT_BROKER_HOST, String(MQTT_BROKER_PORT).toInt());

  dht.begin();

  //Send Homeassistant Device Discovery topic
  sendMQTTDiscoveryTopic();

  digitalWrite(INFO_LED_PIN, LOW);
}

float read_sensor(int pin, int zero_value, int one_value) {
  int raw = analogRead(pin);
  float level = map(raw, zero_value, one_value, 0, 10000) / 100.;
  return constrain(level, 0., 100.);
}

float read_water_smoothed() {
  float value = 0.;
  for (int i=0; i<WATER_MEASUREMENTS; i++) {
    value += read_sensor(SOIL_PIN, NO_WATER, FULL_WATER);
    delay(WATER_MEASURE_INTERVAL_MS);
  }
  return value / WATER_MEASUREMENTS;
}

float read_temperature() {
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  return event.temperature;
}

float read_relative_humidity() {
  sensors_event_t event;
  dht.humidity().getEvent(&event);
  return event.relative_humidity;
}

void read_and_send_data() {
  DynamicJsonDocument root(1024);
  root["device_id"] = device_id;
  root["temperature_celsius"] = read_temperature();
  root["humidity_percent"] = read_relative_humidity();
  root["water"] = read_water_smoothed();
  root["light"] = read_sensor(LIGHT_PIN, 0, 4095);
  Serial.print(F("Sending payload: "));
  String payload;
  serializeJson(root, payload);
  Serial.println(payload.c_str());
  if(reconnect_mqtt(mqtt_client, client_id.c_str())) {
    if (mqtt_client.beginPublish(topic.c_str(), payload.length(), true)) {
      mqtt_client.print(payload);
      if( mqtt_client.endPublish() == 1 )
      {
        Serial.print("Published data topic '");
        Serial.print(topic.c_str());
        Serial.println("' successfully.");
      } else {
        Serial.print("endPublish data topic '");
        Serial.print(topic.c_str());
        Serial.println("' failed.");
      }
      mqtt_client.disconnect();
      Serial.println("Success sending message");
    } else {
        Serial.println("Error sending message");
    }
  }
}

void loop() {
  read_and_send_data();
  Serial.println(F("Flushing wifi client"));
  wifi_client.flush();
  delay(2000);  // Wait a bit to ensure message sending is complete
  Serial.println(F("Disconnecting wifi"));
  WiFi.disconnect(true, false);
  Serial.print(F("Going to sleep after "));
  Serial.print(millis());
  Serial.println(F("ms"));
  Serial.flush();
  ESP.deepSleep(DEEP_SLEEP_SECONDS * 1000000);
}
