#include <avr/wdt.h>
#ifdef __AVR__
#include <avr/power.h>
#endif
#include <stdlib.h>

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
// #include <ODROID_Si1132.h>
#include "SI7021.h"

//-----------------------------------------------MAIN MYSENSORS CONFIG START
// Enable debug prints to serial monitor
#define MY_DEBUG
//#define MY_DEBUG_VERBOSE_SIGNING


// Enable and select radio type attached
#define MY_RADIO_RFM69

//#define MY_RFM69_FREQUENCY RF69_433MHZ
//#define MY_RFM69_FREQUENCY RF69_868MHZ
//#define MY_RFM69_FREQUENCY RF69_915MHZ



//#define MY_RADIO_RFM95
//#define MY_RFM95_MODEM_CONFIGRUATION  RFM95_BW125CR45SF128
#define MY_RFM95_MODEM_CONFIGRUATION RFM95_BW_500KHZ | RFM95_CODING_RATE_4_5, RFM95_SPREADING_FACTOR_2048CPS | RFM95_RX_PAYLOAD_CRC_ON, RFM95_AGC_AUTO_ON // 
#define MY_RFM95_TX_POWER_DBM (14u)

//#define   MY_RFM95_FREQUENCY RFM95_915MHZ
//#define   MY_RFM95_FREQUENCY RFM95_868MHZ
//#define   MY_RFM95_FREQUENCY RFM95_434MHZ

// Comment it out for CW version radio.
//#define MY_IS_RFM69HW

// Comment it out for Auto Node ID #
#define MY_NODE_ID 6

// Avoid battery drain if Gateway disconnected and the node sends more than MY_TRANSPORT_STATE_RETRIES times message.
#define MY_TRANSPORT_UPLINK_CHECK_DISABLED
#define MY_PARENT_NODE_IS_STATIC
#define MY_PARENT_NODE_ID 0

//Enable OTA feature
//#define MY_OTA_FIRMWARE_FEATURE
//#define MY_OTA_FLASH_JDECID 0//0x2020

// Redefining write codes for JDEC FLASH used in the node
// These two defines should always be after #include declaration
#define SPIFLASH_BLOCKERASE_32K 0xD8
#define SPIFLASH_CHIPERASE 0x60

//Enable Crypto Authentication to secure the node
//#define MY_SIGNING_ATSHA204
//#define MY_SIGNING_REQUEST_SIGNATURES
//-----------------------------------------------MAIN MYSENSORS CONFIG END


BH1750 lightMeter;
//ODROID_Si1132 si1132;
SI7021 SI7021Sensor; //
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_BMP280 bmp;

#include <MySensors.h>

#define RELAY_pin 7 // Digital pin connected to relay

// Assign numbers for all sensors we will report to gateway\controller (they will be created as child devices)
#define BATT_sensor 1
#define HUM_sensor 2
#define TEMP_sensor 3
#define VIS_sensor 4
#define UVIndex_sensor 5
#define Visible_sensor 6
#define IR_sensor 7
#define P_sensor 8
#define M_sensor 9
#define G_sensor 10


#define BATTERY_SENSE_PIN A6 // select the input pin for the battery sense point
#define LED_PIN 6

// Create MyMessage Instance for sending readins from sensors to gateway\controller (they will be created as child devices)
MyMessage msg_temp(TEMP_sensor, V_TEMP);
MyMessage msg_hum(HUM_sensor, V_HUM);
MyMessage msg_vis(VIS_sensor, V_LIGHT_LEVEL);
MyMessage msgBatt(BATT_sensor, V_VOLTAGE);
MyMessage msg_uvi(UVIndex_sensor, V_UV);
MyMessage msg_visible(Visible_sensor, V_LIGHT_LEVEL);
MyMessage msg_ir(IR_sensor, V_LIGHT_LEVEL);
MyMessage msg_p(P_sensor, V_PRESSURE);
MyMessage msg_m(M_sensor, V_STATUS);
MyMessage msg_g(G_sensor, V_LIGHT);


//Si1132
float Si1132UVIndex = 0;
uint32_t Si1132Visible = 0;
uint32_t Si1132IR = 0;

float oldSi1132UVIndex = 0;
uint32_t oldSi1132Visible = 0;
uint32_t oldSi1132IR = 0;

uint16_t oldLux = 0, lux;
static uint16_t oldHumdty = 0, humdty;
static uint16_t oldTemp = 0, temp;


volatile bool flagGyroInt = false;
//#define G_VALUE 16380
//#define G_VALUE2 268304400 //G_VALUE * G_VALUE

void pinsIntEnable()
{
  PCMSK2 |= bit (PCINT20);
  PCIFR  |= bit (PCIF2);   // clear any outstanding interrupts
  PCICR  |= bit (PCIE2);   // enable pin change interrupts for A0 to A1
}

ISR (PCINT2_vect)
{
  flagGyroInt = true;
}


void blink_led() {
  digitalWrite(LED_PIN, HIGH);
  wait(50);
  digitalWrite(LED_PIN, LOW);
}


void battery_report() {
  //---------------BATTERY REPORTING START
  static int oldBatteryPcnt = 0;
  int batteryPcnt;

  // Get the battery Voltage
  int sensorValue = analogRead(BATTERY_SENSE_PIN);
  /*  Devider values R1 = 3M, R2 = 470K divider across batteries
   *  Vsource = Vout * R2 / (R2+R1)   = 7,383 * Vout;
   *  we use internal refference voltage of 1.1 Volts. Means 1023 Analg Input values  = 1.1Volts
   *  5.5 is dead bateries. 6.3 or more - is 100% something in between is working range.
   */

  float voltage = sensorValue*0.001074*7.38255 ;
  if (voltage > 6.3)  batteryPcnt = 100;
  else if (voltage < 5.5) batteryPcnt = 0;
  else batteryPcnt = (int)((voltage - 5.5) / 0.008) ;

  Serial.print(F("voltage ")); Serial.println(voltage);

  if (oldBatteryPcnt != batteryPcnt ) {
    sendBatteryLevel(batteryPcnt);
    oldBatteryPcnt = batteryPcnt;
  }
  
  if (oldBatteryPcnt != batteryPcnt ) {
    // Power up radio after sleep
    sendBatteryLevel(batteryPcnt);
    send(msgBatt.set(voltage, 2));
    oldBatteryPcnt = batteryPcnt;
  }
  //------------------BATTERY REPORTING END
}

void light_report() {
  //------------------LIGHT METER BEGIN
  char luxStr[10];

  //lightMeter.begin();
  //lightMeter.begin(BH1750_ONE_TIME_HIGH_RES_MODE); // need for correct wake up
  lightMeter.begin(BH1750::ONE_TIME_LOW_RES_MODE);

  //delay(120); //120ms to wake up BH1750 according the datasheet
  lux = lightMeter.readLightLevel();// Get Lux value
  // dtostrf(); converts float into string
  long luxDelta = (long)(lux - oldLux);
  Serial.print(F("LUX=")); Serial.println(lux);
  dtostrf(lux, 5, 0, luxStr);
  if ( abs(luxDelta) > 50 ) {
    send(msg_vis.set(luxStr), true); // Send LIGHT BH1750 sensor readings
    oldLux = lux;
  }
  wait(5);
  //-----------------LIGHT METER END
}

void temphum_report() {
  //-----------------TEMP&HUM METER BEGIN
  char humiditySi7021[10];
  char tempSi7021[10];

  si7021_env data = SI7021Sensor.getHumidityAndTemperature();

  // Measure Relative Humidity from the Si7021
  humdty = data.humidityBasisPoints;
  dtostrf(humdty, 4, 1, humiditySi7021);
  Serial.print(F("HUM:")); Serial.println(humiditySi7021);
  if (humdty != oldHumdty) {
    send(msg_hum.set(humiditySi7021), true); // Send humiditySi7021 sensor readings
    oldHumdty = humdty;
  }

  wait(5);

  // Measure Temperature from the Si7021
  // Temperature is measured every time RH is requested.
  // It is faster, therefore, to read it from previous RH
  // measurement with getTemp() instead with readTemp()
  temp = data.celsiusHundredths / 100.0;
  dtostrf(temp, 4, 1, tempSi7021);
  Serial.print(F("T:")); Serial.println(tempSi7021);
  if (temp != oldTemp) {
    send(msg_temp.set(tempSi7021), true); // Send tempSi7021 temp sensor readings
    oldTemp = temp;
  }
  wait(5);
  //----------------TEMP&HUM METER END
}

void pressure_report() {
  //----------------PREASURE METER START
  //bmp.wakeup();
  //Serial.print(F("Temperature = "));
  //Serial.print(bmp.readTemperature());
  //Serial.println(" *C");

  Serial.print(F("Pressure = "));
  Serial.print(bmp.readPressure());
  Serial.println(" Pa");

  //Serial.print(F("Approx altitude = "));
  //Serial.print(bmp.readAltitude(1013.25)); // this should be adjusted to your local forcase
  //Serial.println(" m");

  //Serial.println();

  //bmp.sleep();
  wait(5);
  //----------------PREASURE METER END
}

/*
  void lightextra_report()
  {
  char UVI_Si1132[10];
  char VIS_Si1132[10];
  char IR_Si1132[10];
  //Serial.print(F("UV")); Serial.println(si1132.readUV());
  //Serial.print(F("VIS"));Serial.println(si1132.readVisible());
  //Serial.print(F("IR"));Serial.println(Si1132IR);
  Si1132UVIndex = 0;
  Si1132Visible = 0;
  Si1132IR = 0;
  for (int i = 0; i < 10; i++) {
    Si1132Visible += si1132.readVisible();
    //if (Si1132Visible > 4000000) Si1132Visible = 0;
    Si1132IR += si1132.readIR();
    Si1132UVIndex += si1132.readUV();
  }
  Si1132UVIndex /= 10;
  Si1132UVIndex /= 100;
  Si1132Visible /= 10;
  Si1132IR /= 10;
  Serial.print(F("UV=")); Serial.println(Si1132UVIndex);
  Serial.print(F("VIS="));Serial.println(Si1132Visible);
  Serial.print(F("IR="));Serial.println(si1132.readIR());
  long uvIndexDelta = (long)(Si1132UVIndex - oldSi1132UVIndex);
  //Serial.print(F("abs(UVIndex - old UVindex)=")); Serial.print(abs(z)); Serial.print(F("; UVIndex=")); Serial.print(Si1132UVIndex); Serial.print(F("; old UVIndex=")); Serial.println(oldSi1132UVIndex);
  dtostrf(Si1132UVIndex, 4, 2, UVI_Si1132);
  if ( abs(uvIndexDelta) > 0.1 ) {
    send (msg_uvi.set(UVI_Si1132), true);
    oldSi1132UVIndex = Si1132UVIndex;
  }
  wait(5);
  long visDelta = (long)(Si1132Visible - oldSi1132Visible);
  //Serial.print(F("abs(VisibleSi1132 - old)=")); Serial.print(abs(y)); Serial.print(F("; VisibleSi1132=")); Serial.print(Si1132Visible); Serial.print(F("; old VisibleSi1132=")); Serial.println(oldSi1132Visible);
  dtostrf(Si1132Visible, 5, 0, VIS_Si1132);
  if ( abs(visDelta) > 50) {
    send(msg_visible.set(VIS_Si1132), true);
    oldSi1132Visible = Si1132Visible;
  }
  wait(5);
  long irDelta = (long)(Si1132IR - oldSi1132IR);
  //Serial.print(F("abs(IRSi1132 - old)=")); Serial.print(abs(x)); Serial.print(F("; IRSi1132=")); Serial.print(Si1132IR); Serial.print(F("; old IRSi1132 =")); Serial.println(oldSi1132IR);
  dtostrf(Si1132IR, 5, 0, IR_Si1132);
  if (abs(irDelta) > 50) {
    send(msg_ir.set(IR_Si1132), true);
    oldSi1132IR = Si1132IR;
  }
  wait(5);
  //send (msg_ir.set(Si1132IR));
  }
*/
void swarm_report()
{
  battery_report();
  light_report();
  temphum_report();
  //lightextra_report();
  pressure_report();
}

void before() {
   // This section  needs to be execuded if JDEC EPROM needs to be acceses in the loop()
  //noInterrupts();
  //_flash.initialize();
  //_flash.sleep();
  //interrupts();

  analogReference(INTERNAL); // using internal ADC ref of 1.1V

  //No need watch dog enabled in case of battery power.
  //wdt_enable(WDTO_4S);
  wdt_disable();
  
  //lightMeter.begin(BH1750_ONE_1TIME_HIGH_RES_MODE);
  lightMeter.begin();
  //si1132.begin();
  SI7021Sensor.begin();

  //bmp280
  if (!bmp.begin()) {
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));

  } else {
    //bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
    bmp.setSampling(Adafruit_BMP280::MODE_SLEEP,     /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  }


  /*
    if (!bmp.begin()) {
      Serial.println(F("bmp fail"));
      while (1);
    }
  */


  pinMode(LED_PIN, OUTPUT);
  pinsIntEnable();


  if (! lis.begin(0x19)) {   // change this to 0x19 for alternative i2c address
    Serial.println("Couldnt start");
  } else {
    Serial.println("LIS3DH found!");
    lis.setRange(LIS3DH_RANGE_2_G);
    lis.setDataRate(LIS3DH_DATARATE_10_HZ);
    lis.setClick(1, 20);
  }

}

void setup() {

}

void presentation()
{
  // Send the sketch version information to the gateway and Controller
  sendSketchInfo("ButtonSize node", "3.0");

  // Register all sensors to gw (they will be created as child devices)
  present(TEMP_sensor, S_TEMP);
  present(HUM_sensor, S_HUM);
  present(VIS_sensor, S_LIGHT_LEVEL);
  present(BATT_sensor, S_MULTIMETER);
  present(UVIndex_sensor, S_UV);
  present(Visible_sensor, S_LIGHT_LEVEL);
  present(IR_sensor, S_LIGHT_LEVEL);
  present(P_sensor, S_BARO);
  present(M_sensor, S_BINARY);
  present(G_sensor, S_CUSTOM);
}

void loop()
{

  //No need watch dog in case of battery power.
  //wdt_reset();

  // This section  needs to be execuded if JDEC EPROM needs to be acceses in the loop()
  //noInterrupts();
  //_flash.initialize();
  //_flash.wakeup();
  //interrupts();

  

  lis.read();
  Serial.print("X:  "); Serial.print(lis.x);
  Serial.print("  \tY:  "); Serial.print(lis.y);
  Serial.print("  \tZ:  "); Serial.println(lis.z);



  swarm_report();

   // This section  needs to be execuded if JDEC EPROM needs to be acceses in the loop()
  //noInterrupts();
  //_flash.sleep();
  //interrupts();

  // Go sleep for some milliseconds
  sleep(20000);
}
