/* Linksprite 

Infrared Camera Test Code for MEGA.  This is meant to be a functional test of your camera and SD card only... To be used to verify your
circuit and board before you use oap_motioncamera_mega (which is the full Ethernet Shield, Motion Detector, and camera)
As such, you don't really need any of the motion detector connections outlined below

   Needs:
      1) TTL Camera, preferably infrafred
      2) Ethernet Shield with Micro-SD card
      3) Micro-SD card in shield
      4) Motion-trigger (I used Zilog E-PIR in hardware mode)
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
  
  History:
    2013-01-15 - DECj: Converting to use SoftwareSerial library instead of newsoftserial.
    2013-01-22 - DECj: Modifying it so that it writes to SD card
    2013-02-01 - DECj: Modifying for MEGA board.
    2013-02-05 - DECj: Finished MEGA board testing in the other project.  Continuing mods here.  No longer using SoftwareSerial.

*/

#include <SD.h>

int sdCardControlPin = 4;  // SD Card Control Pin

byte incomingbyte;
byte responseData;  // Used to validate responses
int rc; // response counter
byte responseMessage[110]; // Used to hold responses


// Used to hold the JPEG file size
int sizeMessage[9];
unsigned long imageSize; // holds the size of the JPEG image

int i=0; // stupid loop

// a is our Read Starting address 
// I think we're starting to get probs around 32,379 bytes.  Maybe our "a" needs to be unsigned int or long?
// yes that was the problem.  Switching this to Long...
long a=0x0000;

int j=0,count=0;                         
uint8_t MH,ML;
boolean EndFlag=0;

int packetCount = 0;
int badPackets = 0;
unsigned int bytesRead = 0;

const unsigned long deadManTimer = 6000;
unsigned long startWaitTime;

int pictureTaken = 0;  // Number of pictures we've taken since restarted.

File dataFile;
                               
boolean SendResetCmd();
boolean SendTakePhotoCmd();
void SendReadDataCmd();
void SendStopTakingPicturesCmd();

void setup()
{ 
  Serial.begin(9600);
  // Using MEGA serial 2 which is 17 (RX) and 16 (TX) for the Camera
  Serial2.begin(38400);
  delay(5000);  // Give the camera 5 seconds to finish waking up
  
  // Do not Wait for proper response from camera - it often comes before the Mega is awake
  // NEVER attempt any camera communications before initializing SD card.
  
  // Set up SD Card
  pinMode(sdCardControlPin, OUTPUT);

  if (!SD.begin(sdCardControlPin)) {
    Serial.println("  Card failed, or not present"); // don't do anything more:
    return;
  }
  Serial.println("SD Card initialized.");
      
}



void loop() 
{
  Serial.println("Beginning Loop");
  
  // ----- Create image filename (sequential) -----
  Serial.print("Picture Count: ");
  Serial.println(pictureTaken);
  
  String filename = "OAP_"+String(pictureTaken)+".jpg";   // Create new filename.  Note this integer could easily go to 4 decimal places!
      
  int strlen=filename.length()+1;              // Get the length of the string including the null character
  char charBuf[strlen];                        // initialize a character array to fold the filename
  filename.toCharArray(charBuf, strlen);       // convert the String filename to character array
  
  if (SD.exists(charBuf) == true) {
    // The file exists already on the SD card, delete it.
    SD.remove(charBuf);
  }
  dataFile = SD.open(charBuf, FILE_WRITE);     // Open new file on SD Card
  if (dataFile) {
    Serial.println("File Created");
  }
  
  Serial.println("Sending Reset command");   
  SendResetCmd();

    Serial.println("Taking Picture in 2.5 seconds... Say Cheese!");
     delay(2500);    //After reset, wait 2-3 second to send take picture command
     
     Serial.println("Sending Take Photo Command");  
     SendTakePhotoCmd();

    // Get Image Size **************************************
      Serial.println("Sending Read Image Size command"); 
      delay(80);
      GetImageSize();
      delay(80);
      Serial.print("Image Size: ");
      // Returns: 76 00 34 00 04 00 00 XH XL
      // Where XH XL is the length of the picture file, MSB in the front and LSB in the end.
      i=0;
      while(Serial2.available()<9) // wait for 9 bytes
      {
      }

      while(Serial2.available()>0 && i < 9) // Read the serial buffer contents
      {
        incomingbyte=Serial2.read();
        sizeMessage[i] = incomingbyte;
        i++;
      }  
      
      // Yank the image size they sent us from the response
      imageSize=word(sizeMessage[7], sizeMessage[8]);

      Serial.println(imageSize);
    
    
      // Now, Read the JPEG file content
      byte packet[42];
      
      // Setup for a nice, clean reading of the packets
      a=0x0000;  // This is our buffer address!
      EndFlag = 0;
      bytesRead = 0;
      packetCount = 0;
      count=0;
         
      while(!EndFlag)
      {  
 
        // Sometimes there might be gook in the buffer after the last packet and before
        // we ask it for more data... Ignore that.
        dumpCameraInput(false);  // Do this in silent mode!
        
        // Tell the camera we would like a picture and explain to it our buffer size and
        // requested delay... 
        SendReadDataCmd();
          
          i = 0;
          // Notice we read 42 bytes from the serial buffer.  That is:
          // 5 Byte preamble (76 00 32 00 00)
          // 32 Bytes of data (that is what we asked for)
          // 5 Byte postamble (76 00 32 00 00)       
          while(i < 42)    // Read 42 bytes in from serial buffer
          {                  
            while(Serial2.available() > 0)
            {      
              incomingbyte = Serial2.read();
              packet[i] = incomingbyte;
              i++;
            }
          }

          // We have now read one packet
          packetCount++;
           
           //  Test packet to see if preamble 5 bytes equals the postamble 5 bytes
          // (This is a pretty lame test but it is a start)
          for (i=0; i<5; i++)  
          {
            if( !(packet[i] == packet[i+37]) )
              {
                badPackets++;
                // TODO: Could you ever re-ask for data?
              }
          }
         
  
          // Now, loop through the useful bytes of the data packet and write to the card
          // We do this in a self-incremented loop from 5 to < 37 because we need to bail out 
          // immediately when we see the end of the JPG data
          i = 5;
          while (!EndFlag && i < 37)     
          {
            bytesRead++;
            
            //       Write data to packet to SD card 
            if (dataFile) 
            {
              dataFile.write(packet[i]);
            }
            else 
            {
              Serial.println("FILE ERROR - ABORTING PICTURE");
              // It will never recover from this error, so just bail on this picture
              EndFlag = 1;
            }
            
            if((packet[i-1] == 0xFF) && (packet[i] == 0xD9))  //Check if the picture is over (a JPEG ends with FF D9)
            {
              dataFile.close();
              Serial.println("File is closed");
              EndFlag = 1;
              pictureTaken++;
            }
             
             // If we got this far, we are still good to continue processing this packet
             i++;
             
          } // end looping through useful bytes of packet 
           
      }// end of reading the packets until finished     

      Serial.print("Packets: ");
        Serial.println( packetCount);
        Serial.print("Bad packets: ");
        Serial.println( badPackets);
        Serial.print("Bytes read: "); 
        Serial.println(bytesRead);
        Serial.print("Bytes remaining: ");             
        Serial.println(imageSize-bytesRead);

     Serial.println("Issuing Stop Taking Pictures command");
     SendStopTakingPicturesCmd();

     Serial.println("Issuing Reset command");
     SendResetCmd();
         
     Serial.println("Sleeping before taking another picture...");
     delay(5000); 
     
}



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

   startWaitTime = millis();
    i = 0;
    while(i <= 4 && (millis()-startWaitTime < deadManTimer))
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
    
    if (responseMessage[0] == 0x76 && responseMessage[2] == 0x26) {
    Serial.println("Camera has been reset");
    return true;
    }
    else
    {
      Serial.println("Warning: Expected reset response not found");
      return false;
    }           
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


//Send take picture command
// Just tells the camera to take a picture, doesn't send the data to us.
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
      
       startWaitTime = millis();
    i = 0;
    while(i <= 4 && (millis()-startWaitTime < deadManTimer))
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
    
    if (responseMessage[0] == 0x76 && responseMessage[2] == 0x36) {
    Serial.println("Take Picture Command Sent");
    return true;
    }
    else
    {
      Serial.println("Warning: Expected Take Picture response not found");
      return false;
    }           
        
}


// Asks the camera for the size of the image it has taken.
void GetImageSize() 
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





