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

// This Sketch is based on code by  Timo Lappalainen https://github.com/ttlappalainen
//                                  Andreas Koritnik https://github.com/AK-Homberger
//                                  Cory J. Fowler   https://github.com/coryjfowler
//                                    
//                                  
//                                      
// 20211214 first version to read Volvo Penta MDI J1939 messages (RPM, engine hours, coolant temperature, alternator voltage)
// from VP CANBus, convert it and write it to a N2K bus.
//



#define ESP32_CAN_TX_PIN GPIO_NUM_26  // Set CAN TX port to 26  
#define ESP32_CAN_RX_PIN GPIO_NUM_27  // Set CAN RX port to 27

#include <Arduino.h>
#include <Preferences.h>
#include <mcp_can.h>
#include <SPI.h>


// to be printed with some additional information to USB-serial
const char Description[] = "Description: Volvo Penta->N2K interface. Read J1939 data from VP canbus, \nconvert it and send it to a N2K bus.\n\n";

int NodeAddress;            // To store last Node Address
Preferences preferences;    // Nonvolatile storage on ESP32 - To store LastDeviceAddress


const unsigned long TransmitMessages[] PROGMEM = {127488L, 127489L,0}; // Set the information for other bus devices, which messages we support

#define CAN0_INT 17                            
MCP_CAN CAN0(5);                               

// forward declarations
void          SayHello(void);
void          CheckSourceAddressChange(void);


//*****************************************************************************
void setup() {
  uint8_t chipid[6];
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


//*****************************************************************************
void loop() 
{
  long unsigned int PGN;
  unsigned char len = 0;
  unsigned char Data[16];
  double EngineHours,CoolantTemperature, AlternatorVoltage, RPM;
  static int SendSTAT;
  
  if ( Serial.available())      // Dummy to empty input buffer to avoid board to stuck with e.g. NMEA Reader
    Serial.read();

  if(!digitalRead(CAN0_INT))                  // If CAN0_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&PGN, &len, Data);      // Read data: len = data length, buf = data byte(s)

    PGN = (PGN>>8)&0xFFFF;                // get the PGN

    switch(PGN)
    {
      case 61444: RPM = (Data[4] * 256.0 + Data[3] ) / 8.0;                                           // get revolutions
                  Serial.printf("RPM: %.1f\n", RPM);
                  break;
      case 65253: EngineHours = (Data[0] + Data[1] * 256)/20;             // get engine hours
                  SendSTAT |= 1;                                          // set status 'engine hours value is available'
                  break;
      case 65262: CoolantTemperature = Data[0] - 40;                      // get coolant temperature
                  SendSTAT |= 2;                                          // set status 'coolant temperature value is available'
                  break;
      case 65271: AlternatorVoltage = (Data[7] * 256.0 + Data[6]) / 20.0; // get alternator voltage
                  SendSTAT |= 4;                                          // set status 'alternator voltage value is available'
                  break;
    }

    if ( SendSTAT == 0x07 )   // are the three values available
    {
      Serial.printf("Battery:%.2f Hours: %.2f Temperature: %.2f\n", AlternatorVoltage, EngineHours, CoolantTemperature);
      CoolantTemperature = AlternatorVoltage = EngineHours = 0.0;
      SendSTAT = 0;
    }
  }
} 



