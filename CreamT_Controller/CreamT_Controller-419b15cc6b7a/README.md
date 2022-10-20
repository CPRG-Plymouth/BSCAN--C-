# Sick TiM7xx Controller firmware
tested on: STM Nucleo-F207ZG 

This is an example of a  program to establish communication with Sick TiM7xx series
laser scanners and log measurement data.
 
**Note:** The current example is limited to the ethernet interface on supported devices.
To use the example with a different interface, you will need to modify main.cpp and
replace the EthernetInterface class with the appropriate network interface.

Scanner Interface:

Webserver:
Link: http://IPaddr:8080/CreamT/
The webserver runs from index.html, this file must be present on the top level of the SD card.
Modifications to index can be uploaded remotely via the file upload form 
