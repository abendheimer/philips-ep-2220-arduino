#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "philips_coffee_machine.h"
#include "config.h"
#include "secrets.h"

#define THINGNAME "philips-ep-2220"

// Custom topic structure
const char* TOPIC_WATER_IS_EMPTY = "philipsep2220/waterIsEmpty";
const char* TOPIC_WASTE_IS_FULL = "philipsep2220/wasteIsFull";
const char* TOPIC_OP_STATE = "philipsep2220/opState";
const char* TOPIC_SELECTED_OPTION = "philipsep2220/selectedOption";
const char* TOPIC_SELECTED_BEAN_AMOUNT = "philipsep2220/selectedBeanAmount";
const char* TOPIC_SELECTED_CUP_SIZE = "philipsep2220/selectedCupSize";
const char* TOPIC_BREWED_TYPE = "philipsep2220/brewedType";

const char* TOPIC_RESET = "backendToPhilipsep2220/reset";
const char* TOPIC_TURN_POWER_ON = "backendToPhilipsep2220/turnPowerOn";
const char* TOPIC_TURN_POWER_OFF = "backendToPhilipsep2220/turnPowerOff";
const char* TOPIC_RESET_COFFEE_MACHINE = "backendToPhilipsep2220/resetCoffeeMachine";
const char* TOPIC_PRESS_PLAY_OR_PAUSE = "backendToPhilipsep2220/pressPlayOrPause";
const char* TOPIC_SELECT_TYPE = "backendToPhilipsep2220/selectType";
const char* TOPIC_SELECT_BEAN = "backendToPhilipsep2220/selectBean";
const char* TOPIC_SELECT_CUP_SIZE = "backendToPhilipsep2220/selectCupSize";
const char* TOPIC_SELECT_CALC_CLEAN = "backendToPhilipsep2220/selectCalcClean";
const char* TOPIC_SELECT_AQUA_CLEAN = "backendToPhilipsep2220/selectAquaClean";

// Convert certificates to BearSSL::X509List
BearSSL::X509List rootCA(ca_cert);
BearSSL::X509List clientCertificate(client_cert);
BearSSL::PrivateKey clientPrivateKey(private_key);

// MQTT Client Setup
WiFiClientSecure wifiClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
PubSubClient mqttClient(wifiClient);

// Coffee Machine
PhilipsCoffeeMachine coffeeMachine;

// Status tracking
unsigned long lastStatusUpdate = 0;
const int statusUpdateInterval = 2000; // 2 seconds
bool wifiConnected = false;
bool mqttConnected = false;

// WiFi connection management
unsigned long lastWifiCheckTime = 0;
const int wifiCheckInterval = 30000; // 30 seconds
int wifiReconnectAttempts = 0;
const int maxWifiReconnectAttempts = 5;


// Process MQTT message
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  
  // Create a null-terminated string from the payload
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("Payload: ");
  Serial.println(message);
  
  // Pre-power on command
  if (strcmp(topic, TOPIC_RESET) == 0) {
    Serial.println("Reset");
    ESP.restart();
  }
  
  // Power on command
  if (strcmp(topic, TOPIC_TURN_POWER_ON) == 0) {
    bool withCleaning = true;
    
    // Check if payload contains JSON with cleaning preference
    if (length > 0) {
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, message);
      
      if (!error && doc.containsKey("cleaning")) {
        withCleaning = doc["cleaning"].as<bool>();
      }
    }
    
    Serial.printf("Power on command sent (with cleaning: %s)\n", withCleaning ? "yes" : "no");
    coffeeMachine.powerOn(withCleaning);
    return;
  }
  
  // Power off command
  if (strcmp(topic, TOPIC_TURN_POWER_OFF) == 0) {
    coffeeMachine.powerOff();
    Serial.println("Power off command sent");
    return;
  }
  
  // Reset coffee machine command
  if (strcmp(topic, TOPIC_RESET_COFFEE_MACHINE) == 0) {
    coffeeMachine.powerOff();
    delay(1000);
    coffeeMachine.powerOn(false); // Power on without cleaning for reset
    Serial.println("Coffee machine reset command executed");
    return;
  }
  
  // Play/pause button command
  if (strcmp(topic, TOPIC_PRESS_PLAY_OR_PAUSE) == 0) {
    coffeeMachine.executeButtonAction(PLAY_PAUSE);
    Serial.println("Play/Pause button press executed");
    return;
  }
  
  // Select beverage type
  if (strcmp(topic, TOPIC_SELECT_TYPE) == 0) {
    if (length == 0) return;
    
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.println("Failed to parse JSON in selectType");
      return;
    }
    
    if (!doc.containsKey("message")) {
      Serial.println("No type specified in selectType");
      return;
    }
    
    String type = doc["message"].as<String>();
    bool makeBeverage = false;
    
   
    
    Action action;
    
    if (type == "COFFEE") {
      action = SELECT_COFFEE;
    } 
    else if (type == "ESPRESSO") {
      action =  SELECT_ESPRESSO;
    }
    else if (type == "HOT_WATER") {
      action =  SELECT_HOT_WATER;
    }
    else if (type == "STEAM") {
      action =  SELECT_STEAM;
    }
    else {
      Serial.printf("Unknown beverage type: %s\n", type.c_str());
      return;
    }
    
    coffeeMachine.executeButtonAction(action);
    Serial.printf("Selected beverage type: %s (with make: %s)\n", 
                  type.c_str(), makeBeverage ? "yes" : "no");
    return;
  }
  
  // Bean selection
  if (strcmp(topic, TOPIC_SELECT_BEAN) == 0) {
    if (length == 0) return;
    
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.println("Failed to parse JSON in selectBean");
      return;
    }
    
    if (!doc.containsKey("message")) {
      Serial.println("No value specified in selectBean");
      return;
    }
    
    int value = doc["message"].as<int>();
    if (value < 1 || value > 3) {
      Serial.println("Invalid bean strength value (must be 1-3)");
      return;
    }
    
    // First select bean adjustment mode
    coffeeMachine.executeButtonAction(SELECT_BEAN);
    
    // Then adjust to desired value
    BeverageSource source = ANY_SOURCE;
    if (doc.containsKey("source")) {
      String sourceStr = doc["source"].as<String>();
      if (sourceStr == "COFFEE") source = COFFEE_SOURCE;
      else if (sourceStr == "ESPRESSO") source = ESPRESSO_SOURCE;
    }
    
    coffeeMachine.setBeverageSetting(BEAN_TYPE, source, value);
    Serial.printf("Bean strength set to: %d\n", value);
    return;
  }
  
  // Cup size selection
  if (strcmp(topic, TOPIC_SELECT_CUP_SIZE) == 0) {
    if (length == 0) return;
    
    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.println("Failed to parse JSON in selectCupSize");
      return;
    }
    
    if (!doc.containsKey("message")) {
      Serial.println("No value specified in selectCupSize");
      return;
    }
    
    int value = doc["message"].as<int>();
    if (value < 1 || value > 3) {
      Serial.println("Invalid cup size value (must be 1-3)");
      return;
    }
    
    // First select size adjustment mode
    coffeeMachine.executeButtonAction(SELECT_SIZE);
    
    // Then adjust to desired value
    BeverageSource source = ANY_SOURCE;
    if (doc.containsKey("source")) {
      String sourceStr = doc["source"].as<String>();
      if (sourceStr == "COFFEE") source = COFFEE_SOURCE;
      else if (sourceStr == "ESPRESSO") source = ESPRESSO_SOURCE;
      else if (sourceStr == "CAPPUCCINO") source = CAPPUCCINO_SOURCE;
      else if (sourceStr == "HOT_WATER") source = HOT_WATER_SOURCE;
      else if (sourceStr == "AMERICANO") source = AMERICANO_SOURCE;
      else if (sourceStr == "LATTE_MACCHIATO") source = LATTE_MACCHIATO_SOURCE;
    }
    
    coffeeMachine.setBeverageSetting(SIZE_TYPE, source, value);
    Serial.printf("Cup size set to: %d\n", value);
    return;
  }
  
  // Calc clean selection
  if (strcmp(topic, TOPIC_SELECT_CALC_CLEAN) == 0) {
    coffeeMachine.executeButtonAction(SELECT_CALC_CLEAN);
    Serial.println("Calc Clean button pressed");
    return;
  }
  
  // Aqua clean selection
  if (strcmp(topic, TOPIC_SELECT_AQUA_CLEAN) == 0) {
    coffeeMachine.executeButtonAction(SELECT_AQUA_CLEAN);
    Serial.println("Aqua Clean button pressed");
    return;
  }
}

// Speichern des letzten Status
bool lastWaterEmptyStatus = false;
bool lastWasteFullStatus = false;
String lastOpState = "";
String lastSelectedOption = "none";
String lastBrewedType = "none";
int lastBeanAmount = 0;
int lastCupSize = 0;

void publishStatusUpdates() {
  if (millis() - lastStatusUpdate < statusUpdateInterval) {
    return;
  }

  lastStatusUpdate = millis();

  if (!mqttConnected) {
    return;
  }

  // 1. Publish water empty status
  const bool waterIsEmpty = coffeeMachine.getStatusString().equals(state_water_empty.c_str());
  if (waterIsEmpty != lastWaterEmptyStatus) {
    String waterEmptyPayload = waterIsEmpty ? "true" : "false";
    mqttClient.publish(TOPIC_WATER_IS_EMPTY,("{\"message\": \"" + waterEmptyPayload + "\"}").c_str());
    lastWaterEmptyStatus = waterIsEmpty;
  }
  
  // 2. Publish waste container status
  const bool wasteIsFull = coffeeMachine.getStatusString().equals(state_water_empty.c_str());
  if (wasteIsFull != lastWasteFullStatus) {
    String wasteFullPayload = wasteIsFull ? "true" : "false";
    mqttClient.publish(TOPIC_WASTE_IS_FULL,("{\"message\": \"" + wasteFullPayload + "\"}").c_str());
    lastWasteFullStatus = wasteIsFull;
  }

  // 3. Publish operation state
  String currentOpState = coffeeMachine.getStatusString();
  if (currentOpState != lastOpState) {
    mqttClient.publish(TOPIC_OP_STATE,("{\"message\": \"" + currentOpState + "\"}").c_str());
    lastOpState = currentOpState;
  }

  // 4. Publish selected option / brewed type
  String currentStatus = coffeeMachine.getStatusString();
  String selectedOption = "none";
  String brewedType = "none";
  
  // Determine selected option and brewed type based on status
  if (currentStatus.indexOf("Coffee selected") != -1) {
    selectedOption = "COFFEE";
  } else if (currentStatus.indexOf("Espresso selected") != -1) {
    selectedOption = "ESPRESSO";
  } else if (currentStatus.indexOf("Hot water selected") != -1) {
    selectedOption = "HOT_WATER";
  } else if (currentStatus.indexOf("Steam selected") != -1) {
    selectedOption = "STEAM";
  }
  
  // Determine brewing status
  if (currentStatus.indexOf("Brewing Coffee") != -1) {
    brewedType = "COFFEE";
  } else if (currentStatus.indexOf("Brewing Espresso") != -1) {
    brewedType = "ESPRESSO";
  } else if (currentStatus.indexOf("Making Hot Water") != -1) {
    brewedType = "HOT_WATER";
  } else if (currentStatus.indexOf("Making Steam") != -1) {
    brewedType = "STEAM";
  }

  // Only publish if there's a change
  if (selectedOption != lastSelectedOption) {
    mqttClient.publish(TOPIC_SELECTED_OPTION, ("{\"message\": \"" + selectedOption + "\"}").c_str());
    lastSelectedOption = selectedOption;
  }

  if (brewedType != lastBrewedType) {
    mqttClient.publish(TOPIC_BREWED_TYPE, ("{\"message\": \"" + brewedType + "\"}").c_str());
    lastBrewedType = brewedType;
  }

  // 5. Publish selected bean amount and cup size
  BeverageSource currentSource = coffeeMachine.getCurrentBeverageSource();
  if (currentSource != NONE_SOURCE) {
    int beanAmount = coffeeMachine.getBeverageSetting(BEAN_TYPE, currentSource);
    if (beanAmount != lastBeanAmount) {
      if (beanAmount > 0) {
        String beanAmountStr = String(beanAmount);
        mqttClient.publish(TOPIC_SELECTED_BEAN_AMOUNT,  ("{\"message\": \"" + beanAmountStr + "\"}").c_str());
      }
      lastBeanAmount = beanAmount;
    }

    int cupSize = coffeeMachine.getBeverageSetting(SIZE_TYPE, currentSource);
    if (cupSize != lastCupSize) {
      if (cupSize > 0) {
        String cupSizeStr = String(cupSize);
        mqttClient.publish(TOPIC_SELECTED_CUP_SIZE,  ("{\"message\": \"" + cupSizeStr + "\"}").c_str());
      }
      lastCupSize = cupSize;
    }
  }
}


void connectToWifi() {
  // Start with the first Wi-Fi option
  int currentWiFi = 0;

  Serial.println("Attempting to connect to WiFi...");
  
  while (currentWiFi < NUM_WIFI) {
    Serial.print("Connecting to: ");
    Serial.println(WIFI_SSID[currentWiFi]);
    WiFi.begin(WIFI_SSID[currentWiFi], WIFI_PASSWORD[currentWiFi]);

    unsigned long startTime = millis();
    // Wait 15 seconds
    while (millis() - startTime < 15000) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi!");
        return;
      }
      delay(1000);
      Serial.print(".");
    }


    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      Serial.println("\nSSID not available. Trying next WiFi...");
    } else {
      Serial.println("\nFailed to connect. Trying next WiFi...");
    }

    currentWiFi++;
  }
}

// MQTT connection setup for AWS IoT
void setupMQTT() {

timeClient.begin();
  while(!timeClient.update()) {

    timeClient.forceUpdate();
  }

  Serial.println(timeClient.getEpochTime());

  wifiClient.setX509Time(timeClient.getEpochTime());
  wifiClient.setTrustAnchors(&rootCA);  // Set the root CA certificate
  wifiClient.setClientRSACert(&clientCertificate, &clientPrivateKey);  // Set the device certificate and private key 
  mqttClient.setServer(MQTT_HOST, 8883);  // Set the MQTT broker endpoint
  mqttClient.setCallback(mqttCallback);

  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println("Now to MQTT connected");
  delay(1000);
}

// Connect to MQTT broker (AWS IoT Core)
void connectToAWS() {
  while (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.println("Connecting to AWS IoT Core...");
    if (mqttClient.connect(THINGNAME)) {
      Serial.println("Connected to AWS IoT Core");
      mqttClient.subscribe("backendToPhilipsep2220/#"); // Subscribe to all senseo topics
    } else {
      Serial.print("Failed to connect to AWS IoT Core, state: ");
      Serial.println(mqttClient.state());
      delay(5000); // Retry every 5 seconds
    }
  }
}


void setup() {
  // Initialize serial at higher baud rate for better debugging
  Serial.begin(115200);
  delay(1000); // Give serial monitor time to connect
  
  Serial.println("\n\n--- Philips Coffee Machine MQTT Controller ---");
  Serial.println("Version 1.0.0");
  Serial.println("----------------------------------------");
  
  // Initialize coffee machine hardware
  delay(1000);
  coffeeMachine.begin();
  delay(1000);
  Serial.println("Coffee machine initialized successfully");
 
  
  connectToWifi();
  setupMQTT();

    

  connectToAWS();
  
  Serial.println("Setup complete, entering main loop");
}

void loop() {
  // Check WiFi connection periodically
  
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }
  if (!mqttClient.connected()) {
    connectToAWS();
  }
  mqttClient.loop();
  
  publishStatusUpdates();


  
  // Ensure the ESP has time to handle background tasks
  yield();
}