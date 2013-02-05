/*

  OAP_Setup_EEPROM
  
  OK So, unfortunately, with only 2Kb of RAM and 32K of flash (on the UNO), we're going to have to resort to some trickery
  to get this program working.  Step 1 will be moving ALL longish character strings to the EEPROM and reading/writing
  them as necessary.
  
  You need to replace the line after sensor[key] with your www.oneassetplace.com key!
  
  The MEGA has twice the RAM but I still think it is a good idea to get these strings out of the code.  Gives us more debugging freedom.
  
*/
#include <EEPROM.h>

// If we're reading and we get to this, give up!
#define WayTooLong 500


int length = 0; // Length of what we wrote
int x = 0; 
int start = 0;
char ch;

// You MUST setup the array with all of the strings you want to write, and tell us how many there are
// Also give us an array of "starts" we can fill so we can give you your lookup table values.

#define NUMSTRINGS 14
int startValues[NUMSTRINGS];

// Set this to FALSE if you've already written the strings and you just want your lookup table.
bool writeNewStrings = true;

// These values will work with www.oneassetplace.com - just update it with your key.
char* stringsToWrite[] = {
  "POST /readings HTTP/1.1", 
  "Content-Type: multipart/form-data; boundary=",
  "----------------------------283499ce49c6",
  "Host: www.oneassetplace.com",
  "Content-Length: ",
  "Content-Disposition: form-data; name=\"",
  "reading[sensor_id]\"",
  "sensor[key]\"",
  "517c070d8xxxxxxxxxxxxxxxxxxxe801b424d27",
  "reading[raw]\"",
  "{\"5\":\"image\"}",
  "reading[image]\"; filename=\"",
  "Content-Type: image/jpeg",
  "Connection: close"
};


void setup () {
  // Open serial communications
  Serial.begin(9600);
  
  Serial.println("");
  Serial.println("Setup");
  Serial.println((int)freeRam);
  
}


void loop () {
  Serial.println("In Loop starting at 0");
  
  if (writeNewStrings == true) {
    // We want to write strings
    start = 0;
    for (x = 0; x < NUMSTRINGS; x++) {
             
      // Update what is on the EEPROM
      Serial.print("Storing #");
      Serial.print(x+1);
      Serial.print(" (");
      Serial.print(stringsToWrite[x]);
      Serial.print(") start Addr:");
      Serial.println(start);
      
      startValues[x] = start;
      length = ntarrToEEPROM(stringsToWrite[x], start);
      Serial.print("Length was: ");
      Serial.println(length);
      
      // Since length does NOT include the null terminator, we need to bump up start by length + 1
      start += (length + 1);
    
      Serial.println("Finished Writing to EEPROM");
      Serial.println("");
    }
  }    
  
    
  // ====================== Test Reading ==============================
  // See if we can read and print the boundary...
  
  Serial.println("The strings in your EEPROM:");
  Serial.println("START     String");
  Serial.println("-----     -------------------");
  for (x = 0; x < NUMSTRINGS; x++) {
    start = startValues[x];
    Serial.print(start);
    Serial.print("     ");
    writeNtarrFromEEPROM(start);
  }
  
  
  
  Serial.println("All Finished.  Freezing");
  while (1==1)
  { // Do Nothing
  }
  
}


int ntarrToEEPROM (char* writeData, int startAddr) {
 // Writes a null-terminated char array to the EEPROM starting at the address
 // and returns the length (number chars written).  Length does NOT include terminator!
 int currentAddr = startAddr;
 int idx = 0;
 
 int slen = strlen(writeData);
 Serial.print("-->Length is:");
 Serial.println(slen);
 
 for (idx = 0; idx < slen; idx++) {
   EEPROM.write(currentAddr, writeData[idx]);
   currentAddr++;
 }
 // Write the terminator
 EEPROM.write(currentAddr, '\0');
 
 // Return the length of the string (not including term)
 return slen;
   
}


int writeNtarrFromEEPROM(int startAddr) {
  // Writes a null-terminated string (that you must have previously written to EEPROM) from the
  // EEPROM starting at a specific address
   
    int length = 0;
    int currAddr = startAddr;
    do {
      ch = EEPROM.read(currAddr);
      currAddr++;
      length++;
      if (ch != '\0')
        Serial.print(ch);
      if (length == WayTooLong)
        Serial.println("Aborting!");
    } while (ch != '\0' && length <= WayTooLong) ;
    
    Serial.println("");
  
}



int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}


