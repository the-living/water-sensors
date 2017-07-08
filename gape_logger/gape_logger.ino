#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"

// DATA SAMPLING INTERVAL
// IN MILLISECONDS
#define LOG_INTERVAL 1000 // sampling frequency (in ms)

// DATA LOGGING
#define SYNC_INTERVAL 5000 // sd write frequency (in ms)
uint32_t syncTime = 0; // time of last sync()

#define ECHO_TO_SERIAL  1 // echo data to serial port
#define WAIT_TO_START   0 // wait for serial connection to start logging

// MULTIPLEXING
// Protocols for communicating with 16 channels
// Using 4-bit binary selection
// 0 ---> 0000  /   1 ---> 1000
// 2 ---> 0100  /   3 ---> 1100
// 4 ---> 0010  /   5 ---> 1010
// 6 ---> 0110  /   7 ---> 1110
// 8 ---> 0001  /   9 ---> 1001
// 10 --> 0101  /   11 --> 1101
// 12 --> 0011  /   13 --> 1011
// 14 --> 0111  /   15 --> 1111
#define MUX_0   4 // digital pin : binary digit 1
#define MUX_1   5 // digital pin : binary digit 2
#define MUX_2   6 // digital pin : binary digit 4
#define MUX_3   7 // digital pin : binary digit 8
#define WARM_UP 10 // delay in millis to power on sensor
#define MUX_INPUTS 16 // number of inputs to be read

// INPUTS
#define SIGNAL_PIN  0 // analog pin : read sensor output

// the digital pins that connect to the LEDs
#define redLEDpin 2 // digital pin : sd-write indicator on logger shield
#define greenLEDpin 3 // digital pin : data-write indicator on logger shield

RTC_PCF8523 RTC; // define realtime clock object
const int chipSelect = 10; // chip select pin on datalogging shield

File logfile; // logging File

// Error handling
void error( char *str ){
  Serial.print( "error: " );
  Serial.println( str );
  // red LED indicates error
  digitalWrite(redLEDpin, HIGH);

  while(1); // stop logging
}

void setup(){
  Serial.begin(9600); // connect over serial port (USB)
  Serial.println(); // create blank line on serial monitor

  // set multiplexer input pins
  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  // use debugging LEDs pins
  pinMode(redLEDpin, OUTPUT);
  pinMode(greenLEDpin, OUTPUT);

  // Method to not start logging until serial connection is established
  // Disabled by default
  #if WAIT_TO_START
    Serial.println("Type any character to start");
    while(!Serial.available());
  #endif //WAIT_TO_START

  // Initialize SD Card
  Serial.print("Initializing SD card...");
  pinMode(chipSelect, OUTPUT);

  // Check if SD read malfunctioned
  if(!SD.begin(chipSelect)) {
    error("Card failed, or not present");
  }
  // SD is OKAY
  Serial.println("card initialized.");

  // CREATE NEW FILE
  char filename[] = "LOGGER00.CSV";
  for(uint8_t i = 0; i < 100; i++){
    filename[6] = i/10 + '0';
    filename[7] = i%10 + '0';
    if(!SD.exists(filename)) {
      logfile = SD.open(filename, FILE_WRITE);
      break;
    }
  }

  // CHECK IF LOGFILE CAN BE CREATED
  if(!logfile){
    error("couldn't create file");
  }

  // LOG FILE OK
  Serial.print("Logging to: ");
  Serial.println(filename);

  // READY TO BEGIN!
  analogReference(DEFAULT);
  Serial.print("LET'S... ");
  delay(500);
  Serial.print("START... ");
  delay(500);
  Serial.println("LOGGING!");
  delay(1000);

  //CONNECT TO RTC
  Wire.begin();
  if(!RTC.begin()){
    logfile.println("RTC failed");
    #if ECHO_TO_SERIAL
      Serial.println("RTC failed");
    #endif
  }

  // CREATE LOG FILE HEADERS
  logfile.print("millis,stamp,date,time,");
  for( int i = 0; i < MUX_INPUTS; i++ ){
    logfile.print("H");
    logfile.print( i );
    if(i != MUX_INPUTS-1) logfile.print(", ");
  }
  logfile.println();
  #if ECHO_TO_SERIAL
    Serial.print("millis,stamp,date,time,");
    for( int i = 0; i < MUX_INPUTS; i++ ){
      Serial.print("H");
      Serial.print( i );
      if(i != MUX_INPUTS-1) Serial.print(", ");
    }
    Serial.println();
  #endif
}

void loop(){
  DateTime now;

  // delay for logging interval
  delay((LOG_INTERVAL-1) - (millis() % LOG_INTERVAL));

  // Turn on green light to indicate data logging
  digitalWrite(greenLEDpin, HIGH);

  // log millis since starting
  uint32_t m = millis();
  logfile.print(m);
  logfile.print(", ");
  #if ECHO_TO_SERIAL
    Serial.print(m);
    Serial.print(", ");
  #endif

  // fetch current time
  now = RTC.now();
  // log time
  logfile.print(now.unixtime()); // seconds since 1/1/1970
  logfile.print(", ");
  logfile.print(now.year(), DEC);
  logfile.print("/");
  logfile.print(now.month(), DEC);
  logfile.print("/");
  logfile.print(now.day(), DEC);
  logfile.print(", ");
  logfile.print(now.hour(), DEC);
  logfile.print(":");
  logfile.print(now.minute(), DEC);
  logfile.print(":");
  logfile.print(now.second(), DEC);
  #if ECHO_TO_SERIAL
    Serial.print(now.unixtime()); // seconds since 1/1/1970
    Serial.print(", ");
    Serial.print(now.year(), DEC);
    Serial.print("/");
    Serial.print(now.month(), DEC);
    Serial.print("/");
    Serial.print(now.day(), DEC);
    Serial.print(", ");
    Serial.print(now.hour(), DEC);
    Serial.print(":");
    Serial.print(now.minute(), DEC);
    Serial.print(":");
    Serial.print(now.second(), DEC);
  #endif //ECHO_TO_SERIAL

  // Cycle through sensors and record readings
  for(int i = 0; i < MUX_INPUTS; i++){
    analogRead(SIGNAL_PIN);
    delay(WARM_UP);
    int sensorReading = readMux( i );
    logfile.print(", ");
    logfile.print( sensorReading );
    #if ECHO_TO_SERIAL
      Serial.print(", ");
      Serial.print( sensorReading );
    #endif
  }

  // end logging line
  logfile.println();
  #if ECHO_TO_SERIAL
    Serial.println();
  #endif

  // turn off green LED
  digitalWrite(greenLEDpin, LOW);

  // check if time to save to SD
  if( (millis() - syncTime) < SYNC_INTERVAL ) return;
  syncTime = millis();

  // turn on Red LED to indicate writing data
  digitalWrite(redLEDpin, HIGH);
  // save logged data to SD card
  logfile.flush();
  // turn off red LED to indicate writing complete
  digitalWrite(redLEDpin, LOW);
}

// READ FROM MULTIPLEXER
// Method for reading data from a specified channel via MULTIPLEXER
int readMux( int channel ){
  // Digital pins for encoding Binary selection
  int inputs[] = {MUX_0,MUX_1,MUX_2,MUX_3};

  // Cycle through binary pins
  for(int i = 0; i < 4; i++){
    // Set binary pin to correct bit value (1 / 0 :: high / low)
    digitalWrite( inputs[i], bitRead(channel,i) );
  }
  // Pause to allow sensor to warm up
  delayMicroseconds(WARM_UP);
  // Read from sensor
  int val = analogRead(SIGNAL_PIN);
  // Normalize sensor reading (eliminate polarity)
  return abs(val-512);
}
