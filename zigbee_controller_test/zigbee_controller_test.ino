/*
 * ********* OAP zigbee_controller_test ********
 * A program to interface an Zigbee (we used xbee) in controller mode (tied to an Arduino) with
 * any number of Remote zigbees for monitoring and control...
 */

#include <SoftwareSerial.h>

int rcvZigbeeSerial = 5;  // We receive - hook to Zigbee DOUT
int xmtZigbeeSerial = 6;  // We transmit - hook to Zigbee DIN

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

int analogVals[4]; // At most we can hold 4 analog values (A0 through A3)

SoftwareSerial zbSerial(rcvZigbeeSerial, xmtZigbeeSerial); // RX/TX

void setup() {
  // For comm and debugging
  Serial.begin(9600);
  Serial.println("Initializing Zigbee serial");
 
  // Setup comm to the Zigbee
  zbSerial.begin(9600);
  Serial.println("Zigbee serial initialized");
  
}


void loop() {
  // For now, just echo what we get from the Zigbee
  
//  while(zbSerial.available()>0) {
//    incomingbyte = zbSerial.read();
//    Serial.print(incomingbyte, HEX);
//    Serial.print(' ');
//  }
//  Serial.println();
  
  // Read packets and handle if 0x92 (I/O Report)
  if (zbSerial.available() >= minFrameSize) {
    // Look for the start byte
    Serial.println("Have minimum frame size available");
    if (zbSerial.read() == 0x7E) {
      // This is the start of a frame
      Serial.println("-- Start of Frame --");
      
      // Length is always two bytes
      i = 0;
      while(zbSerial.available() > 0 && i < 2) { // Read 2 bytes
        lengthField[i] = zbSerial.read();
        i++;
      }
      // Now 0 and 1 of the length Field is the length:
      expectedLength = word(lengthField[0], lengthField[1]);
      actualLength = 0;  // We'll keep track of it
      
      Serial.print("Expected Length:");
      Serial.println(expectedLength);
      
      // Now we expect a 1-byte frame type
      byte frameType = zbSerial.read();
      actualLength++;
      
      if (frameType == 0x92) {
        // This is an "I/O Sample Data" frame
        Serial.println("Processing I/O SAMPLE DATA frame");

        // Read the 8 byte 64-bit source address
        i = 0;
        Serial.print("Source Address: ");
        while(zbSerial.available()>0 && i < 8) {
          incomingbyte = zbSerial.read();
          i++;
          Serial.print(incomingbyte, HEX);
          Serial.print(' ');
        }
        Serial.println("");
        actualLength += i;

        // Read the 2 byte 16-bit source NETWORK address
        i = 0;
        Serial.print("Source NETWORK Address: ");
        while(zbSerial.available()>0 && i < 2) {
          incomingbyte = zbSerial.read();
          i++;
          Serial.print(incomingbyte, HEX);
          Serial.print(" ");
        }
        Serial.println("");
        actualLength += i;

        // Read the 1 byte Receive Options
        i = 0;
        Serial.print("Receive options: ");
        while(zbSerial.available()>0 && i < 1) {
          incomingbyte = zbSerial.read();
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
        while(zbSerial.available()>0 && i < 1) {
          incomingbyte = zbSerial.read();
          i++;
          Serial.println(incomingbyte, HEX);
        }
        actualLength += i;


        // Read the 2 byte DIGITAL channel MASK
        i = 0;
        Serial.print("Digital Channel Mask: ");
        while(zbSerial.available()>0 && i < 2) {
          incomingbyte = zbSerial.read();
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
        while(zbSerial.available()>0 && i < 1) {
          incomingbyte = zbSerial.read();
          analogMaskField = incomingbyte; // Save the value
          i++;
          Serial.println(incomingbyte, BIN);
        }
        actualLength += i;

        // Now, if the digital mask was NOT 0 0, then we'll get two bytes of the digital samples
        if (digitalMaskField[0] != 0 || digitalMaskField[1] !=0) {
          // We'll get two bytes of digital data
          i = 0;
          Serial.print("Digital Samples: ");
          while(zbSerial.available()>0 && i < 2) {
            incomingbyte = zbSerial.read();
            digitalSampleField[i] = incomingbyte; // save the value
            i++;
            Serial.print(incomingbyte, BIN);
            Serial.print(" ");
          }
          Serial.println("");
          actualLength += i;
        }
         
        // At this point, for EACH enabled analog input, we'll get a 2-byte value
        if (analogMaskField != 0) {
           // We're going to get any number of 2-byte pairs but stop when our length is met
          
          // Keep reading until we've read expectedLength bytes
          pair = 0;
          while(expectedLength-actualLength > 0) {

            while(zbSerial.available() < 2) {
             // waiting for two bytes...
             // TODO: Have a deadman timer here
             }

            i = 0;
            while(zbSerial.available()>0 && i < 2 ) {
              incomingbyte = zbSerial.read();
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
        
        // At this point we've read "expectedLength" bytes, and all that should be left is the checksum
        Serial.print("Bytes Read:");
        Serial.println(actualLength);
        
        if (actualLength != expectedLength) {
          Serial.println("ERROR: Byte count mismatch");
        }
        else {
          Serial.println("Byte count matches");
        }
        }
        
        
        byte checksumField;
        i = 0;
        while(zbSerial.available() > 0 && i < 1) { // Read 1 byte
          checksumField = zbSerial.read();
          i++;
        }
        Serial.print("Checksum=");
        int checksum = int(checksumField);
        Serial.println(checksum);
        Serial.println("");
      }
      else {
        Serial.println("Cannot process unknown frame type");
      }
    }
  } 
  
  delay(100); 
 
}
