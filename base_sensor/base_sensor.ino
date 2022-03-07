/******************************************************************************
  Read basic CO2 and TVOCs

  Marshall Taylor @ SparkFun Electronics
  Nathan Seidle @ SparkFun Electronics

  April 4, 2017

  https://github.com/sparkfun/CCS811_Air_Quality_Breakout
  https://github.com/sparkfun/SparkFun_CCS811_Arduino_Library

  Read the TVOC and CO2 values from the SparkFun CSS811 breakout board

  A new sensor requires at 48-burn in. Once burned in a sensor requires
  20 minutes of run in before readings are considered good.

  Hardware Connections (Breakoutboard to Arduino):
  3.3V to 3.3V pin
  GND to GND pin
  SDA to A4
  SCL to A5

******************************************************************************/
#include <Wire.h>

#include "SparkFunCCS811.h" //Click here to get the library: http://librarymanager/All#SparkFun_CCS811
#include "SparkFunBME280.h"
#include "SparkFun_Qwiic_OpenLog_Arduino_Library.h"

#define CCS811_ADDR 0x5B //Default I2C Address
//#define CCS811_ADDR 0x5A //Alternate I2C Address

CCS811 myCCS811(CCS811_ADDR);
BME280 myBME280;
OpenLog myLog; //Create instance
float tempCorrectionC = 0;
float tempCorrectionF = 0;
boolean correctionSet = false;
float startTempC;
float startTempF;

// How long to wait for temperature stabilization for correction term, normally 20
float delayMinutes = 20;

void setup()
{
  Serial.begin(115200);

  Wire.begin(); //Initialize I2C
  myLog.begin(); //Open connection to OpenLog (no pun intended)

  Serial.println("OpenLog Write File Test");
  
  if (myCCS811.begin() == false)
  {
    Serial.print("CCS811 error. Please check wiring. Freezing...");
    while (1)
      ;
  }

  //This begins the CCS811 sensor and prints error status of .beginWithStatus()
  CCS811Core::CCS811_Status_e returnCode = myCCS811.beginWithStatus();
  Serial.print("CCS811 begin exited with: ");
  //Pass the error code to a function to print the results
  Serial.print(myCCS811.statusString(returnCode));
  Serial.println();

  myCCS811.setRefResistance(9950);

  //Initialize BME280
  if (myBME280.beginI2C() == false) //Begin communication over I2C
  {
    Serial.println("The sensor did not respond. Please check wiring.");
    while(1); //Freeze
  }

  myBME280.settings.runMode = 3; //Normal mode
  myBME280.settings.tStandby = 0;
  myBME280.settings.filter = 4;
  myBME280.settings.tempOverSample = 5;
  myBME280.settings.pressOverSample = 5;
  myBME280.settings.humidOverSample = 5;

  
  delay(10);  //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
  byte id = myBME280.begin(); //Returns ID of 0x60 if successful
  if (id != 0x60)
  {
    Serial.println("Problem with BME280");
  }
  else
  {
    startTempC = myBME280.readTempC();
    startTempF = myBME280.readTempF();
    Serial.print("BME280 online Initial temperature(C):" );
    Serial.println(startTempC);
  }
}

void loop()
{
  //Check to see if data is ready with .dataAvailable()
  if (myCCS811.dataAvailable())
  {

    String outputString ="";
    //If so, have the sensor read and calculate the results.
    //Get them later
    myCCS811.readAlgorithmResults();

    float BMEtempC = myBME280.readTempC();
    float BMEtempF = myBME280.readTempF();
    float BMEhumid = myBME280.readFloatHumidity();

    if (!correctionSet && millis() > delayMinutes * 60 * 1000 ){
      tempCorrectionC = BMEtempC-startTempC;
      tempCorrectionF = BMEtempF-startTempF;
      outputString += "Found Temperature correction of ";
      outputString += tempCorrectionC; 
      outputString += " at ms ";
      outputString += millis();
      outputString += "\n";
      correctionSet = true;
    } 
    
    //This sends the temperature data to the CCS811
    myCCS811.setEnvironmentalData(BMEhumid, BMEtempC-tempCorrectionC);
    
    outputString +=("raw temp:\t");
    outputString +=(BMEtempC);
    outputString +=("C");

    outputString +=("\t");
    outputString +=(BMEtempF);
    outputString +=("F\t");

   outputString +=("corrected temp\t");
    outputString +=(BMEtempC-tempCorrectionC);
    outputString +=("C");

    outputString +=("\t");
    outputString +=(BMEtempF-tempCorrectionF);
    outputString +=("F\t");

    outputString +=("humid\t");
    outputString +=(BMEhumid);
    outputString +=("%\t");

    outputString +=("pressure\t");
    outputString +=(myBME280.readFloatPressure()/100); // convert to mbar
    outputString +=("mbar\t");
  
    outputString +=" CO2\t";
    //Returns calculated CO2 reading
    outputString +=myCCS811.getCO2();
    outputString +="ppm\t tVOC\t";
    //Returns calculated TVOC reading
    outputString +=myCCS811.getTVOC();
    outputString +="ppb\t";
    outputString +=  printRunTime();
    Serial.println(outputString);
    myLog.println(outputString);
    myLog.syncFile();
  }

  delay(5000); //Don't spam the I2C bus
}


//Prints the amount of time the board has been running
//Does the hour, minute, and second calcs
String printRunTime()
{
  char buffer[50];

  unsigned long runTime = millis();

  int hours = runTime / (60 * 60 * 1000L);
  runTime %= (60 * 60 * 1000L);
  int minutes = runTime / (60 * 1000L);
  runTime %= (60 * 1000L);
  int seconds = runTime / 1000L;

  sprintf(buffer, "RunTime[%02d:%02d:%02d]", hours, minutes, seconds);
  
  String output = buffer;
  if (hours == 0 && minutes < delayMinutes)
    output += ("\tNot yet valid");

 return output;
}

//printSensorError gets, clears, then prints the errors
//saved within the error register.
void printSensorError()
{
  uint8_t error = myCCS811.getErrorRegister();

  if (error == 0xFF) //comm error
  {
    Serial.println("Failed to get ERROR_ID register.");
  }
  else
  {
    Serial.print("Error: ");
    if (error & 1 << 5)
      Serial.print("HeaterSupply");
    if (error & 1 << 4)
      Serial.print("HeaterFault");
    if (error & 1 << 3)
      Serial.print("MaxResistance");
    if (error & 1 << 2)
      Serial.print("MeasModeInvalid");
    if (error & 1 << 1)
      Serial.print("ReadRegInvalid");
    if (error & 1 << 0)
      Serial.print("MsgInvalid");
    Serial.println();
  }
}
