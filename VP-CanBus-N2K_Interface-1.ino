//
//   Volvo Penta CANbus to SignalK via MQTT Gateway
//
//   Filename        :   vp_canbus_to_n2k.py
//   Description     :   Sends boat engine data to MQTT Gateway
//   Date            :   22/05/2026
//   Author          :   Simon Thompson
//   Copyright       :   Simon Thompson 2026
//                   :   Buhhe 2021
//   Dependencies    :   Arduino IDE, ESP32 board support, MCP_CAN library, SPI library, WiFi library, PubSubClient library
//   License         :   GNU Lesser General Public License v2.1 or later
//   Repository      :   https://github.com/st599/VolvoPenta-MQTT_Interface 
//   Based on        :   This code is based on the work of Buhhe and has been modified to 
//                       read specific J1939 data from the Volvo Penta CANbus and send it to a SignalK server. 
//                       The original code can be found at https://github.com/buhhe/VolvoPenta-N2K_Interface.


/*
  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


//
//   SYSTEM IMPORTS
//
#include <Arduino.h>
#include <Preferences.h>
#include <mcp_can.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>

//
//  AUTHOR IMPORTS
//

//
//  GLOBAL CONSTANTS
//
const char *ssid = "xxxxx"; // Enter your WiFi name
const char *password = "xxxxx";  // Enter WiFi password
const char *mqtt_broker = "192.168.0.1";  // MQTT broker address (e.g., IP address or hostname of the SignalK server)
const int mqtt_port = 1883; // MQTT broker port (default is 1883 for non-secure connections)
WiFiClient espClient; // Create a WiFi client for MQTT communication
PubSubClient client(espClient); // Create an MQTT client using the WiFi client


#define CAN0_INT 17                            
MCP_CAN CAN0(5); 

const char Description[] = "Description: Volvo Penta->N2K interface. Read J1939 data from VP canbus, \nconvert it and send it to a SignalK server.\n\n";
const char mqttPrefix[] = "W/signalk/";  // MQTT topic prefix for SignalK data
const char mmsi[] = "123456789";  // MMSI number for the vessel, used in MQTT topics
const char runTimeTopic[] = "/vessels/self/propulsion/main/runTime"; // MQTT topic for engine hours
const char coolantTempTopic[] = "/vessels/self/propulsion/main/coolantTemperature"; // MQTT topic for coolant temperature
const char alternatorVoltageTopic[] = "/vessels/self/propulsion/main/alternatorVoltage"; // MQTT topic for alternator voltage
const char rpmTopic[] = "/vessels/self/propulsion/main/revolutions"; // MQTT topic for Revolutions (Hz)

#define celsiusToKelvin 273.15  // Constant to convert Celsius to Kelvin, if needed in future calculations
#define hoursToSeconds 3600  // Constant to convert hours to seconds, if needed in future calculations
#define rpmToHz 1.0/60.0  // Constant to convert RPM to Hz, if needed in future calculations

//
//   FUNCTION DEFINITIONS
//
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  // Initialize serial communication, WiFi connection, and CAN bus
  Serial.begin(115200);
  delay(50);

  // Connect to WiFi
  setup_wifi();

  //  Initialize CAN bus
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) // Initialize MCP2515 running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.
    Serial.println("MCP2515 Initialized Successfully");
  else
  {
    Serial.println("Error Initializing MCP2515...");
    while (1);
  }
  
  CAN0.setMode(MCP_LISTENONLY);   // Set operation mode to listen only. We don't want to write anything to the VP CAN-Bus
  pinMode(CAN0_INT, INPUT);       // Configuring pin for INT input

  // Initialize MQTT client
  client.setServer(mqtt_broker, mqtt_port);

  delay(200);
}


//
//   MAIN LOOP: read CAN messages and process them
//
void loop() 
{
  long unsigned int PGN;
  unsigned char len = 0;
  unsigned char Data[16];
  double EngineHours, CoolantTemperature, AlternatorVoltage, RPM, Revolutions_Hz, EngineSeconds;
  static int SendSTAT;
  
  if ( Serial.available())      // Dummy to empty input buffer to avoid board to stuck with e.g. NMEA Reader
    Serial.read();
  
  if(!digitalRead(CAN0_INT))                  // If CAN0_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&PGN, &len, Data);        // Read data: len = data length, buf = data byte(s)

    PGN = (PGN>>8)&0xFFFF;                    // get the PGN

    switch(PGN)
    {
      case 61444: RPM = (Data[4] * 256.0 + Data[3] ) / 8.0;               // get revolutions
                  Revolutions_Hz = RPM * rpmToHz;                         // convert RPM to Hz
                  Serial.printf("Revolutions (Hz): %.2f\n", Revolutions_Hz);
                  break;
      case 65253: EngineHours = (Data[0] + Data[1] * 256)/20;             // get engine hours
                  EngineSeconds = EngineHours * hoursToSeconds;           // convert engine hours to seconds
                  SendSTAT |= 1;                                          // set status 'engine hours value is available'
                  Serial.printf("Engine Hours: %.2f\n", EngineHours);
                  break;
      case 65262: CoolantTemperature = Data[0] - 40 + celsiusToKelvin;    // get coolant temperature
                  SendSTAT |= 2;                                          // set status 'coolant temperature value is available'
                  Serial.printf("Coolant Temperature: %.2f\n", CoolantTemperature);
                  break;
      case 65271: AlternatorVoltage = (Data[7] * 256.0 + Data[6]) / 20.0; // get alternator voltage
                  SendSTAT |= 4;                                          // set status 'alternator voltage value is available'
                  Serial.printf("Alternator Voltage: %.2f\n", AlternatorVoltage);
                  break;
    }

    if ( SendSTAT == 0x07 )   // are the three values available
    {
      Serial.printf("Battery: %.2f V  Hours: %.2f  Temperature: %.2f K  Revolutions: %.2f (Hz)\n", AlternatorVoltage, EngineHours, CoolantTemperature, Revolutions_Hz);
      char str[256];
      strcpy(str, mqttPrefix);
      strcat(str, mmsi);  
      strcat(str, runTimeTopic);
      client.publish(str, String(EngineSeconds).c_str(), true);  // Publish engine seconds to MQTT topic
      strcpy(str, mqttPrefix);
      strcat(str, mmsi);
      strcat(str, coolantTemperatureTopic);
      client.publish(str, String(CoolantTemperature).c_str(), true);  // Publish coolant temperature to MQTT topic
      strcpy(str, mqttPrefix);
      strcat(str, mmsi);
      strcat(str, revolutionsTopic);
      client.publish(str, String(Revolutions_Hz).c_str(), true);  // Publish revolutions to MQTT topic
      strcpy(str, ""); 
      CoolantTemperature = AlternatorVoltage = EngineHours = Revolutions_Hz = EngineSeconds = 0.0; 
      SendSTAT = 0;
    }
  }
} 



