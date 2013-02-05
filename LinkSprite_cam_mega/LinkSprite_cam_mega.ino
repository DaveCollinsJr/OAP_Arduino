/* Linksprite 

Infrared Camera Test Code for MEGA.  This is meant to be a functional test of your camera and SD card only... To be used to verify your
circuit and board before you use oap_motioncamera_mega (which is the full Ethernet Shield, Motion Detector, and camera)
As such, you don't really need any of the motion detector connections outlined below


   Needs:
      1) TTL Camera, preferably infrafred
      2) Ethernet Shield with Micro-SD card
      3) Micro-SD card in shield
      4) Motion-trigger (I used Zilog E-PIR in hardware mode)
      5) Arduino (Uno used for this project)
      A) Update the "mac" (MAC Address) in credentials.h from the shield, then place Arduino Ethernet Shield on the Arduino
    
    Circuit (MEGA 2560):  
      A) Pin 4 (Digital Output) RESERVED: SS for SD Card
      B) Pin 50 (Digital Output) to "Txd(Out)" Pin of camera (was Brown wire, third wire in our camera)
          Remember!  The Camera's TRANSMIT is _OUR_ RECEIVE and vice-versa!
      C) Pin 51 (Digital Output) to "Rxd(In)" Pin of camera (was Red wire, fourth wire in our camera)
      D) Pin 7 (Digital Output) To "SLEEP" (pin 7) of Motion Detector - used to wake it up
      E) Pin 8 (Digital Input) To "MOTION" (pin 5) of Motion Detector - LOW when motion is sensed
      F) Pin 10 (Digital Output) RESERVED: SS for Ethernet Shield
      G) Pin 11 (Digital Output) RESERVED: SPI to Ethernet Shield
      H) Pin 12 (Digital Output) RESERVED: SPI to Ethernet Shield
      I) Pin 13 (Digital Output) RESERVED: SPI to Ethernet Shield
      J) +5V to +5V of camera (was Grey wire, first wire in our camera)
      K) GND to GND of camera (was Purple wire, second wire in our camera)
      L) GND to GND of Motion Detector (Pins 1 (GND), 3 (DLY), 4 (SNS), and 8 (GND) of ePIR)
      M) +3.3V to Motion Detector pins 2 (VDD), 6 (LG)

  Notes: 
    Not all pins on the Mega and Mega 2560 support change interrupts, so only the following
      can be used for RX: 10, 11, 12, 13, 50, 51, 52, 53, 62, 63, 64, 65, 66, 67, 68, 69
  
  History:
    2013-01-15 - DECj: Converting to use SoftwareSerial library instead of newsoftserial.
    2013-01-22 - DECj: Modifying it so that it writes to SD card
    2013-02-01 - DECj: Modifying for MEGA board.

*/

#include <SoftwareSerial.h>
#include <SD.h>

int sdCardControlPin = 4;  // SD Card Control Pin

byte incomingbyte;
byte responseData;  // Used to validate responses
int rc; // response counter
byte responseMessage[20]; // Used to hold responses

//SoftwareSerial(rxPin, txPin) 
SoftwareSerial mySerial(50,51);                     //Configure pin (rx) and (tx) as soft serial port
// Try using MEGA serial 2 which should be: 17 (RX) and 16 (TX)


// Used to hold the JPEG file size
int sizeMessage[9];
int imageSize; // holds the size of the JPEG image

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

int pictureTaken = 0;  // Number of pictures we've taken since restarted.

File dataFile;
                               
void SendResetCmd();
void SendTakePhotoCmd();
void SendReadDataCmd();
void SendStopTakingPicturesCmd();

void setup()
{ 
  Serial.begin(9600);
  mySerial.begin(38400);
  Serial2.begin(38400);
  delay(1000);
  
//  if (Serial2.isListening() )
 //   Serial.println("Camera is listening");
  
  // Wait for proper response from camera
  Serial.println("Waiting for camera init end"); 
  i = 0;
  while(i <= 15)
  {
    if (Serial2.available()>0) {
      incomingbyte=Serial2.read();
      responseMessage[i] = incomingbyte;
      Serial.print(responseMessage[i]);
      i++;
    }
  }
  
  // Did we get the expected response?
  if (responseMessage[0] == 0x36 && responseMessage[1] == 0x32 && responseMessage[14] == 0x0A) {
    Serial.println("Camera is initialized");
  }
  else
    Serial.println("WARNING: Camera not initialized");  
  
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
  
  // --- Clear the input buffer - we haven't asked for anything yet ---
  Serial.println("Clearing input buffer"); 
  while(mySerial.available()>0)
  {
    incomingbyte=mySerial.read();
  }
 
  Serial.println("Sending Reset and clearing buffer until we issue take picture command");   
  SendResetCmd();
  while(mySerial.available()>0)
  {
    incomingbyte=mySerial.read();
  }

    Serial.println("Taking Picture in 2.5 seconds... Say Cheese!");
     delay(2500);                               //After reset, wait 2-3 second to send take picture command
     
     // --- Clear the input buffer - we haven't asked for anything yet ---
      Serial.println("Clearing input buffer"); 
      while(mySerial.available()>0)
      {
        incomingbyte=mySerial.read();
      }
         
     Serial.println("Sending Take Photo Command");  
     SendTakePhotoCmd();
     // They should respond with 76 00 36 00 00
     while(mySerial.available()>0)
      {
        incomingbyte=mySerial.read();
      }   


    // Get Image Size **************************************
      Serial.println("Sending Read Image Size command"); 
      delay(80);
      GetImageSize();
      delay(80);
      Serial.print("Image Size: ");
      // Returns: 76 00 34 00 04 00 00 XH XL
      // Where XH XL is the length of the picture file, MSB in the front and LSB in the end.
      i=0;
      while(mySerial.available()<9) // wait for 9 bytes
      {
      }

      while(mySerial.available()>0) // Read the serial buffer contents
      {
        incomingbyte=mySerial.read();
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
        while(mySerial.available() > 0) 
        {
          incomingbyte=mySerial.read();
        }  
        
        
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
            while(mySerial.available() > 0)
            {      
              incomingbyte = mySerial.read();
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
void SendResetCmd()
{
     // As of Arduino 1.0 the 'BYTE' keyword is no longer supported.
     // Please use Serial.write() instead.
     // we must cast to (byte) to tell the compiler how to interpret these ZERO values
     // can use (byte) but it is recommended to use (uint8_t)
     // We Send: 65 00 26 00
     // It Returns: 76 00 26 00

      // First, flush the buffer
      //dumpInputBuffer();

      mySerial.write(0x56);
      mySerial.write((uint8_t)0x00);
      mySerial.write(0x26);
      mySerial.write((uint8_t)0x00);
                
}



//Send take picture command
// Just tells the camera to take a picture, doesn't send the data to us.
void SendTakePhotoCmd()
{    // Send: 56 00 36 01 00
     // Response: 76 00 36 00 00  (5 Bytes)
     
      // Send the take picture command
      mySerial.write(0x56);
      mySerial.write((uint8_t)0x00);
      mySerial.write(0x36);
      mySerial.write(0x01);
      mySerial.write((uint8_t)0x00);  
        
}


// Asks the camera for the size of the image it has taken.
void GetImageSize() 
{ 
  mySerial.write(0x56); 
  mySerial.write(byte(0x00)); 
  mySerial.write(0x34); 
  mySerial.write(0x01); 
  mySerial.write(byte(0x00)); 
}


//Read data
void SendReadDataCmd()
{
      MH=a/0x100;  // divide by 0x100 (256 Decimal)
      ML=a%0x100;  // Modulus of that division

      mySerial.write(0x56);
      mySerial.write((uint8_t)0x00);
      mySerial.write(0x32);
      mySerial.write(0x0c);
      mySerial.write((uint8_t)0x00); 
      mySerial.write(0x0a);
      mySerial.write((uint8_t)0x00);
      mySerial.write((uint8_t)0x00);

      // This is the "Init Address" from which we want to read
      mySerial.write(MH);
      mySerial.write(ML);
      
      mySerial.write((uint8_t)0x00);
      mySerial.write((uint8_t)0x00);

      // This is the "Data Length" (KK KK)  In our case 0x20 = 32 Bytes
      mySerial.write((uint8_t)0x00);
      mySerial.write(0x20);
      
      // This is the "Spacing Interval" we want which is recommended to be small such as 00 0A
      mySerial.write((uint8_t)0x00);  
      mySerial.write(0x0a);

      // Each time we Read Data, increase the starting address by 0x20 (32 Decimal)...
      // This is our buffer size.
      a += 0x20;
}



void SendStopTakingPicturesCmd()
{ // Tells the damn camera to stop taking pictures
  // It should respond with: 76 00 36 00 00 (FIVE bytes)
      mySerial.write(0x56);
      mySerial.write((uint8_t)0x00);
      mySerial.write(0x36);
      mySerial.write(0x01);
      mySerial.write(0x03);    

}





