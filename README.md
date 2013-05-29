*OAP_Arduino*
=============

One Asset Place Arduino Projects

This project holds all of the Open Sourced One Asset Place utilities and programs.  For most of these projects, you can find detailed examples at [One Asset Place](http://www.oneassetplace.com)  Specifically, see our [Tutorials Index](http://www.oneassetplace.com/tutorials)

Please feel free to reach out to us with questions, suggestions, or comments!  davecollins@oneassetplace.com

NOTE: If you are very new to Arduino, please read the section at the end titled "About Libraries".


This repository currently holds the following projects:
-------------------------------------------------------

*OAP_motion_camera_mega*:
A full working project [and accompanying tutorial](http://www.oneassetplace.com/pages/motion_camera_mega) that allows you to build a motion-triggerd still camera that will take pictures and upload them to your One Asset Place dashboard.  Requires an Arduino Mega 2650.  Full circuit diagrams and sofware are included, and the circuit diagrams can also be found on [Fritzing](http://http://fritzing.org/projects/motion-camera-for-one-asset-place/)

*OAP_sens_2d_wifly_uno*:
A full working project [and accompanying tutorial](http://www.oneassetplace.com/pages/BuildWifiSensorArduino) that has you building a wireless sensor based on an Arduino with two digital inputs and a WiFi connection.

*OAP_zigbee_controller_ethernet_mega*:
A full working project for building a *mesh network* using Zigbees and One Asset Place.  Full tutorial coming soon.  In the meantime, get up to speed on mesh networks with [our mesh network overview](http://www.oneassetplace.com/pages/whymeshbetter).  


*OAP_Setup_EEPROM*:
A utility program that allows you to easily maintain a set of strings written to the Arduino's EEPROM.  _Needed to setup the camera code_.  Great technique if you need to reduce the memory footprint of an Arduino sketch that has a lot of long strings (in our case HTTP Posts)

*LinkSprite_cam_mega*:
A utility piece of code that can be used to test just your camera and image-saving capabilities.  Useful in proceeding step-by-step through this rather advanced camera project.

*LinkSprite_cam_mega_test*:
A utility piece of code to test whether or not you are communicating properly to the camera



About Libraries:
================
Your local Arduino installation must have libraries installed if the code has #include statements.  For example,
the OAP_motion_camera_mega requires the following (from the include statements): 
*Ethernet.h*, *SPI.h*, *SdFat.h*, *EEPROM.h*

[Arduino instructions for installing libraries](http://arduino.cc/en/Guide/Libraries)

All of the libraries except SdFat are [standard Arduino libraries](http://arduino.cc/en/Reference/Libraries)

SdFat is hosted on [code.google.com](https://code.google.com/p/sdfatlib/)  Specifically, grab the [most recent SdFat download](https://code.google.com/p/sdfatlib/downloads/list)

Once you download those libraries and install them per the first link, completely exit and re-enter your
Arduino IDE then try compiling again.
