/*
    Example of reading sensors and performing an HTTP POST to a Rails back-end server
    
    Circuit:
      Digital Output Pin 6 (PWM) to Resistor, then to long leg (anode, +) of LED.  This is Left Door Open LED
        Cathode (short, -) of that LED to GND
      Digital Output Pin 9 (PWM) to Resistor, then to long leg (anode, + ) of LED.  This is Right Door Open LED    
        Cathode (short, -) of that LED to GND
      Digital Input Pin 3 to the Hall Effect Sensor (magnetic switch) the other side of that switch to GND.  This is left door open switch.
      Digital Input Pin 5 to the Hall Effect Sensor (magnetic switch) the other side of that switch to GND.  This is right door open switch.
      Digital Output Pin 7 to the CTRL of the relay circuit
      GND of the relay circuit to GROUND
      5V of the relay circuit to +5V
      VIN from the Lower Arduino should NOT go to the shield, but should instead go to one of the RELAY LOAD points
      The other RELAY LOAD point goes to VIN of the shield.  This allows us to control power to the WiFly board from this code
      
      
    Shout Outs and Attributions:
      Many thanks (and credit) to John Hart at LifeBoat Farms in New Zealand for inspiration and code:
                http://lifeboat.co.nz/the-finished-wireless-water-sensor/
      Many thanks to Mike Holden at Holden Technology for porting the older Spark Fun example code to the new SPI
      And of course to the gang at Spark Fun for continuously offering up great products and ideas.
          
    Copyright/License:
    This software is provided under the Creative Commons Attribution (CC) license
    See license.md or http://www.tldrlegal.com/l/CC for the actual license
    
*/

#include "WiFly.h"
#include "Credentials.h"
#include <SPI.h>
 
WiFlyClient client( servername, serverport);
 
// give the LED pins a name:
int leftDoorLEDPin = 6;
int rightDoorLEDPin = 9;

int leftDoorPin = 3;
int rightDoorPin = 5;

// This pin is connected to a relay and controls power to the WiFly Shield
// This is optional, but you'll have better long-term stability if you use the relay circuit
int shieldPowerPin = 7;

  // Warning!  This must be wide enough for your longest data string that is ever possible!
const int longestPostData = 130;

// the sentCount lets us tell how many consecutive sendings we have had...
int sentCount = 0;

// Keep track of whether or not we have joined the network, and use that info to proceed
boolean joinedNetwork = false;

 // PWMValue / 51 = Volts    (163 / 51 = 3.2volts)
 // Remember you still need to use resistors on the LED's or you'll burn them out!
#define onVoltage 163
#define offVoltage 0

// Change this to 1 to flip the open/closed logic if, for example, you aren't using reed switches
#define readingDoorClosed 0

void setup()
{
  Serial.begin( 9600 ); 
  
  // initialize the digital pin as an output.
  pinMode(leftDoorLEDPin, OUTPUT);
  pinMode(rightDoorLEDPin, OUTPUT);
  
  // Initialize the WiFly shield control pin as an output
  pinMode(shieldPowerPin, OUTPUT);
  
  // initialize both warehouse door pins as digital inputs with built-in pull-up resistors
  // PULL_UP resistor causes the FLOATING (circuit open) value to be HIGH=1
  pinMode(leftDoorPin, INPUT_PULLUP);
  pinMode(rightDoorPin, INPUT_PULLUP);
   
 
   // NOTE: We moved the whole WiFly.begin join code from setup to the loop
   // since we are now powering it up and down.
   digitalWrite(shieldPowerPin, LOW); // Power off the WiFly Shield
}

 
void loop()
{
  
  // Warning!  This must be wide enough for your longest data string that is ever possible!
  // Our string is in the format of "reading[sensor_id]=1&reading[reported]=x";
  char postData[longestPostData];

  // Get the LED status
  // In this circuit, 1 will be DOOR CLOSED, 0 will be DOOR OPEN
  int leftDoorStatus, rightDoorStatus, doorStatus;
  int charsInData;
  
  Serial.println("Powering up Shields...");  
  // --- Power up the WiFly shield, pause, and then try to join the network. ---
  digitalWrite(shieldPowerPin, HIGH); // trigger relay to power WiFly shield VIN
  delay(600); //allow WiFly time to boot

  // Check on the left door
  leftDoorStatus = handleDoorReadingAndLED(leftDoorPin, leftDoorLEDPin);
  
  // Check on the right door    
  rightDoorStatus = handleDoorReadingAndLED(rightDoorPin, rightDoorLEDPin);


  Serial.println("Beginning");
  WiFly.begin();
  
  Serial.println("Attempting to join network"); 
  if( !WiFly.join( ssid, passphrase ) ) 
  {
    Serial.println( "..Unable to join" );
       // Flash Lights to alert humans, don't try anything this cycle
     flashLightsForProblem(leftDoorLEDPin, rightDoorLEDPin, onVoltage);
     joinedNetwork = false;
  }
  else
  {
    Serial.println("..Joined");  
    joinedNetwork = true;
  }
  
  // --- If we got this far, the WiFly shield is powered up and we are on the network ---
  
  if (joinedNetwork == true) {

    // Use sprintf to format our postData char array with the exact POST DATA we need to POST to the server.
    // WARNING!  The Arduino implementation of sprintf has some shortcomings!  It won't handle floats and can be a bit buggy
    // Also if your SET COMM SIZE (flush size) is not high enough, you'll get strange linefeeds in the middle of this data!!
    charsInData = sprintf(postData, "reading[sensor_id]=1&sensor[key]=PUTYOURONEASSETPLACEKEYHERE&reading[raw]={\"1\":\"%d\",\"2\":\"%d\"}", leftDoorStatus, rightDoorStatus);
    Serial.println(postData);

    Serial.println("Attempting to connect and POST");  
    if( client.connect() ) 
    {
      //Serial.println( data );
      // Remember, POST is very picky about newlines.  Note the difference between println() and print()
      // use "client." for real, "Serial." for testing...
    
      client.println( "POST /readings HTTP/1.1" );
      client.print( "Host: ");
      client.println( servername );
      client.println( "Content-Type: application/x-www-form-urlencoded" );
      client.print( "Content-Length: " );
      client.println( strlen(postData) );
      client.println( "Connection: close" );
      client.println();
      client.print( postData );
      client.println();
    }
    else {
      // Failed to connect to the Wifly Board
      // 01/03/2013 - Having issues where it stops sending, yet the C code seems to be looping just fine
      // (for instance, the LED's properly go on/off as doors are opened / closed)
      // Will reset the client here.  Also will "flash" the LED's to let you know this occurred
      Serial.println("Failed to connect");  
     flashLightsForProblem(leftDoorLEDPin, rightDoorLEDPin, onVoltage);
    }
      // In the future, look for responses here and possibly control something.
      Serial.println("Stopping client...");  
      client.stop();  // Stop the client.  We are done
  } // End of if joinedNetwork
    
    // increase our sent count.    
    sentCount++;
  
   // Pause a bit after transfer.  
   delay( 100 );
   
  Serial.println("Shutting down WiFly shield...");  
   // Now, power OFF the WiFly since it seems unstable over several days
   digitalWrite(shieldPowerPin, LOW); // Power down WiFly
  
   // Wait 15 seconds, then do it again!
  delay( 15000 );
}



int handleDoorReadingAndLED(int doorPin, int doorLEDPin) {
  /* This function checks on a given door's Pin, lights or turns off the LED as appropriate
      and returns 1 if the door was OPEN and 0 if the door was CLOSED */
  int doorStatus;
  int reading = 0;
  reading = digitalRead(doorPin);
  //Serial.println(reading);

  if (reading == readingDoorClosed)
  {
    // This door is CLOSED, turn OFF its LED
    analogWrite(doorLEDPin, offVoltage);
    doorStatus=0;
  }
  else
  {
    // The right door is OPEN, LIGHT the LED
    analogWrite(doorLEDPin, onVoltage);
    doorStatus=1;
  }
  
  return doorStatus;
  
}

void flashLightsForProblem(int leftLightPin, int rightLightPin, int highVoltage) {
  for (int x=0; x<4; x++)
  {
    analogWrite(leftLightPin, 0);
    analogWrite(rightLightPin, 0);
    delay (250);  
    analogWrite(leftLightPin, highVoltage);
    analogWrite(rightLightPin, highVoltage);
    delay (250);
    analogWrite(leftLightPin, 0);
    analogWrite(rightLightPin, 0);
  }
    
}



