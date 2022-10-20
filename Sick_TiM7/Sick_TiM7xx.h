/*
Sick TiM5xx series laser scanner control library
*/

#include "mbed.h"

#ifndef SICK_TIM5XX_H
#define SICK_TIM5XX_H

class TiM7xx
{
    static const int dataBufferSize = 8192; //add to constructor
    static const int fileBufferSize = 17920;
public:
    TiM7xx(TCPSocket &socket)
        :_socket{socket} {} //constructor
    
    //Sick SOPAS control functions - in order given in Telegram listing document
    //Communication
    int init();
    int initone();
    int setAccessMode(int mode); //mode 2: Maintenance; 3: Authorized Client; 4: Service
    int pollTCPsocket(bool state);
    //Scan Data
    int getData(int &timeStamp_ms, char (&distanceBuffer)[dataBufferSize], char (&rssiBuffer)[dataBufferSize]);
    int getTime(int &timeStamp_ms, char *info); //parse device measurement time - returns current device timestamp (milliseconds since power on)
    //File I/O
    int buildFile(int timeStamp_s, int timeStamp_ms,char (&fileBuffer)[fileBufferSize], char (&distanceBuffer)[dataBufferSize],char (&rssiBuffer)[dataBufferSize]);
    int headerWrite(FILE* f);
    int dataWrite(FILE* f);
    //Basic settings
    int startMeasurement(); //starts laser & motor running
    int stopMeasurement();  //turns off laser and motor
    int rebootDevice();
    int saveParam();        //permanently save changed parameters to device EEPROM
    int run();              //logout and activate parameter changes
    //Measurement output
    int scanDataCfg();      //configure scan data content
    int scanDataCfg1();
    int outputRangeCfg();   //configure scan output range & resolution
    int readOutputRange();
    int pollOneTelegram();
    int pollContTelegram(bool state);
    //Time stamp
    int readDeviceTime();
    //Filter
    
    //Encoder
    
    //Outputs
    
    //Inputs
    
    //Status
    int readDeviceInfo();
    int readDeviceState();
    int readDeviceName();
    //Interfaces
            
    //Diagnostics
    int SOPAsError();
private:
    TCPSocket &_socket;
    static const int replyBufferSize = 8192; //must be large enough for maximum possible data
    static const int cmdBufferSize = 64;
    char replyBuffer[replyBufferSize] = {}; //stack size must be large enough for allocated reply buffer
    char cmdBuffer[cmdBufferSize] = {};
    char headerInfo[64] = {};
    char deviceName[17] = {};
    int refTime_s = 0;
    int deviceStartTime_s = 0;
    
    //Initialisation
    int setRefTime(int lastMeasTime_s);
    //Communication
    int recieveSOPASreply(char (&reply)[replyBufferSize], bool printReply);
    int sendSOPAsCmd(char (&cmd)[cmdBufferSize]);
};

#endif