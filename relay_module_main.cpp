/* 
Automated home irrigation based on weather data provided by CIMIS weather stations (https://cimis.water.ca.gov/Default.aspx).
Copyright (C) 2024  Natalie C. Pueyo Svoboda

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.


turn on drip/sprinkler systems based on calculations using
CIMIS provided ETo and UCANR SLIDE rules (https://ucanr.edu/sites/UrbanHort/Water_Use_of_Turfgrass_and_Landscape_Plant_Materials/SLIDE__Simplified_Irrigation_Demand_Estimation/)
to compute correct irrigation timing for the various garden locations. 
*/ 

#include <Arduino.h>
#include "ESP8266WiFi.h"
// see https://electrosome.com/connecting-esp8266-wifi/ for basic connectivity code setting esp8266 to station mode
#include <MQTT.h> // see https://github.com/256dpi/arduino-mqtt and for example usage, https://esp32io.com/tutorials/esp32-mqtt. Requires https://github.com/256dpi/lwmqtt


// Wifi information header  
#include "IrrigationConfig.h" 

const char ssid[] = WIFI_SSID_SECRET;
const char password[] = WIFI_PASSWORD_SECRET;

const char client_id[] = "esp1_node";
const char host_id[] = MQTT_HOST;
const char mqtt_ssid[] = MQTT_SSID_SECRET;
const char mqtt_password[] = MQTT_PASSWORD_SECRET;

String controller_num = String("1 ");   // ESP number for this garden section (0 == offline), add space to make processing easier

WiFiClient net;
MQTTClient client;

unsigned long lastMillis = 0;
unsigned long newTimer = 0;     // in msec, the amount of time that has passed since the relay turned on
bool newTimerState = false;     // flag to determine when newTimer should be heeded
unsigned long relayOnTimer = 0; // in msec, the amount of time the relay should stay ON to water the plants

uint8_t relay_state2 = 0x0; 
uint8_t relay_state3 = 0x0; 
uint8_t relay_state4 = 0x0;
uint8_t relay_state8 = 0x0;  // extra pin if needed

void connect() {
//   Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

//   Serial.print("\nconnecting...");
  while (!client.connect(client_id, mqtt_ssid, mqtt_password)) {
    Serial.print(".");
    delay(1000);
  }

//   Serial.println("\nconnected!");

  client.subscribe("/back_yard");
  // client.unsubscribe("/back_yard");
}



void messageReceived(String &topic, String &payload) {
//   Serial.println("incoming: " + topic + " - " + payload);

  String relay_string = payload.substring(0,1);
  String time_string = payload.substring(2);
  
  switch (relay_string.toInt())
  // change global variable that leads to irrigation for the given garden sector, only three relays are in use
  {
    case 2:
        relay_state2 = 0x1;
        break;
    case 3:
        relay_state3 = 0x1;
        break;
    case 4:
        relay_state4 = 0x1;
        break;
    case 8:
        relay_state8 = 0x1;
        break;
  }

  relayOnTimer = (unsigned long)time_string.toInt();
  // Serial.printf("Relay will be on for %lu (in msec)\n", relayOnTimer);
 
  // Note: Do not use the client in the callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable,
  // or push to a queue and handle it in the loop after calling `client.loop()`.
}


// see https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// const int R1_GPIO = 15; // D8, pulled to ground and SPI (only ok as output pin)
const int R2_GPIO = 13; // D7, MOSI
const int R3_GPIO = 12; // D6, MISO
const int R4_GPIO = 14; // D5, SCK
// const int R5_GPIO = 16; // D0, WAKE
const int R6_GPIO = 3;  // RX, don't use this as output especially if UART in is desirable
const int R7_GPIO = 0;  // D3, FLASH, pulled to ground
// using D4 pulled to low, means that the LED will always be on
const int R8_GPIO = 2;  // D4, LED, pulled to ground, HIGH at boot

void setup() {
    // Serial.begin(115200);

    // ESP8266_REG(310) = (1 << 2); //   this is the PIN_DIR_OUTPUT register (sets a pin to be an output if the gpio's bit is high) -> GPES in core_esp8266_wiring_digital.cpp
    // gpio_output_set((bit_value)<<gpio_no, ((~(bit_value))&0x01)<<gpio_no, 1<<gpio_no, 0)

    pinMode(R2_GPIO, OUTPUT); 
    digitalWrite(R2_GPIO, 0x0); 
    pinMode(R3_GPIO, OUTPUT); 
    digitalWrite(R3_GPIO, 0x0); 
    pinMode(R4_GPIO, OUTPUT); 
    digitalWrite(R4_GPIO, 0x0); 
    pinMode(R8_GPIO, OUTPUT); 
    digitalWrite(R8_GPIO, 0x0); 

    // turn relays 6-7 off, otherwise on by default
    pinMode(R6_GPIO, OUTPUT); 
    digitalWrite(R6_GPIO, 0x0);
    pinMode(R7_GPIO, OUTPUT); 
    digitalWrite(R7_GPIO, 0x0);
    

    // Connect to WiFi
    WiFi.begin(ssid, password);

    client.begin(host_id, net);
    client.onMessage(messageReceived);

    connect();
}


void loop() {

    client.loop();
    delay(10);  // <- fixes some issues with WiFi stability

    if (!client.connected()) {
        connect();
    }

    
    if ((relay_state2 || relay_state3 || relay_state4 || relay_state8) && !newTimerState) {
        // if any of the relay states is on, start a timer to turn them off in ~1 second
        newTimer = millis();
        newTimerState = true;
    }

    // turn off any relays and reset timers
    if ((millis() - newTimer > relayOnTimer) && newTimerState) {
        int relayOn = relay_state8*8 + relay_state4*4 + relay_state3*3 + relay_state2*2;  // possible bc only one relay should ever be on at any one time
        
        client.publish("/relay_done", String(controller_num + relayOn));
        // Serial.printf("The outgoing message will say that relay %d was on\n", relayOn);

        relay_state2 = 0x0;
        relay_state3 = 0x0;
        relay_state4 = 0x0;
        relay_state8 = 0x0;
        newTimerState = false;
        relayOnTimer = 0;
    }

    digitalWrite(R2_GPIO, relay_state2);
    digitalWrite(R3_GPIO, relay_state3); 
    digitalWrite(R4_GPIO, relay_state4);
    digitalWrite(R8_GPIO, relay_state8);
    

    // test by publishing a message roughly every 10 second.
    // if (millis() - lastMillis > 10000) {
    //     lastMillis = millis();
    //     client.publish("/back_yard", "2 3000"); // test that the correct relay turns for the correct amount of time
    // }

}

