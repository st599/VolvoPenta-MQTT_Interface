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
//   Volvo Penta CANbus to SignalK via MQTT Gateway
//
//   Filename        :   vp_canbus_to_n2k.py
//   Description     :   Sends boat engine data to MQTT Gateway
//   Date            :   22/05/2026
//   Author          :   Simon Thompson
//   Copyright       :   Simon Thompson 2026
//                   :   Buhhe 2021
//   Dependencies    :   Arduino IDE, ESP32 board support, MCP_CAN library, SPI library
//   License         :   GNU Lesser General Public License v2.1 or later
//   Repository      :   https://github.com/st599/VolvoPenta-MQTT_Interface 
//   Based on        :   This code is based on the work of Buhhe and has been modified to 
//                       read specific J1939 data from the Volvo Penta CANbus and send it to a SignalK server. 
//                       The original code can be found at https://github.com/buhhe/VolvoPenta-N2K_Interface.

//
//   SYSTEM IMPORTS
//
#include <Arduino.h>
#include <Preferences.h>
#include <mcp_can.h>
#include <SPI.h>


//
//  AUTHOR IMPORTS
//

//
//  GLOBAL CONSTANTS
//
#define CAN0_INT 17                            
MCP_CAN CAN0(5); 

const char Description[] = "Description: Volvo Penta->N2K interface. Read J1939 data from VP canbus, \nconvert it and send it to a SignalK server.\n\n";
const char mqttPrefix[] = "W/signalk/";  // MQTT topic prefix for SignalK data
const char runTimeTopic[] = "/vessels/self/propulsion/main/runTime";
const char coolantTempTopic[] = "/vessels/self/propulsion/main/coolantTemperature";
const char alternatorVoltageTopic[] = "/vessels/self/propulsion/main/alternatorVoltage";
const char rpmTopic[] = "/vessels/self/propulsion/main/revolutions";




//
//   FUNCTION DEFINITIONS
//
void setup() {
  uint32_t id = 0, i;

  Serial.begin(115200);
  delay(50);

  SayHello();  // print some useful information to USB-serial

  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK) // Initialize MCP2515 running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.
    Serial.println("MCP2515 Initialized Successfully");
  else
  {
    Serial.println("Error Initializing MCP2515...");
    while (1);
  }
  
  CAN0.setMode(MCP_LISTENONLY);   // Set operation mode to listen only. We don't want to write anything to the VP CAN-Bus
  pinMode(CAN0_INT, INPUT);       // Configuring pin for INT input



  preferences.begin("nvs", false);                          // Open nonvolatile storage (nvs)
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
  double EngineHours,CoolantTemperature, AlternatorVoltage, RPM, Revolutions_Hz;
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
                  Revolutions_Hz = RPM / 60.0;                            // convert RPM to Hz
                  Serial.printf("Revolutions (Hz): %.2f\n", Revolutions_Hz);
                  break;
      case 65253: EngineHours = (Data[0] + Data[1] * 256)/20;             // get engine hours
                  SendSTAT |= 1;                                          // set status 'engine hours value is available'
                  Serial.printf("Engine Hours: %.2f\n", EngineHours);
                  break;
      case 65262: CoolantTemperature = Data[0] - 40;                      // get coolant temperature
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
      Serial.printf("Battery:%.2f Hours: %.2f Temperature: %.2f Revolutions (Hz): %.2f\n", AlternatorVoltage, EngineHours, CoolantTemperature, Revolutions_Hz);
      CoolantTemperature = AlternatorVoltage = EngineHours = Revolutions_Hz = 0.0; 
      SendSTAT = 0;
    }
  }
} 



