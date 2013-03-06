/*
  One Asset Place Zigbee Controller for Arduino
  
  This may or may not eventually need a bunch of information written to the EEPROM, but we'll start with NOT needing 
  the info in the EEPROM
  
   Needs:
      1) Zigbee setup in controller mode 
      2) Ethernet Shield 
      3) Arduino (Uno should work for this project)
      4) Update the "mac" (MAC Address) in credentials.h from the shield, then place Arduino Ethernet Shield on the Arduino
    
    Circuit (MEGA):  
      A) Pin 4 (Digital Output) RESERVED: SS for SD Card
      B) Pin 16 (Serial2 TX) to "DIN" of the zigbee controller
      C) Pin 17 (Serial2 RX) to "DOUT" of the zigbee controller
          Remember!  The zigbee's TRANSMIT is _OUR_ RECEIVE and vice-versa!
      D) Pin 10 (Digital Output) RESERVED: SS for Ethernet Shield
      E) Pin 11 (Digital Output) RESERVED: SPI to Ethernet Shield
      F) Pin 12 (Digital Output) RESERVED: SPI to Ethernet Shield
      G) Pin 13 (Digital Output) RESERVED: SPI to Ethernet Shield
      H) GND to Zigbee "GND"
      I) +3.3V to Zigbee "VCC"

  
  ToDo:
  
  History:
    2013-03-04 - DECj: First version, basing the code off of the successful OAP Motion Camera / Ethernet code
    
    Shout Outs:
      Robert Faludi and "Building Wireless Sensor Networks"
          
    Copyright (Creative Commons):
    This software is provided under the Creative Commons Attribution (CC) License
    See License.md or http://www.tldrlegal.com/L/CC for the actual license
    
*/
#include <SoftwareSerial.h>
#include <Ethernet.h>
#include "Credentials.h"
#include <SPI.h>

// Name all of our pins
#define sdCardControlPin 4  // SD Card Control Pin

// No string we need will ever be longer than this
#define WayTooLong 1000

boolean lastConnected = false;  // Keep track of whether or not we connected last time


// WARNING!  If you get into larger image sizes, this might need to be unsigned long!
unsigned int startConnectionTime;

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
unsigned long fileBytesSent;


int received = 0;
int pictureTaken = 0;  // Number of pictures we've taken since restarted.
int sizeMessage[9];  // Used to hold the JPEG file size as cryptically given to us by the camera

//SdFat sd;

//int rcvZigbeeSerial = 5;  // We receive - hook to Zigbee DOUT
//int xmtZigbeeSerial = 6;  // We transmit - hook to Zigbee DIN
// Use Serial2 (16,17) with MEGA

int minFrameSize = 21;  // We've got to have at least 21 bytes
int i;
int expectedLength;  // What the frame tells us we'll get
int actualLength;    // What we actually got
int pair;  // Counts the number of analog value paris we've received

byte incomingbyte;
byte lengthField[2]; // Holds Length Frame Field
byte digitalMaskField[2]; // Holds the 2-byte digital channel mask
byte digitalSampleField[2]; // Holds the 2-byte set of digital sample
byte analogMaskField; // Holds the one-byte analog mask value
byte analogSampleField[2]; // Holds a pair of analog samples

int digitalVal; // Both digital bytes end up as one int value
int analogVals[4]; // At most we can hold 4 analog values (A0 through A3)

// Warning!  This must be wide enough for your longest data string that is ever possible!
// Our string is in the format of "reading[sensor_id]=1&reading[reported]=x";
char postData[100];
char smallChunk[20];
int charsInData;
const int maxToReceive = 20;
char firstReceived[maxToReceive];
boolean haveDigital;

byte checkSumCalc; // We'll calculate and compare checksum

// Holds the ASCII representation of Source Address of the sending zigbee Sensor (matches OneAssetPlace Sensor Key)
char sourceAddr[17];  // Includes room for the null terminator

//SoftwareSerial zbSerial(rcvZigbeeSerial, xmtZigbeeSerial); // RX/TX


EthernetClient client;  // Setup a client to connect to www.oneassetplace.com server

void setup() {
  // ======================================  ONE TIME SETUP  =====================================
  // Open serial communications
  Serial.begin(9600);
  Serial.println("Setup");

  // Give the Ethernet shield some time to wake up
  delay(300);
  

  // Set up SD Card
  /* Note that because the W5100 and SD card share the SPI bus, only one can be active at a time.
      If you are using both peripherals in your program, this should be taken care of by the corresponding libraries.
      If you're not using one of the peripherals in your program, however, you'll need to explicitly deselect it.
      To do this with the SD card, set pin 4 as an output and write a high to it. 
  */
  //pinMode(sdCardControlPin, OUTPUT);
  //digitalWrite(sdCardControlPin, HIGH);

  
  // start the Ethernet connection:
  Serial.println("Starting Ethernet Connection"); 
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Ethernet Begin Fail");
      // no point in carrying on, so do nothing forevermore:
    while(true);
  }
  Serial.println("Ethernet Started"); 
    
  // give the Ethernet shield a second to initialize:
  delay(1000); 

  // Setup comm to the Zigbee
  //zbSerial.begin(9600);
  Serial2.begin(9600);
  Serial.println("Zigbee serial initialized");
 
}


 
void loop()
{
  Serial.println("Loop"); 
  /*
    Here is our overall loop:
      1) Gather any pending reading from any ZigBees (Via controller zigbee on Serial2)
      2) Format it as an HTTP Post
      3) Send it to OneAssetPlace.com
  */
 
   // Reset some variables that get set if and only if Zigbee reports something
   haveDigital = false;
   pair = 0;
   checkSumCalc = 0x00;  // Reset the Checksum
 
    // Read packets and handle if 0x92 (I/O Report)
    if (Serial2.available() >= minFrameSize) {
      // Look for the start byte
      Serial.println("Have minimum frame size available");
      if (Serial2.read() == 0x7E) {
        // This is the start of a frame
        Serial.println("-- Start of Frame --");
        
        // Length is always two bytes
        i = 0;
        while(Serial2.available() > 0 && i < 2) { // Read 2 bytes
          lengthField[i] = Serial2.read();
          i++;
        }
        // Now 0 and 1 of the length Field is the length:
        expectedLength = word(lengthField[0], lengthField[1]);
        actualLength = 0;  // We'll keep track of it
        
        Serial.print("Expected Length:");
        Serial.println(expectedLength);
        
        // Checksum includes everything after the length, but not frame delimiters
        
        // Now we expect a 1-byte frame type
        byte frameType = Serial2.read();
        actualLength++;
        
        addToChecksum(frameType);  // This is part of our checksum
        if (frameType == 0x92) {
          // This is an "I/O Sample Data" frame
          Serial.println("Processing I/O SAMPLE DATA frame");
  
          // Read the 8 byte 64-bit source address
          i = 0;
          Serial.print("Source Address: ");
          while(Serial2.available()>0 && i < 8) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            // We need the ASCII representation of this HEX string (e.g. we need '2F')
            // TODO: Better way thansprintf?
            sprintf(&sourceAddr[i*2], "%02x", incomingbyte); 
            i++;
            Serial.print(incomingbyte, HEX);
            Serial.print(' ');
          }
          Serial.println("");
          Serial.print("Stored Source Address:");
          Serial.println(sourceAddr);
          actualLength += i;
  
          // Read the 2 byte 16-bit source NETWORK address
          i = 0;
          Serial.print("Source NETWORK Address: ");
          while(Serial2.available()>0 && i < 2) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            i++;
            Serial.print(incomingbyte, HEX);
            Serial.print(" ");
          }
          Serial.println("");
          actualLength += i;
  
          // Read the 1 byte Receive Options
          i = 0;
          Serial.print("Receive options: ");
          while(Serial2.available()>0 && i < 1) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            i++;
            Serial.print(incomingbyte, HEX);
            if (incomingbyte == 0x01) {
              Serial.println(" (Packet Acknowledged)");
            }
            else {
              Serial.println(" (Packet was broadcast packet)");
            }
          }
          actualLength += i;
  
    
          // Read the 1 byte Number of Samples (Always 1)
          i = 0;
          Serial.print("Number of Samples: ");
          while(Serial2.available()>0 && i < 1) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            i++;
            Serial.println(incomingbyte, HEX);
          }
          actualLength += i;
  
  
          // Read the 2 byte DIGITAL channel MASK
          i = 0;
          Serial.print("Digital Channel Mask: ");
          while(Serial2.available()>0 && i < 2) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            digitalMaskField[i] = incomingbyte; // save the value
            i++;
            Serial.print(incomingbyte, BIN);
            Serial.print(" ");
          }
          Serial.println("");
          actualLength += i;
  
         // Read the 1 byte ANALOG channel MASK
          i = 0;
          Serial.print("Analog Channel Mask: ");
          while(Serial2.available()>0 && i < 1) {
            incomingbyte = Serial2.read();
            addToChecksum(incomingbyte);  // This is part of our checksum
            analogMaskField = incomingbyte; // Save the value
            i++;
            Serial.println(incomingbyte, BIN);
          }
          actualLength += i;
  
          // Now, if the digital mask was NOT 0 0, then we'll get two bytes of the digital samples
          if (digitalMaskField[0] != 0 || digitalMaskField[1] !=0) {
            // We'll get TWO bytes of digital data
            haveDigital = true;
            i = 0;
            Serial.print("Digital Samples: ");
            while(Serial2.available()>0 && i < 2) {
              incomingbyte = Serial2.read();
              addToChecksum(incomingbyte);  // This is part of our checksum
              digitalSampleField[i] = incomingbyte; // save the value
              i++;
              Serial.print(incomingbyte, BIN);
              Serial.print(" ");
            }
            // Since, even if ALL bits are on, the largest value we would get is
            // decimal 7,423, we'll just store this as an int and parse it out server-side
            digitalVal = word(digitalSampleField[0],digitalSampleField[1]);
            Serial.print("Digital Value as decimal: ");
            Serial.println(digitalVal);
            actualLength += i;
          }
          else {
            // Nothing digital
            haveDigital = false;
            digitalVal = 0;
          }
           
           
          // At this point, for EACH enabled analog input, we'll get a 2-byte value
          if (analogMaskField != 0) {
             // We're going to get any number of 2-byte pairs but stop when our length is met
            
            // Keep reading until we've read expectedLength bytes
            pair = 0;
            while(expectedLength-actualLength > 0) {
  
              while(Serial2.available() < 2) {
               // waiting for two bytes...
               // TODO: Have a deadman timer here
               }
  
              i = 0;
              while(Serial2.available()>0 && i < 2 ) {
                incomingbyte = Serial2.read();
                addToChecksum(incomingbyte);  // This is part of our checksum
                analogSampleField[i] = incomingbyte; // save it
                i++;
                Serial.print(incomingbyte, HEX);
                Serial.print(' ');
              }
              // Save this pair
              analogVals[pair] = word(analogSampleField[0],analogSampleField[1]);
              /* From the manual:
                  Analog values are returned as 10-bit values.  0x0000 represents 0volts and
                  0x3FF = 1.2V (max that can be read).
                  To convert the a/D reading to mV, do the following:
                  AD(mv) = (ADIO reading / 0x3FF)*1200mV
               */            
              
              Serial.print(" analogVals[");
              Serial.print(pair);
              Serial.print("]=");
              Serial.println(analogVals[pair]);
              actualLength += i;
              pair++;
            }
          }  // end if analogMaskField != 0
          
          
          // At this point we've read "expectedLength" bytes, and all that should be left is the checksum
          Serial.print("Bytes Read:");
          Serial.println(actualLength);
          
          if (actualLength != expectedLength) {
            Serial.println("ERROR: Byte count mismatch");
          }
          else {
            Serial.println("Byte count matches");
          }
          
          byte checksumField;
          i = 0;
          while(Serial2.available() > 0 && i < 1) { // Read 1 byte
            checksumField = Serial2.read();
            i++;
          }
          Serial.print("Supplied Checksum=");
          int checksum = int(checksumField);
          Serial.println(checksum);
          Serial.print("Calculated Checksum=");
          int ick = int(0xFF - checkSumCalc);  // You must subtract this from 0xFF
          Serial.println(ick);
          if (checksum != ick) {
              // That packet was ABSOLUTE CRAP!
              haveDigital = false;
              pair = 0;
              Serial.println("-- BAD BACKET --");
          }
          else {
            Serial.println("GOOD PACKET");
          }
        } // End of Frame type 0x92
        else {
          Serial.println("Cannot process unknown frame type");
        }
      } // END of we have enough bytes for a frame
    } // END of reading Zigbee packet 
 
   // If we have EITHER some digital values OR some analog values, we need to post
   if (haveDigital == true || pair > 0) {
     // Our Post Data will be of the form:
     // sensor[key]=kdkfdjfdj...sdfdlsfsdf&reading[raw]={"A0":"836"}
     strcpy(postData, "sensor[key]=");
     
     // The Sensor Key is the Zigbee Unique address
     strcat(postData, sourceAddr);
     
     // Then we append the reading[raw] parameter
     strcat(postData, "&reading[raw]=");     

     // Start the JSON
     // TODO: If there are NO digital and NO analog values, we'll have an empty JSON... OK?
     strcat(postData, "{");
     
     // Assuming there are some ANALOG values, make a JSON to contain them
     if (pair != 0) {
       
       // For each analog pair, add to the json
       for (i=0; i<pair; i++) {
        
        // Analog readings are in the form "An":"value"
        Serial.print("analogVals[");
        Serial.print(i);
        Serial.print("]=");
        Serial.println(analogVals[i]);
        // TODO: Do this without sprintf which is buggy in Arduino
        // TODO: Second one will need a leading comma
        charsInData = sprintf(smallChunk, "\"A%d\":\"%d\"", i, analogVals[i]);
        
        //Serial.println(smallChunk);
        if (i > 0) {
          // Need a comma before the subsequent json
          strcat(postData, ",");
        }
        strcat(postData, smallChunk);
       } // End for each ANALOG pair
 
     }  // End of building the Analog value JSON
 
     // Now, if there were any DIGITAL values, send them as a single word
     if (haveDigital == true) {
         // Yes, we have digital
         if (pair != 0) {
           // We also had some Analog, so the json has been started and has at least 1 element
           // Need a comma before the subsequent json
          strcat(postData, ",");
         }
         
         // Now send this as "digitalvals":"<intval>"
         charsInData = sprintf(smallChunk, "\"digitalvals\":\"%d\"", digitalVal);
         strcat(postData, smallChunk);
     }
 
      // END the json
       strcat(postData, "}");
 
       // For debugging
       Serial.print("POSTING:(");
       Serial.print(postData);
       Serial.println(")");
       Serial.print("Length:");
       Serial.println(strlen(postData));
         
     
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

          // Remember, POST is very picky about newlines and dashes!!.  Note the difference between println() and print()
          // use "client." for real, "Serial." for testing...
          
          client.println( "POST /readings HTTP/1.1" );
          client.println( "Host: www.oneassetplace.com" );
          client.println( "Content-Type: application/x-www-form-urlencoded" );
          client.print( "Content-Length: " );
          client.println( strlen(postData) );
          client.println( "Connection: close" );
          client.println();
          client.print( postData );
          client.println();
            
        }  // END if able to connect to Ethernet board
        else {
          // Failed to connect to the Ethernet Board
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

            // TODO: I think you should also check client.connected again because I think we're waiting for the timeout all the time
            while ((millis() - startWaitTime) < deadManTimer) {
           
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
              Serial.println("Unable to process response");
              // Clear out the response... This is old data now.
              for (i = 0; i < maxToReceive; i++) {
                firstReceived[i] = ' ';
              }
            }
        
            // At this point we've either finished naturally or we've given up, so stop the client
            Serial.println("Stopping client");
            client.stop();
            
            // What was the first chunk of our received data?
            Serial.print("FirstReceived: ");
            Serial.println(firstReceived);
            
            if (strstr(firstReceived, "201") != NULL  || strstr(firstReceived, "302") != NULL) {
              // Successful Transfer!
              // I believe we're getting the 302 moved when we send the same filename, so that is OK too
              Serial.println("201 Success or 302 Moved");
              
            }
      
            
            // Record our status (which really is always going to be "disconnected"
            lastConnected = client.connected();
          } // end if we have either DIGITAL or ANALOG values to send


      Serial.println("Waiting for next...");
      delay(100);
       
   }
   
   
void addToChecksum(byte newval) {
   // Keeps a running Zigbee checksum
   //Serial.print("Checksum: Old=");
   //Serial.print(checkSumCalc);
   //Serial.print(" +");
   //Serial.print(newval);
   checkSumCalc += newval;
   //Serial.print(" B4_AND=");
   //Serial.print(checkSumCalc);
   // Keep only the lowest 8 bits
   checkSumCalc = checkSumCalc & 0xFF;
   //Serial.print(" After=");
   //Serial.println(checkSumCalc);
   
 }
   
   
