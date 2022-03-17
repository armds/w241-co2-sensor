/******************************************************************************
  Reads information from an evironmental sensor and writes an hourly csv file
  with environmental data.

  Boards expected:

  - Sparkfun Qwiic Pro Micro USB-C
  - Sparkfun CCS811/BME280 (Qwiic) Environmental Combo Breakout
  - Sparkfun Qwiic OpenLog
  - SparkFun Real Time Clock Module

  Libraries required:
   http://librarymanager/All#SparkFun_CCS811
   http://librarymanager/All#SparkFunBME280
   https://www.arduinolibraries.info/libraries/spark-fun-qwiic-open-log
   http://librarymanager/All#SparkFun_RV1805

 Author: Anne Marshal 
 Copyright: 2022
******************************************************************************/
#include <Wire.h>

#include "SparkFunCCS811.h" 
#include "SparkFunBME280.h"
#include "SparkFun_Qwiic_OpenLog_Arduino_Library.h"
#include "SparkFun_RV1805.h"

#define CCS811_ADDR 0x5B //Default I2C Address for the sensor
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

// sleep tolerance, how old a calibration use stamp can be for us to still use it.
float tolMinutes = 10;

// Name of calibration timestamp file
String calibrationTimeStampFile = "calstamp.txt";

// set this to either B or W depending on which sensor is being written to
// is only used for output CSV filenames
char sensorId = 'P';

// if we need to set the clock chip, this will set it to the time the IDE started
// then add 3 hours to adjust to EST time.  Don't do this after 9pm thought, 'cause its not smart!
// unless the chip has crashed for a while, it should seldom need to have the clock set.
boolean setClock = false;

// desired interval between samples in seconds
unsigned int secsBetweenSamples = 5;

// interval between calibration file writes
unsigned int secsBetweenCalibWrites = 120;


// string buffers like to be global, because memory is a thing
char fCorr[10];
char cCorr[10];
char calibuf[30];
char calibuf2[30];     

// Globalize frequently used vars for faster loops
unsigned int loopStartTime;
String currentTime;

   
void setup()
{
  Serial.begin(9600);

  Wire.begin(); //Initialize I2C
  myLog.begin(); //Open connection to OpenLog (no pun intended)

  // initialize clock
   if (rtc.begin() == false) {
    Serial.println("Something went wrong, check wiring");
  }

  //Use the time from the Arduino compiler (build time) to set the RTC
  //Keep in mind that Arduino does not get the new compiler time every time it compiles.
  //to ensure the proper time is loaded, open up a fresh version of the IDE and load the sketch.
  rtc.set24Hour();
  if (setClock) {
    if (rtc.setToCompilerTime() == false) {
      Serial.println("Something went wrong setting the time");
    }

    rtc.updateTime();
    // correct to put it in EST not PST
    int hrs = rtc.getHours();
    int day = rtc.getDate();
    int EST = hrs+3;
    if (EST > 23) {
      EST = EST - 24;
      day = day +1;
    }
    rtc.setHours(EST);
    rtc.setDate(day);
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
   // if last run time > 
    startTempC = myBME280.readTempC();
    startTempF = myBME280.readTempF();
    Serial.print("BME280 online Initial temperature(C):" );
    Serial.println(startTempC);
  }
}

void loop()
{
  loopStartTime = millis();
  
  if (rtc.updateTime() == false) //Updates the time variables from RTC
  {
    Serial.print("RTC failed to update");
  }
  currentTime = rtc.stringTime();

  // if the current logging csv file does not exist, intitalize it with the header row
  String currentLogFileName = getCurrentLogFileName();
  if (myLog.size(currentLogFileName) <1){
    Serial.print(currentTime);
    Serial.print("\tInitializing logfile: ");
    Serial.println(currentLogFileName);
    myLog.append(currentLogFileName);
    myLog.println("Time,TempC,TempF,Humid%,Corr TempC,Corr TempF,Press mbar,CO2 ppm,TVOC ppb,RunTime,Validity");
    myLog.syncFile();
  }

  //Check to see if environmental data is ready with .dataAvailable()
  if (myCCS811.dataAvailable())
  {

    // outputString is the log message you'll see on the monitor
    String outputString ="";

    //dataLogString is the comma delimited data written to the hourly csv files
    String dataLogString ="";
    
    //read the sensor and calculate the results.
    myCCS811.readAlgorithmResults();

    float BMEtempC = myBME280.readTempC();
    float BMEtempF = myBME280.readTempF();
    float BMEhumid = myBME280.readFloatHumidity();

    //once we have the base read, we can work out if we have the correction parameters we need yet
    if (!correctionSet) {
      // we've waited long enough, and not picked it up from a file so... do the calculation
      if( millis() > delayMinutes * 60 * 1000 ){
        // calculate the temperature differential of the chip warming up.  Device should be held
        // and environment with a stable temperature for the first delayMinutes from power up.
        // delay of 20 minutes recommended
        tempCorrectionC = BMEtempC-startTempC;
        tempCorrectionF = BMEtempF-startTempF;
        outputString += "Found Temperature correction of ";
        outputString += tempCorrectionC; 
        outputString += " at time ";
        outputString += currentTime;
        outputString += "\n";
        correctionSet = true;
      } 
      else if ( myLog.size(calibrationTimeStampFile) > -1){
        // check for logged calibration, if recent enough will be used for corrections
        myLog.read(calibuf,30,calibrationTimeStampFile);

        // arudino C is limited, so we need to deal with the input as string buffers :(
        char buff[2];
        //year
        buff[0] = calibuf[0];
        buff[1] = calibuf[1];
        int caliyear = atoi(buff);

        //month
        buff[0] = calibuf[3];
        buff[1] = calibuf[4];
        int calimonth = atoi(buff);
        
        //day
        buff[0] = calibuf[6];
        buff[1] = calibuf[7];
        int caliday = atoi(buff);
        //hour
        buff[0] = calibuf[9];
        buff[1] = calibuf[10];
        int calihour = atoi(buff);
        //minute
        buff[0] = calibuf[12];
        buff[1] = calibuf[13];
        int calimin = atoi(buff);  

        // and it REALLY can't deal with floats.  I appologize to any reader that this is what it 
        // is apparently adruino thinks this is best?
        
        char fbuff[6];
        //cCalibration
        fbuff[0] = calibuf[15];
        fbuff[1] = calibuf[16];
        fbuff[2] = calibuf[17];
        fbuff[3] = calibuf[18];
        fbuff[4] = calibuf[19];
        fbuff[5] = calibuf[20];
        fbuff[6] = calibuf[21];
        fbuff[7] = calibuf[22];
        fbuff[8] = ' ';
        fbuff[9] = ' ';
        float caliC = atof(fbuff);

        //cCalibration
        fbuff[0] = calibuf[24];
        fbuff[1] = calibuf[25];
        fbuff[2] = calibuf[26];
        fbuff[3] = calibuf[27];
        fbuff[4] = calibuf[28];
        fbuff[5] = calibuf[29];
        fbuff[6] = calibuf[30];
        fbuff[7] = calibuf[31];
        fbuff[8] = ' ';
        fbuff[9] = ' ';
        float caliF = atof(fbuff);

        if (caliyear == rtc.getYear() &&
            calimonth == rtc.getMonth() &&
            caliday == rtc.getDate() &&
            ((calihour == rtc.getHours() && (rtc.getMinutes() - calimin < tolMinutes)) ||
              (calihour +1 == rtc.getHours() && (rtc.getMinutes()+60 - calimin < tolMinutes)))) {
                sprintf(calibuf2, "%02d %02d %02d %02d %02d ", 
                        caliyear,calimonth,
                        caliday,calihour, calimin);
                Serial.print("Reusing calibration from file: ");
                Serial.print(calibuf2);
                Serial.print(caliC);
                Serial.print(" ");
                Serial.println(caliF);
                tempCorrectionC = caliC;
                tempCorrectionF = caliF;
                correctionSet = true;
        } 
      }
    }
   
    dataLogString += currentTime;
    dataLogString += ",";
    dataLogString += BMEtempC;
    dataLogString += ",";
    dataLogString += BMEtempF;
    dataLogString += ",";
    dataLogString += BMEhumid;
    dataLogString += ",";
    dataLogString += BMEtempC-tempCorrectionC;
    dataLogString += ",";
    dataLogString += BMEtempF-tempCorrectionF;
    dataLogString += ",";
    
    //This sends the temperature data to the CCS811 to calibrate the sensor readings
    myCCS811.setEnvironmentalData(BMEhumid, BMEtempC-tempCorrectionC);
    
    outputString += "Time\t";
    outputString += currentTime;

    
    outputString +=("\tcorrected temp\t");
    outputString +=(BMEtempC-tempCorrectionC);
    outputString +=("C");

    outputString +=("\t");
    outputString +=(BMEtempF-tempCorrectionF);
    outputString +=("F\t");
    
    outputString +=" CO2\t";
    //Returns calculated CO2 reading
    outputString +=myCCS811.getCO2();
    
    dataLogString += myCCS811.getCO2();
    dataLogString += ",";
    
    outputString +="ppm\t tVOC\t";
    //Returns calculated TVOC reading
    outputString += myCCS811.getTVOC();
    outputString += "ppb\t";
    outputString += printRunTime(loopStartTime);
    
    dataLogString += myCCS811.getTVOC();
    dataLogString += ",";  
    dataLogString += printRunTime(loopStartTime);
   
    Serial.println(outputString);
    myLog.append(currentLogFileName);
    myLog.println(dataLogString);
    myLog.syncFile();
          
    // write out the latest calibration time and value, so we can reuse at restart
    // only do this once a minute
    /*Serial.print(loopStartTime % (secsBetweenCalibWrites *1000));
    Serial.print(" vs ");
    Serial.println(secsBetweenSamples*1000);
    */
    if (correctionSet & (loopStartTime % (secsBetweenCalibWrites *1000) <= secsBetweenSamples*1000)) {
      if (myLog.size(calibrationTimeStampFile) > -1) {
        myLog.removeFile(calibrationTimeStampFile);
      }
      myLog.append(calibrationTimeStampFile);

    // arudino sprintf can't do floats because ¯\_(ツ)_/¯
      dtostrf(tempCorrectionC,8,1,cCorr);
      dtostrf(tempCorrectionF,8,1,fCorr);

      sprintf(calibuf, "%02d %02d %02d %02d %02d %s %s", 
              rtc.getYear(),rtc.getMonth(),
              rtc.getDate(),rtc.getHours(),rtc.getMinutes(),
              cCorr, fCorr);
      myLog.println(calibuf);
      myLog.syncFile();
      Serial.print("Wrote calibration file: ");
      Serial.println(calibuf);
    }      

  }

  unsigned int delayTime = (secsBetweenSamples*1000 - (millis() - loopStartTime)); // account for how long we took to process
  if (delayTime > 0) {
    delay(delayTime); //Snooze between samples, should have this at least 1 sec to not spam the I2C bus
  }
}

//Generates the output filename based on how long we've been running.
String getCurrentLogFileName()
{
  char buffer[50];

  int hours = rtc.getHours();
  int day = rtc.getDate();
  int month = rtc.getMonth();
  int year = rtc.getYear();

  sprintf(buffer, "%c%02d%02d%02d_%02d.csv", sensorId, year, month,day,hours);
  
  String output = buffer;
 
 return output;
}

//Prints the amount of time the board has been running
//Does the hour, minute, and second calcs
String printRunTime(unsigned long runTime)
{
  char buffer[50];


  int hours = runTime / (60 * 60 * 1000L);
  runTime %= (60 * 60 * 1000L);
  int minutes = runTime / (60 * 1000L);
  runTime %= (60 * 1000L);
  int seconds = runTime / 1000L;

  sprintf(buffer, "[%02d:%02d:%02d]", hours, minutes, seconds);
  
  String output = buffer;
  if (! correctionSet)
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
