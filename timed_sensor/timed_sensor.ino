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
#include "SparkFun_RV1805.h"

#define CCS811_ADDR 0x5B //Default I2C Address
//#define CCS811_ADDR 0x5A //Alternate I2C Address

CCS811 myCCS811(CCS811_ADDR);
BME280 myBME280;
OpenLog myLog; //Create instance
RV1805 rtc;
OpenLog dataLog;

float tempCorrectionC = 0;
float tempCorrectionF = 0;
boolean correctionSet = false;
float startTempC;
float startTempF;

// How long to wait for temperature stabilization for correction term, normally 20
float delayMinutes = 20;

// Name of calibration timestamp file
String calibrationTimeStampFile = "calstamp.txt";

void setup()
{
  Serial.begin(115200);

  Wire.begin(); //Initialize I2C
  myLog.begin(); //Open connection to OpenLog (no pun intended)

  Serial.println("OpenLog Write File Test");
  
 

  // initialize clock
   if (rtc.begin() == false) {
    Serial.println("Something went wrong, check wiring");
  }

  //Use the time from the Arduino compiler (build time) to set the RTC
  //Keep in mind that Arduino does not get the new compiler time every time it compiles.
  //to ensure the proper time is loaded, open up a fresh version of the IDE and load the sketch.
  if (rtc.setToCompilerTime() == false) {
    Serial.println("Something went wrong setting the time");
  }
  
  // set up environment sensor
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

    // read calibration start, timestamp, and last run time from disc

    // if last run time > 
    startTempC = myBME280.readTempC();
    startTempF = myBME280.readTempF();
    Serial.print("BME280 online Initial temperature(C):" );
    Serial.println(startTempC);
  }

  
}

void loop()
{
  if (rtc.updateTime() == false) //Updates the time variables from RTC
  {
    Serial.print("RTC failed to update");
  }
  String currentTime = rtc.stringTime();

  // if the current logging csv file does not exist, intitalize it with the header row
  String currentLogFileName = getCurrentLogFileName();
  //Get size of file
  long sizeOfFile = myLog.size(currentLogFileName);

  if (sizeOfFile == -1){
    Serial.print(currentTime);
    Serial.print("\tInitializing logfile: ");
    Serial.println(currentLogFileName);
    myLog.append(currentLogFileName);
    myLog.println("TempC,TempF,Humid%,Corr TempC,Corr TempF,CO2 ppm,TVOC ppb,RunTime,Validity");
  }
  

  
  //Check to see if data is ready with .dataAvailable()
  if (myCCS811.dataAvailable())
  {

    
    
    String outputString ="Time,";
    String dataLogString ="";
    outputString += currentTime;
    dataLogString += currentTime;
    dataLogString += ",";
    
    //If so, have the sensor read and calculate the results.
    //Get them later
    myCCS811.readAlgorithmResults();

    float BMEtempC = myBME280.readTempC();
    float BMEtempF = myBME280.readTempF();
    float BMEhumid = myBME280.readFloatHumidity();

    dataLogString += BMEtempC;
    dataLogString += "\t";
    dataLogString += BMEtempF;
    dataLogString += "\t";
    dataLogString += BMEhumid;
    dataLogString += "\t";

    
    if (!correctionSet && millis() > delayMinutes * 60 * 1000 ){
      tempCorrectionC = BMEtempC-startTempC;
      tempCorrectionF = BMEtempF-startTempF;
      outputString += "Found Temperature correction of ";
      outputString += tempCorrectionC; 
      outputString += " at time";
      outputString += currentTime;
      outputString += "\n";
      correctionSet = true;
    } 

    dataLogString += BMEtempC-tempCorrectionC;
    dataLogString += ",";
    dataLogString += BMEtempF-tempCorrectionF;
    dataLogString += ",";
    //This sends the temperature data to the CCS811
    myCCS811.setEnvironmentalData(BMEhumid, BMEtempC-tempCorrectionC);
    
    outputString +=("\traw temp:\t");
    outputString +=(BMEtempC);
    outputString +=("C");

    outputString +=("\t");
    outputString +=(BMEtempF);
    outputString +=("F,");

    outputString +=("corrected temp\t");
    outputString +=(BMEtempC-tempCorrectionC);
    outputString +=("C");

    outputString +=("\t");
    outputString +=(BMEtempF-tempCorrectionF);
    outputString +=("F\t");

    outputString +=("humid\t");
    outputString +=(BMEhumid);
    outputString +=("%\t");

    outputString +=("pressure,");
    float BMEmbar = myBME280.readFloatPressure()/100;
    outputString +=(BMEmbar); // convert to mbar
    outputString +=("mbar,");
    dataLogString += BMEmbar;
    dataLogString += ",";
  
    outputString +=" CO2,";
    //Returns calculated CO2 reading
    outputString +=myCCS811.getCO2();
    dataLogString += myCCS811.getCO2();
    dataLogString += ",";
    
    outputString +="ppm\t tVOC\t";
    //Returns calculated TVOC reading
    outputString +=myCCS811.getTVOC();
    dataLogString += myCCS811.getTVOC();
    dataLogString += ",";
    outputString +="ppb\t";
    outputString +=  printRunTime();
    dataLogString += printRunTime();
    Serial.println(outputString);
    myLog.append(currentLogFileName);
    myLog.println(dataLogString);
    myLog.syncFile();

    // write out the latest calibration time and value, so we can reuse at restart
    if (calibrationSet) {
      if (myLog.size(calibrationTimeStampFile) > -1) {
        myLog.removeFile(calibrationTimeStampFile);
      }
      myLog.append(calibrationTimeStampFile)
      String cal_output;
      sprintf(cal_output, "%02d %02d %02d %02d %02d %06f %06f", 
              rtc.getYear(),rtc.getMonth(),
              rtc.getDate(),rtc.getHours(),
              tempCorrectionC,tempCorrectionF);
      myLog.println(cal_output);
      Serial.print("Wrote calibration file: ")
      Serial.println(cal_output);
    }      
  }

  delay(5000); //Don't spam the I2C bus
}

//Prints the amount of time the board has been running
//Does the hour, minute, and second calcs
String getCurrentLogFileName()
{
  char buffer[50];

  int hours = rtc.getHours();
  int day = rtc.getDate();
  int month = rtc.getMonth();
  int year = rtc.getYear();

  sprintf(buffer, "B%02d%02d%02d_%02d.csv", year, month,day,hours);
  
  String output = buffer;
 
 return output;
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

  sprintf(buffer, "[%02d:%02d:%02d]", hours, minutes, seconds);
  
  String output = buffer;
  if (hours == 0 && minutes < delayMinutes)
    output += (",Not yet valid");

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
