#include "mqtt.h"

boolean reconnect_mqtt(PubSubClient& client, const char* client_id) {
  if (client.connected()) {
    return true;
  }
  Serial.println(F("Reconnecting to MQTT broker"));
  client.connect(client_id
#ifdef MQTT_BROKER_USER
    ,MQTT_BROKER_USER
#endif
#ifdef MQTT_BROKER_PASS
    ,MQTT_BROKER_PASS
#endif
  );
  if (client.connected()) {
    Serial.println(F("Connected to MQTT broker"));
    return true;
  } else {
    Serial.println(F("Failed to connect to MQTT broker"));
    return false;
  }
}
