/*
  One Asset Place Motion Camera - REQUIRES ARDUINO MEGA 2560 R3.... Needs more memory than an Uno can handle!
  
    WARNING!! YOU MUST SAVE VALUES IN EEPROM FIRST (with OAP_Setup_EEPROM) BEFORE USING THIS!!!  Here is the current list of saved values:
  The strings in your EEPROM:
    START     String
    -----     -------------------
    0       POST /readings HTTP/1.1
    24      Content-Type: multipart/form-data; boundary=
    69      ----------------------------283499ce49c6
    110     Host: www.oneassetplace.com
    138     Content-Length: 
    155     Content-Disposition: form-data; name="
    194     reading[sensor_id]"
    214     sensor[key]"
    227     517c070d8xxxxxxxxxxxxxxxxxxxxe801b424d27      <--- Replace this with YOUR www.oneassetplace.com security key!!!
    268     reading[raw]"
    282     {"1":"image"}     <--- Replace the "1" with your "Sensor Input Seq"!!!!
    296     reading[image]"; filename="
    324     Content-Type: image/jpeg
    349     Connection: close
  
  This version had to use AGRESSIVE memory management techniques because we were running out of both FLASH (program) and SRAM (variables) !!!
  In the UNO we had to resort to using all numeric logging (which was awful).  Here on the MEGA we should have enough SRAM to 
  have meaningful messages, but we still need to keep them SHORT!

  Basically, we had to eliminate any and all strings we didn't need... Sorry for the lack of readability.  Go back to the separate, working programs
  (the one that takes pictures and stores them, and the one that sends pictures) to solve your problem if you have debugging issues)
    
  Motion triggers camera to take picture.  Store picture as a file on local micro-sd card (of Ethernet shield).  When picture file is complete,
  send raw picture data as an HTTP Post to One Asset Place.  Delete file locally after sent.
    
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
    2013-03-26 - DECj: Fixed to be OK with http 200 as well.  Also don't wait for timeout at end of POST.
    
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




// =========================================================================================================
//                            Camera specific settings
const int preambleSize = 5;  // Number of bytes in the JPEG preamble.  Specific to the CAMERA.
const int postambleSize = 5;  // Number of bytes in the JPEG postamble.  Specific to the CAMERA.

//    ------------------------------------------------------------------------------------------
// !!!! Careful! Original values of 32 and 0x20 are well tested !!!!!
// 32 / 0x20 / 20 = 26.8 seconds to save
// 64 / 0x40 / 20 = 19.4 seconds to save
// 128 / 0x80 / 20 = 15.7 seconds to save
// 248 / 0xF8 / 20 = 15.1 seconds to save... diminishing returns lets try larger sync ratio
// 248 / 0xF8 / 40 = 15.4 Not much change.  Perhaps the cameras buffer is not that large?
// 248 / 0xF8 / 40 @ full speed = 15.0 with some bugginess in POST. Back to half speed
// 248 / 0xF8 / 80 = about 15.  Keeping it here for now.
// Camera documentation says it must be an "integer multiple of 8".  The code, as currently written won't support a
// value >= 256 for sure.
// TODO: Fix the sendreaddatacmd routine so it can properly split into words so we can go up larger.  Should also
// Be able to get rid of the redundant jpegReadPacketSize and calculate it automatically.
const int usefulPacketSize = 248; // Number of bytes in the buffer we're asking for
// !!!Careful with this one!  This is the number of bytes we ask for in each JPEG payload
const byte jpegReadPacketSize = 0xF8;  // This _MUST_ be the hex equivalent of usefulPacketSize!!
//    ------------------------------------------------------------------------------------------

// This is the FULL packet size from the camera (including preamble/postamble)  Was originally 42 (with 32 byte useful)
const int fullPacketSize = preambleSize + postambleSize + usefulPacketSize;

// This is the START of OUR chunk of the buffer that actually contains image data
//      This is array position 5. 0 through 4 hold the preamble
const int innerDataBodyStart = preambleSize;  
// This is the END of our chunk of the buffer that actually contains image data
//      This is array position 36.  37 through 41 hold the postamble
const int innerDataBodyEnd = innerDataBodyStart + usefulPacketSize - 1;  
// =========================================================================================================



const int syncInterval = 80;  // Each time we've written this many chunks to the SD card, call a sync...  Initial was 20s



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
int stopAt = 0;
int bytesWritten;

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

// file system objects
SdFat sd;
SdFile dataFile;  // NOTE: dataFile is shared between both the picture taking and picture sending routines


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
  /*
    Here is our overall loop:
    
    Alternate between taking pictures and sending pictures
    
    If we took a picture last time:
      1) Any pictures on the SD card to send?
        IF SO:
          A) Send picture to OneAssetPlace via Ethernet shield
          B) Delete picture locally
    
    If we didn't take a picture last time:
      1) See if there is motion detected
      IF MOTION:
        A) Take a Picture
        B) Save picture to micro-sd card
        C) Connect to server and send picture
        D) Delete picture from micro-sd card
        E) (Housekeeping) If card nearly full, delete oldest picture(?)
      IF NO MOTION:
        A) Increment heartbeat counter
        B) Every X increments send something to server so it knows we are alive
  */
 
  if (tookPictureLastTime == true) {
    /* 1) Any pictures on the SD card to send?
        IF SO:
          A) Send picture to OneAssetPlace via Ethernet shield
          B) Delete picture locally
    */
    //    File root;
    SdFile root;
    
    // Any pictures locally to send?
    // 2013-02-20 - DECj: Trying a new technique here that should be faster and easier than scanning
    // through the directory... We KNOW our files will be OAP_00.JPG through OAP_99.JPG so just
    // loop through that array and stop if you find one.  Idea was from AnalogLogger example code.
    Serial.println("Checking for Pic Files");
    
    char name[] = "OAP_00.JPG";

    for (uint8_t i = 0; i < 100; i++) {
      name[4] = i/10 + '0';
      name[5] = i%10 + '0';
      if (sd.exists(name)) {
        // We have an image file that needs to be sent to One Asset Place  
        if (dataFile.open(name, O_READ)) {
            
          Serial.print("File:");
          Serial.println(name);
  
          char firstReceived[maxToReceive]; // To hold the first X characters received from HTTP
  
           // We are building the string manually, so let's add a terminator at the end no matter what
           firstReceived[maxToReceive] = '\0';

          // REMEMBER!
          // A) sprintf does not work reliably in Arduino!
          // B) strcpy is not an APPEND it is a force-over copy!!
          // C) You are responsible for buffer size, etc!  If you overwrite buffers, expect extremely odd results including constant
          //    resetting of your Arduino!
          //strcpy(postData, ""); // Empty our array of characters terminated with \0
          //strcpy(postData, restTags);
          // Now APPEND the picture data to the postData string...
          //strcat(postData, dataFile.name());
          //strcat(postData, "\"}:");      
          //Serial.println(postData);
        
          theSize = dataFile.fileSize();
          Serial.print("Size:");
          Serial.println(theSize);
          
          // Calculate our post length which is everything we'll be sending in the BODY of the post
          // Don't forget to account for the CR/LF!!
          // Using WRITE, the value for our 47,560 file was 48119.  That is 559 of overhead:
          // PostBody (My calculations of text) is 551
          // filename is (in this case) 9
          //postLength = 48119;
          // By my calculations, we sent them 48118 of data, but it seems happy with this.
          // TODO:  Might want to play with a few other values (549, 550, 551) to see if it makes a difference.
          postLength = 550 + strlen(name) + theSize;
          
          // if there's incoming data from the net connection.
          // send it out the serial port.  This is for debugging
          // purposes only:
          while (client.available() > 0) {
            char c = client.read();
            //Serial.print(c);
          }
          
          // if there's no net connection, but there was one last time
          // through the loop, then stop the client:
          if (!client.connected() && lastConnected) {
            Serial.println("Stopping prior connection");
            client.stop();
          }
        
          // --- If we got this far, the Ethernet shield is powered up and we are on the network ---
          Serial.println("Shields Up, about to POST");  
          // if you get a connection, report back via serial:
          if (client.connect(serverName, serverPort)) {
            //Serial.println( data );
            // Remember, POST is very picky about newlines and dashes!!.  Note the difference between println() and print()
            // use "client." for real, "Serial." for testing...
            
            // Send "POST /readings HTTP/1.1"
            writeNtarrFromEEPROM(0, true);
            // Send "Host: www.oneassetplace.com"
            writeNtarrFromEEPROM(110, true);
            // Send "Content-Type: multipart/form-data; boundary=<boundary>
            writeNtarrFromEEPROM(24, false);
            writeNtarrFromEEPROM(69, true);
            // Send "Content-Length: "
            writeNtarrFromEEPROM(138, false);
            client.println( postLength );
            // Send "Connection: close"
            writeNtarrFromEEPROM(349, true);
  
            // Content-Length we send is from AFTER "Connection: close" until the end (?)
            client.println();
            
            // Send -- + boundary
            client.print("--");
            writeNtarrFromEEPROM(69, true);
            // Send Content-Disposition: form-data; name="
            writeNtarrFromEEPROM(155, false);
            // Send reading[sensor_id]=
            writeNtarrFromEEPROM(194, true);
            // Send the "Sensor ID" with a CR before it
            client.println();
            client.println(sensorID);
  
            // Send -- + boundary
            client.print("--");
            writeNtarrFromEEPROM(69, true);
            // Send Content-Disposition: form-data; name="
            writeNtarrFromEEPROM(155, false);
            // Send sensor[key]"
            writeNtarrFromEEPROM(214, true);
            // Send (the key) with a CR before it
            client.println();
            writeNtarrFromEEPROM(227, true);
  
            // Send -- + boundary
            client.print("--");
            writeNtarrFromEEPROM(69, true);
            // Send Content-Disposition: form-data; name="
            writeNtarrFromEEPROM(155, false);
            // Send reading[raw]"
            writeNtarrFromEEPROM(268, true);
            // Send {"sensorinputseq":"image"} with a CR before it
            client.println();
            writeNtarrFromEEPROM(282, true);
  
            // Send -- + boundary
            client.print("--");
            writeNtarrFromEEPROM(69, true);
            // Send Content-Disposition: form-data; name="
            writeNtarrFromEEPROM(155, false);
            // Send reading[image]"; filename="
            writeNtarrFromEEPROM(296, false);
            // Send the filename and an end quote
            client.print(name);
            client.println("\"");
            
            // Send Content-Type: image/jpeg
            writeNtarrFromEEPROM(324, true);
            client.println();
            // Now LOOP through the entire JPEG image file sending each byte
  
            // Todo: More graceful handling if server resets connection...
            // && client.connected()
            fileBytesSent = 0;
  
            // Warning! (SD) dataFile.read did not work as documented!  Must use dataFile.available to check for more data
            while (dataFile.available() > 0 ) {
              nextByte = dataFile.read();
              // Careful with how this information is formatted.  HEX doesn't seem correct
              // You probably need a WRITE not a PRINT here!
              client.write(nextByte);
              fileBytesSent++;
            }
  
            // CLOSE the file
            dataFile.close();
            Serial.println("Pic file closed"); 
  
            if (client.connected() == true) {
              // END of looping through the file 
              client.println();
              // Send -- + boundary + --
              client.print("--");
              writeNtarrFromEEPROM(69, false);
              client.print("--");
              client.println();
            }
            else
            {
              // Unexpected:  Client was not connected at end of file sending, couldn't send final boundary
              Serial.println("Error: Client not connected at end");  
            }
              
            Serial.print("Sent:");
            Serial.println(fileBytesSent);
              
            }  // END if able to connect to Ethernet board
            else {
              // Failed to connect to the Ethernet Board
              // 01/03/2013 - Having issues where it stops sending, yet the C code seems to be looping just fine
              // (for instance, the LED's properly go on/off as doors are opened / closed)
              // Will try resetting the client here.  Also will "flash" the LED's to let you know this occurred
              Serial.println("Error: Ethernet Connect Fail");
            }
        
          // NORMALLY what happens in this scenario, is the SERVER closes the connection, which will
          // exit out of this loop after you've gathered all the response data...  If, for some reason,
          // we get no response (and the connection just hangs) the deadManTimer should eventually kick
          // us out of this loop.
          if (client.connected() == true) {
            const unsigned long deadManTimer = 6000;    // Six seconds is all we'll wait at this point
            unsigned long startWaitTime;  // SD.size() returns unsigned long
            startWaitTime = millis();

            // Note that we also check client.connected again because we were waiting for the timeout all the time
            while ((millis() - startWaitTime) < deadManTimer && client.connected() == true) {
           
              // Handle the data coming back! .available returns the number of bytes available for reading
              while (client.available() > 0) {
                char c = client.read();
                  /* This is EXACTLY what we get back from a successful response:
                    HTTP/1.1 201 Created
                    Content-Type: text/html; charset=utf-8
                    X-UA-Compatible: IE=Edge
                    ETag: "e0aa021e21dddbd6d8cecec71e9cf564"
                    Cache-Control: max-age=0, private, must-revalidate
                    Set-Cookie: _oneassetplace_session=BAh7BkkiD3Nlc3Npb25...blahblahblahblah...cb2d80d1b8e9b1cf101093ef84925bcbe7; path=/; HttpOnly
                    X-Request-Id: 8882289d0bc02b442b72d33fe9885ae6
                    X-Runtime: 1.316919
                    Connection: close
                    Server: thin 1.5.0 codename Knife
                    
                    OK
                  */
                // How do we parse through that WITHOUT eating up tons of precious RAM?
                // Just keep a small buffer of the first x characters and look for HTTP/1.1 201 ?
                if (received < maxToReceive-1) {
                  firstReceived[received] = c;
                  received++;
                }
                  
                  //Serial.print(c);
              }
                
             // Give the server a bit of time to respond
             delay (10);
             }  // END of deadman timer waiting for response
              
            }  // END if client still connected
            else
            {
              // We should have been still connected but we were not - could not process response.
              Serial.println("Error: Could not process response");
            }
        
            // At this point we've either finished naturally or we've given up, so stop the client
            Serial.println("Stopping client");
            client.stop();
            
            // What was the first chunk of our received data?
            Serial.print("FirstReceived: ");
            Serial.println(firstReceived);
            
            if (strstr(firstReceived, "201") != NULL  || strstr(firstReceived, "302") != NULL || strstr(firstReceived, "200") != NULL){
              // Successful Transfer!
              // Anything even CLOSE to success should delete the file, otherwise we keep re-sending
              Serial.println("200 OK, 201 Success, or 302 Moved");
              
              // Delete the file from our local SD card
              sd.remove(name);
            }
            
            // Record our status (which really is always going to be "disconnected"
            lastConnected = client.connected();
          } // end if the data file is open
          else {
            // The file existed but didn't open... Should log
            Serial.print("ERROR: File existed but could not open:");
            Serial.println(name);
          }
        }  // end if file exists
        //  ===============================  END of code to SEND a single picture file to the server  ===================
            
      }  // END for each possible filename

      Serial.println("Finished traversing pic files");
       
      // Record the fact that, this time through, we did NOT take a picture
      tookPictureLastTime = false;
      
  } 
  else
  {
    // ++++++++++++++++++++++++++++++++++++++++++++++++++  BEGINNING of PICTURE PORTION  +++++++++++++++++++++++++++++++++++++++
  
    // Is there motion Detected?
    // TODO: Incorporate one of the motion detector circuits
    if (digitalRead(MOTION_DETECT_PIN) == LOW)
    //if (1 == 1)
    {
      // OK we need to take a picture, save it, and send it
      Serial.println("Motion Detect"); 
      
      // Variables ONLY needed by the PICTURE side of things...
      boolean EndFlag=0;
      int i;  // Various loops
      int u;
            
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
      dataFile.open(charBuf, O_WRITE | O_CREAT | O_TRUNC);
      if (dataFile.isOpen())
      {
        Serial.println("New file open");
      }
      else
      {
        Serial.println("ERROR: Could not open file");
      }
      
      
      Serial.println("Sending Reset command");   
      SendResetCmd();
  
      Serial.println("Taking Picture in 2.5 seconds... Say Cheese!");
      delay(2500);    //After reset, wait 2-3 second to send take picture command
     
      Serial.println("Sending Take Photo Command");  
      SendTakePhotoCmd();
  
      Serial.println("Sending Read Image Size command");
      delay(80);  // You MUST pause a little bit here or the camera is unhappy
      SendGetImageSizeCommand();
      delay(80);  // You MUST pause a little bit here or the camera is unhappy.  50 seemed to be OK for uno
      Serial.print("Image Size: ");
      // Returns: 76 00 34 00 04 00 00 XH XL
      // Where XH XL is the length of the picture file, MSB in the front and LSB in the end.

        startWaitTime = millis();
        Serial.println("Waiting for image size");  // Waiting for image size response
        while(Serial2.available() < 9 && (millis() - startWaitTime) < deadManTimer) // wait for 9 bytes  but gank out if you have to
        {
          // Note: Without a dead-man timer here, this thing will hang if the camera hangs
        }
        
        if(Serial2.available() >= 9) {
          // Assume all is well - we got an image size response
          Serial.println("Received Image Size Response: ");

          i=0;
          while(Serial2.available() > 0 && i < 9) // Read the serial buffer contents but no more than 9
          {
             incomingbyte=Serial2.read();
             sizeMessage[i] = incomingbyte;
             //Serial.print(sizeMessage[i]);
             i++;
          }  
          
          // Yank the image size they sent us from the response
          theSize = word(sizeMessage[7], sizeMessage[8]);
    
          Serial.print("Image Size: ");
          Serial.println(theSize);
          EndFlag = false;
            
        }
        else {
          // Bummer... We seemed to hang waiting for the image size to come back.  Let's bail and try later
          EndFlag = true;
          Serial.println("Error: Failed to receive image size response");
        }
        

        // Now, Read the JPEG file content
        byte packet[fullPacketSize];
        byte usefulPacket[innerDataBodyEnd - innerDataBodyStart + 1];
        // Setup for a nice, clean reading of the packets
        a=0x0000;  // This is our buffer address!
        packetCount = 0;
           
        while(!EndFlag)
        {  
          // Sometimes there might be gook in the buffer after the last packet and before
          // we ask it for more data... Ignore that.
          // Do not log this clearing of the input buffer... In a loop here!
          dumpCameraInput(false);  // Do this in silent mode!
          
          // Tell the camera we would like a picture and explain to it our buffer size and
          // requested delay... 
          SendReadDataCmd();
            
          i = 0;
          u = 0;
          stopAt = 0;
          int numBytes = 0;
          
          // Notice we read (fullPacketSize) 42 bytes from the serial buffer.  That is:
          // 5 Byte preamble (76 00 32 00 00)
          // 32 Bytes of data (that is what we asked for)
          // 5 Byte postamble (76 00 32 00 00)       
          while(i < fullPacketSize)    // Read fullPacketSize (42) bytes in from serial buffer
          {                  
            while(Serial2.available() > 0)
            {      
              packet[i] = Serial2.read();  // Save EVERYTHING in packet (for debugging)
              if (i >= innerDataBodyStart && i <= innerDataBodyEnd && stopAt == 0) {
                usefulPacket[u] = packet[i];  // Only image data goes in usefulPacket
                
                if((packet[i] == 0xD9) && (packet[i-1] == 0xFF))
                  { // Hey! FF D9 is the END of a JPEG image, so we only need to write up to this
                    // ... The rest will be filler up until the postamble
                    stopAt = u;
                  }
                u++;
              }
              i++;
              
            }
          }
  
          // We have now read another packet
          packetCount++;
  
          // Now, just write the entire length to the SD card rather than one byte at a time
          dataFile.clearWriteError();
          // WARNING: SdFat Library seems to have a bug with checking the error status here...
          // It will always return 0 which, according to the docs, means an error has occurred...
          // TODO: Reproduce and log as issue or fix.
          
          // OK There are two possibilities here:
          // 1) stopAt == 0 : We read a full set of 32 bytes (innderDataBodyEnd - innerDataBodyStart + 1)
          // 2) else We only need stopAt+1 bytes
          if (stopAt == 0)
            numBytes = (innerDataBodyEnd - innerDataBodyStart + 1);
          else
            numBytes = stopAt+1;
          
          bytesWritten = dataFile.write(usefulPacket, numBytes);

          if (packetCount%syncInterval == 0) {
            // Need to sync every syncInterval times...
            //Serial.println("Syncinc");
            dataFile.sync();
          }


          if (stopAt != 0) {
            // This was the end of the image!
              dataFile.close();
              Serial.println("File closed");
              EndFlag = 1;
              pictureTaken++;
              // After 99 we'll go back to zero
              if (pictureTaken > 99)
                pictureTaken = 0;
            }
               
            // If we got this far, we are still good to continue processing this packet
               
        } // end looping through useful bytes of packet 
             
  
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

  }  // End of Send Picture or Take Picture IF
  

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
  
  dumpCameraInput(true);
  
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

      // This is the "Data Length" (KK KK)  In our case jpegReadPacketSize 0x20 = 32 Bytes
      Serial2.write((uint8_t)0x00);
      Serial2.write(jpegReadPacketSize);
      
      // This is the "Spacing Interval" we want which is recommended to be small such as 00 0A
      Serial2.write((uint8_t)0x00);  
      Serial2.write(0x0a);

      // Each time we Read Data, increase the starting address by jpegReadPacketSize 0x20 (32 Decimal)...
      // This is our buffer size.
      a += jpegReadPacketSize;
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


