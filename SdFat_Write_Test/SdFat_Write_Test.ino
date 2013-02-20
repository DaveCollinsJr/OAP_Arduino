/*

  A simple test of using write and the SdFat library instead of SD
  
 Needs:
      1) TTL Camera, preferably infrafred
      2) Ethernet Shield with Micro-SD card
      3) Micro-SD card in shield
      4) Motion-trigger (We used Zilog E-PIR in hardware mode)
      5) Arduino (Mega used for this project)
      A) Update the "mac" (MAC Address) in credentials.h from the shield, then place Arduino Ethernet Shield on the Arduino
    
    Circuit (MEGA 2560):  
      A) Pin 4 (Digital Output) RESERVED: SS for SD Card
      B) Pin 7 (Digital Output) To "SLEEP" (pin 7) of Motion Detector - used to wake it up
      C) Pin 8 (Digital Input) To "MOTION" (pin 5) of Motion Detector - LOW when motion is sensed
      D) Pin 10 (Digital Output) RESERVED: SS for Ethernet Shield
      E) Pin 11 (Digital Output) RESERVED: SPI to Ethernet Shield
      F) Pin 12 (Digital Output) RESERVED: SPI to Ethernet Shield
      G) Pin 13 (Digital Output) RESERVED: SPI to Ethernet Shield
      H) Pin 16 (Digital Output) to "Rxd(In)" Pin of camera (was Red wire, fourth wire in our camera)
      I) Pin 17 (Digital Input) to "Txd(Out)" Pin of camera (was Brown wire, third wire in our camera)
          Remember!  The Camera's TRANSMIT is _OUR_ RECEIVE and vice-versa!
      J) +5V to +5V of camera (was Grey wire, first wire in our camera)
      K) GND to GND of camera (was Purple wire, second wire in our camera)
      L) GND to GND of Motion Detector (Pins 1 (GND), 3 (DLY), 4 (SNS), and 8 (GND) of ePIR)
      M) +3.3V to Motion Detector pins 2 (VDD), 6 (LG)

  Notes: 
    Not all pins on the Mega and Mega 2560 support change interrupts, so only the following
      can be used for RX: 10, 11, 12, 13, 50, 51, 52, 53, 62, 63, 64, 65, 66, 67, 68, 69
  
  ToDo:
    * Fix it so it would be happy with image filenames up to 999 (OAP_999.jpg)
    * Possibly take different actions if don't receive expected responses from Camera
    * Check responses for all camera commands
    * Can DRY up a few things and clean up some variables
    * Turn a ton of magic numbers into defines
  
  History:
    2013-01-15 - DECj: Converting to use SoftwareSerial library instead of newsoftserial.
    2013-01-22 - DECj: Modifying it so that it writes to SD card
    2013-02-01 - DECj: Modifying for MEGA board.
    2013-02-05 - DECj: Finished MEGA board testing in the other project.  Continuing mods here.  No longer using SoftwareSerial.
    2013-02-04 - DECj: Successful multi-picture MEGA run!
    2013-02-20 - DECj: Attempting to change from Arduino's SD Library to SdFat.  SdFat supposedly has fewer memory leaks and is faster.
    
    Shout Outs:
      To "salacain" (Arduino forum) of Ashburn, VA for posting some code that was a starting point for grabbing images from the camera 
      To Mario Bohmer (http://marioboehmer.blogspot.de/ ) for some good simple code that worked for detecting motion
      And of course to the gang at Spark Fun for continuously offering up great products and ideas.
          
    Copyright (NONE):
    Please feel free to use, modify or share this code without restriction. If you'd like to credit One Asset Place or CC-Logic in your 
    derivatives, that'd be nice too!
    
*/

#include <Ethernet.h>
#include "Credentials.h"
#include <SPI.h>
#include <SdFat.h>
#include <EEPROM.h>

// Name all of our pins
#define sdCardControlPin 4  // SD Card Control Pin
#define SLEEP_MODE_PIN 7  // ... To ePIR Pin 7 (SLEEP command)
#define MOTION_DETECT_PIN 8  // ... To ePIR Pin 5 (MOTION DETECT)

// No string we need will ever be longer than this
#define WayTooLong 1000

const int maxToReceive = 20;
boolean lastConnected = false;  // Keep track of whether or not we connected last time
boolean tookPictureLastTime = false;  // The last time through the loop, did we take a picture?
const int packetBuffSize = 42;  // This is our read/write buffer size from the camera to the SD card
const int innnerDataBodyStart = 5;  // This is the START of our chunk of the buffer that actually contains image data
const int innerDataBodyEnd = 37;  // This is the END of our chunk of the buffer that actually contains image data

// Test with reduced SPI speed for breadboards.
// Change spiSpeed to SPI_FULL_SPEED for better performance
// Use SPI_QUARTER_SPEED for even slower SPI bus speed
const uint8_t spiSpeed = SPI_HALF_SPEED;

// WARNING!  If you get into larger image sizes, this might need to be unsigned long!
unsigned int startConnectionTime;

byte incomingbyte;  // Used to hold gathered data
byte responseData;  // Used to validate responses
byte nextByte;  // Holds the bytes as we receive them from file before sending
byte responseMessage[110]; // Used to hold responses

int rc; // response counter
int packetCount = 0;

const unsigned long deadManTimer = 6000;
unsigned long startWaitTime;

// unsigned long is what SD.size() returns
unsigned long theSize;   // This generically holds either our image size or the file size (saves us two bytes!)
unsigned long postLength;
unsigned long fileBytesSent;


int received = 0;
int pictureTaken = 0;  // Number of pictures we've taken since restarted.
int sizeMessage[9];  // Used to hold the JPEG file size as cryptically given to us by the camera

// a is our Read Starting address 
// I think we're starting to get probs around 32,379 bytes.  Maybe our "a" needs to be unsigned int or long?
// yes that was the problem.  Switching this to Long...
long a=0x0000;

uint8_t MH,ML;

//File dataFile;  // NOTE: dataFile is shared between both the picture taking and picture sending routines
// file system object
SdFat sd;
SdFile dataFile;  // NOTE: dataFile is shared between both the picture taking and picture sending routines
SdFile writePicFile;


EthernetClient client;  // Setup a client to connect to RAILS server

boolean SendResetCmd();
boolean SendTakePhotoCmd();
void SendReadDataCmd();
void SendStopTakingPicturesCmd();

void setup() {
  // ======================================  ONE TIME SETUP  =====================================
  // Open serial communications
  Serial.begin(9600);
  Serial.println("Setup");
  
  // Give the motion detector some time to wake up
  delay(200);
  // Tell the motion detector we do NOT want it in sleep mode
  digitalWrite(SLEEP_MODE_PIN, HIGH);  

  // Talk to the Camera on MEGA Serial 2
  Serial2.begin(38400);

  // Give the Ethernet shield some time to wake up
  delay(300);
  Serial.println("Ethernet Awake"); 

  // Set up SD Card
  /* Note that because the W5100 and SD card share the SPI bus, only one can be active at a time.
      If you are using both peripherals in your program, this should be taken care of by the corresponding libraries.
      If you're not using one of the peripherals in your program, however, you'll need to explicitly deselect it.
      To do this with the SD card, set pin 4 as an output and write a high to it. 
  */
  pinMode(sdCardControlPin, OUTPUT);

  if (!sd.begin(sdCardControlPin, spiSpeed)) {
    Serial.println("SD Card Fail"); // don't do anything more:
    return;
  }
  

  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Ethernet Begin Fail");
      // no point in carrying on, so do nothing forevermore:
    while(true);
  }
    
  // give the Ethernet shield a second to initialize:
  delay(1000); 
 
}


 
void loop()
{
  Serial.println("Loop"); 
     // ++++++++++++++++++++++++++++++++++++++++++++++++++  BEGINNING of PICTURE PORTION  +++++++++++++++++++++++++++++++++++++++
  
    // Is there motion Detected?
    // TODO: Incorporate one of the motion detector circuits
    //if (digitalRead(MOTION_DETECT_PIN) == LOW)
    if (1 == 1)
    {
      // OK we need to take a picture, save it, and send it
      Serial.println("Motion Detect"); 
      
      // Variables ONLY needed by the PICTURE side of things...
      boolean EndFlag=0;
      int i;  // Various loops
            
     // ----- Create image filename (sequential) -----
      Serial.println("Creating Image File");

      // NEVER use thse "String" Library in Arduino!  It immediately wastes 2K!!    
      //      String filename = "OAP_"+String(pictureTaken)+".jpg";   // Create new filename.  Note this integer could easily go to 4 decimal places!
      //      int strlen=filename.length()+1;              // Get the length of the string including the null character
      //      char charBuf[strlen];                        // initialize a character array to fold the filename
      //      filename.toCharArray(charBuf, strlen);       // convert the String filename to character array
      
      //Serial.println(filename);
      char charBuf[11] = "OAP_00.JPG";
      
      // Make an ascii representation of this number and jam it in the filename
      charBuf[4] = pictureTaken/10 + '0';
      charBuf[5] = pictureTaken%10 + '0';
    
      Serial.println(charBuf);
      
      if(sd.exists(charBuf)) {
        // The file exists... Remove it
        sd.remove(charBuf);
        Serial.println("Removed existing file");
      }

      // Open the file
      writePicFile.open(charBuf, O_WRITE | O_CREAT );
      if (writePicFile.isOpen())
      {
        Serial.println("New file open");
      }
      else
      {
        Serial.println("ERROR: Could not open file");
      }
           
     
        // Now, Read the JPEG file content
        byte packet[packetBuffSize];
        // Setup for a nice, clean reading of the packets
        a=0x0000;  // This is our buffer address!
        packetCount = 0;
           
        while(!EndFlag)
        {  

            // Just fill the dang thing with dummy data
            for (i = 0; i < 15; i++) {
              packet[i] = i;
            }
            packet[i+1] = 0xFF;
            packet[i+2] = 0xD9;
            packetCount = i;
    
            // Now, loop through the useful bytes of the data packet and write to the card
            // We do this in a self-incremented loop from innnerDataBodyStart (5) to < innerDataBodyEnd (37) because we need to bail out 
            // immediately when we see the end of the JPG data
            i = innnerDataBodyStart;
            int bytesWritten = 0;
            while (!EndFlag && i < innerDataBodyEnd)     
            {s
              //       Write data to packet to SD card 
             writePicFile.clearWriteError();
             writePicFile.write(packet[i]);
              
              if((packet[i-1] == 0xFF) && (packet[i] == 0xD9))  //Check if the picture is over (a JPEG ends with FF D9)
              {
                writePicFile.close();
                Serial.println("File closed");
                EndFlag = 1;
                pictureTaken++;
                // After 99 we'll go back to zero
                if (pictureTaken > 99)
                  pictureTaken = 0;
              }
               
               // If we got this far, we are still good to continue processing this packet
               i++;
               
            } // end looping through useful bytes of packet 
             
        }// end of reading the packets until finished     
  
        Serial.print("Packets: ");
        Serial.println( packetCount);
  
       Serial.println("Issuing Stop Taking Pictures command");
       SendStopTakingPicturesCmd();
  
       Serial.println("Issuing Reset command");
       SendResetCmd();
    
   
    } // end motionDetected==true
    else
    {
      // No motion detected
      // TODO: Do some housekeeping
      Serial.println("No Motion Detect");  
    }

   tookPictureLastTime = true;

 

  // Wait a while, then do it again!
  Serial.println("WAITING for next loop");
  delay( 2000 );
  
  
}  // End of Loop


//Send Reset command
boolean SendResetCmd()
{
     // Dumps the camera input buffer, issues a reset command, and looks for the response
     // Returns true if we received the expected response
     // As of Arduino 1.0 the 'BYTE' keyword is no longer supported.
     // Please use Serial.write() instead.
     // we must cast to (byte) to tell the compiler how to interpret these ZERO values
     // can use (byte) but it is recommended to use (uint8_t)
     // We Send: 65 00 26 00
     // It Returns: 76 00 26 00
     //              v    &

      // First, flush the buffer
      dumpCameraInput(true);

      Serial2.write(0x56);
      Serial2.write((uint8_t)0x00);
      Serial2.write(0x26);
      Serial2.write((uint8_t)0x00);

      return successIfContains(0x76, 0, 0x26, 2);
}

void dumpCameraInput(boolean verbose)
{
  // Use this before issuing a command to be sure the buffer is empty.
  if (verbose == true)
    Serial.print("Dumping...");
    
  while(Serial2.available() > 0) 
     {
        incomingbyte=Serial2.read();
        if (verbose == true)
          Serial.write(incomingbyte);
      }  
      
  if (verbose == true)
    Serial.println("... END DUMPING");
  
}


//  Send take picture command. Tells the camera to take a picture, and analyzes the response
boolean SendTakePhotoCmd()
{    // Send: 56 00 36 01 00
     // Response: 76 00 36 00 00  (5 Bytes:  v  6  )
     
     dumpCameraInput(true);
     
      // Send the take picture command
      Serial2.write(0x56);
      Serial2.write((uint8_t)0x00);
      Serial2.write(0x36);
      Serial2.write(0x01);
      Serial2.write((uint8_t)0x00);  
      
      return successIfContains(0x76, 0, 0x36, 2);
        
}


boolean successIfContains(byte firstValue, int firstPos, byte secondValue, int secondPos) {
 // Use this to DRY up our response checking loops
 // You must give us two bytes and their positions (0-start).  We'll look
 // in Serial2's input for them (as long as deadManTimer) and tell you if we received them or not
    startWaitTime = millis();
    int i = 0;
    while(i <= secondPos && (millis()-startWaitTime < deadManTimer))
    {
      if (Serial2.available() > 0) {
        incomingbyte=Serial2.read();
        responseMessage[i] = incomingbyte;
        //Serial.print(responseMessage[i], HEX);
        Serial.write(responseMessage[i]);
        if (incomingbyte == 0x0A)
          Serial.println();
        else {
          //Serial.print(" ");
        }
        i++;
      }
    }
    
    if (responseMessage[firstPos] == firstValue && responseMessage[secondPos] == secondValue) {
    Serial.println("Good Response");
    return true;
    }
    else
    {
      Serial.println("Warning: Expected response not found");
      return false;
    }           
        
 
}




// Asks the camera for the size of the image it has taken.
void SendGetImageSizeCommand() 
{ 
  Serial2.write(0x56); 
  Serial2.write(byte(0x00)); 
  Serial2.write(0x34); 
  Serial2.write(0x01); 
  Serial2.write(byte(0x00)); 
}


//Read data
void SendReadDataCmd()
{
      MH=a/0x100;  // divide by 0x100 (256 Decimal)
      ML=a%0x100;  // Modulus of that division

      Serial2.write(0x56);
      Serial2.write((uint8_t)0x00);
      Serial2.write(0x32);
      Serial2.write(0x0c);
      Serial2.write((uint8_t)0x00); 
      Serial2.write(0x0a);
      Serial2.write((uint8_t)0x00);
      Serial2.write((uint8_t)0x00);

      // This is the "Init Address" from which we want to read
      Serial2.write(MH);
      Serial2.write(ML);
      
      Serial2.write((uint8_t)0x00);
      Serial2.write((uint8_t)0x00);

      // This is the "Data Length" (KK KK)  In our case 0x20 = 32 Bytes
      Serial2.write((uint8_t)0x00);
      Serial2.write(0x20);
      
      // This is the "Spacing Interval" we want which is recommended to be small such as 00 0A
      Serial2.write((uint8_t)0x00);  
      Serial2.write(0x0a);

      // Each time we Read Data, increase the starting address by 0x20 (32 Decimal)...
      // This is our buffer size.
      a += 0x20;
}



void SendStopTakingPicturesCmd()
{ // Tells the damn camera to stop taking pictures
  // It should respond with: 76 00 36 00 00 (FIVE bytes)
      Serial2.write(0x56);
      Serial2.write((uint8_t)0x00);
      Serial2.write(0x36);
      Serial2.write(0x01);
      Serial2.write(0x03);    

}



int writeNtarrFromEEPROM(int startAddr, boolean addCRLF) {
  // Writes a null-terminated string (that you must have previously written to EEPROM) from the
  // EEPROM starting at a specific address
    char ch;
    int length = 0;
    int currAddr = startAddr;
    do {
      ch = EEPROM.read(currAddr);
      currAddr++;
      length++;
      if (ch != '\0')
        client.print(ch);
      if (length == WayTooLong)
        Serial.println(160);
    } while (ch != '\0' && length <= WayTooLong) ;
    
    if (addCRLF == true)
      client.println("");
  
}


