/*
Sick TiM7xx series laser scanner control library
*/

#include "Sick_TiM7xx.h"

/*TiM7xx::TiM7xx(TCPSocket &socket)
        :_socket{socket}
{
    //TiM7xx::init();
}
/*int TiM7xx::extCmd(const char &buf)
{
    
    sendSOPAsCmd(buf);
    return(1);
}*/

//define static variables
//char TiM7xx::replyBuffer = '\0';
//char TiM7xx::cmdBuffer = '\0';
//Sick SOPAS control functions - in order given in Telegram listing document
//Communication

int TiM7xx::init() // for fast scans
{
    int rNum = 0;
    if (!(rNum = readDeviceInfo() > 0))
    {
        printf("Error reading connecting to device: %d\n", rNum);
    }
    else
    {
        rNum = readDeviceState();
        rNum = readDeviceName();
        rNum = setAccessMode(3); //set access to authorised client to allow parameter setting
        rNum = scanDataCfg(); 
        rNum = outputRangeCfg(); 
        //rNum = saveParam();
        rNum = run();
        rNum = readOutputRange();
        rNum = readDeviceState();
        if (!(rNum > 0))
        {
            printf("Error reading initialising device: %d\n", rNum);
        } else
        {
            //set start reference time
            int timestamp = readDeviceTime();
            setRefTime(timestamp);
        }
    }
    return(rNum);
}


int TiM7xx::init1() // This is used for Slow scans
{
    int rNum = 0;
    if (!(rNum = readDeviceInfo() > 0))
    {
        printf("Error reading connecting to device: %d\n", rNum);
    }
    else
    {
        rNum = readDeviceState();
        rNum = readDeviceName();
        rNum = setAccessMode(3); //set access to authorised client to allow parameter setting
        rNum = scanDataCfg1();   // configuration here is different to fast scans
        rNum = outputRangeCfg(); 
        //rNum = saveParam();
        rNum = run();
        rNum = readOutputRange();
        rNum = readDeviceState();
        if (!(rNum > 0))
        {
            printf("Error reading initialising device: %d\n", rNum);
        } else
        {
            //set start reference time
            int timestamp = readDeviceTime();
            setRefTime(timestamp);
        }
    }
    return(rNum);
}


int TiM7xx::setAccessMode(int mode)
{
    int rNum;
    char cmd[32] = {};
    char password[] = {"00000000"};
    switch(mode)
    {
    case 2 : strcpy(password, "B21ACE26");
        break;
    case 3 : strcpy(password, "F4724744");
        break;
    case 4 : strcpy(password, "81BE23AA");
        break;
    default  : strcpy(password, "00000000");
        break;
    }
    std::sprintf(cmd,"\x02sMN SetAccessMode 0%d %s\x03", mode, password);
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::pollTCPsocket(bool state)
{
    int rNum = recieveSOPASreply(replyBuffer, state);
    //printf("Polled reply %d [%s]\n", rNum, replyBuffer);    
    return(rNum);
}
//Scan Data
int TiM7xx::getData(int &timeStamp_ms, char (&distanceBuffer)[dataBufferSize], char (&rssiBuffer)[dataBufferSize])
{
    distanceBuffer[0] = {};
    rssiBuffer[0] = {};
    
    int timeStamp_s = 0;
    char info[256] = {};
    char* pdist;
    char* pdistn;
    char* prssi;
    char* prssin;
    char* pend;
    pdist = strstr(replyBuffer, "DIST1");
    prssi = strstr(replyBuffer, "RSSI1");
    pdistn = pdist;
    prssin = prssi;
    pend = strstr(replyBuffer, deviceName)-8;
    if (!pdist)
    {
        //printf("Not a valid data message! [%s]\n", replyBuffer);
        printf("Not a valid data message!\n");
        return(0);
    }
    strncpy(info, replyBuffer, pdist-replyBuffer);
    
    for (int i = 0; i<=5; i++)
    {
        pdistn = pdistn+strcspn(pdistn, " ")+1;
        prssin = prssin+strcspn(prssin, " ")+1;
    }
    
    int dloc = prssi-pdistn-1; //find length of data array
    int rloc = pend-prssin-1;
    
    strncat(info, pdist, pdistn - pdist);
    strncat(info, prssi, prssin - prssi);
    strcat(info, pend);
    strncpy(distanceBuffer, pdistn, dloc);
    strncpy(rssiBuffer, prssin, pend-prssin-1);
    
    distanceBuffer[dloc] = {}; //strncpy doesn't add end of line - manually terminate
    rssiBuffer[rloc] = {}; 
       
    timeStamp_s = getTime(timeStamp_ms, info);
    //printf("DataTime = %d %d\r\n",timeStamp_s, timeStamp_ms);
    return(timeStamp_s); 
}  
int TiM7xx::getTime(int &timeStamp_ms, char *info)
{
    struct tm measTime = {0};
    int timeStamp_s, us;
    char* pend;
    char* pendn;
    pend = strstr(info, "7B2");
    if(pend == NULL) {
        timeStamp_s  = 0;
        timeStamp_ms = 0;
        printf("No valid time info %s\r\n", info);
        return(0);
    }
    pendn = pend;
    measTime.tm_year = strtol(pend, NULL, 16) - 1900;
        
    for (int i = 0; i<=5; i++)
    {
        pendn = pendn+strcspn(pendn, " ")+1;
        switch (i)
        {
            case 0: measTime.tm_mon = strtol(pendn, NULL, 16) - 1; break;
            case 1: measTime.tm_mday = strtol(pendn, NULL, 16); break;
            case 2: measTime.tm_hour = strtol(pendn, NULL, 16); break;
            case 3: measTime.tm_min = strtol(pendn, NULL, 16); break;
            case 4: measTime.tm_sec = strtol(pendn, NULL, 16); break;
            case 5: us = strtol(pendn, NULL, 16); break;
            default: ;
        }
    }
    
    timeStamp_s = mktime(&measTime);
    timeStamp_ms = us/1000;
    
    //printf("Info: %s\r\n", info);
    //printf("TimeStamp: %d:%d\r\n", timeStamp_s, timeStamp_ms);
    //printf("Device time: %s\n", asctime(&measTime));
    //printf("Device time: %d\n", mktime(&measTime));
    //printf("Device time: %d %d\n", timeStamp_s, timeStamp_ms);
    return(timeStamp_s);
}
//File I/O
int TiM7xx::buildFile(int timeStamp_s, int timeStamp_ms, char (&fileBuffer)[fileBufferSize], char (&distanceBuffer)[dataBufferSize],char (&rssiBuffer)[dataBufferSize])
{
    //strcpy(dist, "1AE 1A3 1BE 1A6");
    //strcpy(rssi, "308D 318C 3037 3184");
    //printf("Building data file array...\n");
    fileBuffer[0] = {};
    //get real timestamp from device time
    int timeStamp = deviceStartTime_s + timeStamp_s; //calc measurement UNIX time stamp*/
    //printf("timestamp: %d = %d+%d\n", timeStamp, deviceStartTime_s, timeStamp_s);
    sprintf(fileBuffer, "%010d %03d ", timeStamp, timeStamp_ms);
    strcat(fileBuffer, distanceBuffer);
    strcat(fileBuffer, " ");
    strcat(fileBuffer, rssiBuffer);
    
    //printf("File: [%s]\n", fileBuffer);
    return(timeStamp);
}
int TiM7xx::headerWrite(FILE* f)
{
    fprintf(f, "%s\r\n", headerInfo);
    printf("File Header: [%s]\n", headerInfo);
    return(1);
}
int TiM7xx::dataWrite(FILE* f)
{
    return(1);
}
//Basic settings
int TiM7xx::startMeasurement()
{
    int rNum;
    const char cmd[] = {"\x02sMN LMCstartmeas\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::stopMeasurement()
{
    int rNum;
    const char cmd[] = {"\x02sMN LMCstopmeas\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::rebootDevice()
{
    int rNum;
    const char cmd[] = {"\x02sMN mSCreboot\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::saveParam()
{
    int rNum;
    const char cmd[] = {"\x02sMN mEEwriteall\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::run()
{
    int rNum;
    const char cmd[] = {"\x02sMN Run\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
//Measurement output for fast scan
int TiM7xx::scanDataCfg()
{
    int rNum;
    //[CH] [RSSI] [Resolution] [RSSI units] [Encoder] [Position] [Name] [Comment] [Time] [Rate]
    //Fixed: Ch1 16Bit RSSI Name Time
    const char cmd[] = {"\x02sWN LMDscandatacfg 01 00 1 1 0 00 00 0 1 0 1 +2\x03"}; 
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}

//Measurement output for slow scan 2 Hz
int TiM7xx::scanDataCfg1()
{
    int rNum;
    //[CH] [RSSI] [Resolution] [RSSI units] [Encoder] [Position] [Name] [Comment] [Time] [Rate]
    //Fixed: Ch1 16Bit RSSI Name Time
    const char cmd[] = {"\x02sWN LMDscandatacfg 01 00 1 1 0 00 00 0 1 0 1 +7\x03"}; 
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}

int TiM7xx::outputRangeCfg()
{
    int rNum;
    //[Angular Res] [Start Angle] [Stop Angle]
    //Fixed: Ch1 16Bit RSSI Name Time
    //const char cmd[] = {"\x02sWN LMPoutputRange 1 D05 FFF92230 225510\x03"}; // max range (-45 : 225 [270deg])
    //const char cmd[] = {"\x02sWN LMPoutputRange 1 D05 0 186A0\x03"};
    const char cmd[] = {"\x02sWN LMPoutputRange 1 D05 FFFFB1E0 169540\x03"}; // live range (-2 : 148 [150 deg])
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::readOutputRange()
{
    int rNum;
    const char cmd[] = {"\x02sRN LMPoutputRange\x03"}; 
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    strcpy(headerInfo, replyBuffer);
    return(rNum);
}
int TiM7xx::pollOneTelegram()  // Telegram used for 1Hz scan, polls for the active field set on scanner
{
    const char cmd[] = {"\x02sRN LMDscandata\x03"};
    //const char cmd[] = {"\x02sEN LMDscandata 1\x03"};
    strcpy(cmdBuffer, cmd);
    int rNum = sendSOPAsCmd(cmdBuffer);
    return(rNum);
}
int TiM7xx::pollContTelegram(bool state)  // Temegram used for fast scan, output rate set in scanDataCfg()
{
    if (state)
    {
        const char cmd[] = {"\x02sEN LMDscandata 1\x03"}; //start data streaming
        strcpy(cmdBuffer, cmd);
    } else
    {
        const char cmd[] = {"\x02sEN LMDscandata 0\x03"}; //stop data streaming
        strcpy(cmdBuffer, cmd);
    }
    int rNum = sendSOPAsCmd(cmdBuffer);
    return(rNum);
}
//Time stamp
int TiM7xx::readDeviceTime()
{
    char* pinfo;
    int mSec = 0;
    //need to poll a scan to get the latest time stamp :(
    pollOneTelegram();
    while(pollTCPsocket(0)<=0);
    pinfo = strstr(replyBuffer, deviceName);
    printf("Info: %s\r\n", pinfo);
    int timestamp = getTime(mSec, pinfo);
    return(timestamp);   
}
//Filter

//Encoder

//Outputs

//Inputs

//Status
int TiM7xx::readDeviceInfo()
{
    int rNum;
    const char cmd[] = {"\x02sRN DeviceIdent\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
}
int TiM7xx::readDeviceState()
{
    int rNum;
    const char cmd[] = {"\x02sRN SCdevicestate\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    return(rNum);
    
}
int TiM7xx::readDeviceName()
{
    int rNum;
    const char cmd[] = {"\x02sRN LocationName\x03"};
    strcpy(cmdBuffer, cmd);
    rNum = sendSOPAsCmd(cmdBuffer);
    if (rNum >= 0) {
        ThisThread::sleep_for(100ms);
        rNum = recieveSOPASreply(replyBuffer, 1);
    }
    
    if (rNum > 0)
    {
        char *pch;
        pch = strstr(replyBuffer, "LocationName");
        pch = strchr(pch, ' ')+1;
        int span = strtol(pch, NULL, 16);
        pch = strchr(pch, ' ')+1;
        strncpy(deviceName, pch, span);
        deviceName[span] = '\0';
        printf("Device Name: %s [%d]\n", deviceName, strlen(deviceName));
    }
    
    return(rNum);    
}

//Diagnostics

//PRIVATE
//Initialisation
int TiM7xx::setRefTime(int lastMeasTime_s)
{
    time_t rtcTime = time(NULL); //get RTC time
    struct tm refTm = *localtime(&rtcTime); //build reftime struct
    printf("Current Time: %s\n",asctime(&refTm));
    //set refTm to midnight
    //refTm.tm_hour = 0;
    //refTm.tm_min = 0;
    //refTm.tm_sec = 0;
    //printf("Time at midnight: %s\n",asctime(&refTm));
    //deviceStartTime_s  = difftime(rtcTime, mktime(&refTm)); //calculate seconds since midnight
    //refTime_s = difftime(mktime(&refTm), 0); //seconds to refTm since epoch
    refTime_s = mktime(&refTm); //real device initialisation time
    printf("refTime_s: %d\n", refTime_s);
    //printf("deviceStartTime_s: %d\n", deviceStartTime_s);
    //calculate time that device turned on
    //printf("lastMeasTime_s: %d\n", lastMeasTime_s);
    deviceStartTime_s = refTime_s - lastMeasTime_s;
    printf("deviceStartTime_s: %d\n", deviceStartTime_s);
    return(deviceStartTime_s);
}
//send telegram commands to scanner
int TiM7xx::sendSOPAsCmd(char (&cmd)[cmdBufferSize]) 
{
    replyBuffer[0] = '\0';
    int scount = _socket.send(cmd, strlen(cmd));
    //send command
    if (scount > 0) //check for valid TCP socket
    {
        printf("sent %d [%s]\n", scount, cmd);
    } 
    else
    {
        printf("TCP socket send error: %d\n", scount);
    }
    return(scount);     
}

int TiM7xx::recieveSOPASreply(char (&reply)[replyBufferSize], bool printReply)
{
    //check for replies
    char rbuffer[1024] = {};
    static char tempBuffer[1024] = {};
    strcpy(reply, tempBuffer);
    //printf("first reply [%s]\n", reply);
    tempBuffer[0] = {};
    
    int rcount = 0;
    int i = 0;
    while(1) //blocking until socket.timeout
    {
        rcount = _socket.recv(rbuffer, sizeof(rbuffer));
        rbuffer[rcount] = '\0';        
        //handle empty calls
        if (rcount < 0)
        {      
            //printf("No data on TCP socket!\n");
            break;
        }
        //printf("\nrecv %d [%s]\n", rcount, rbuffer);
        if (strlen(reply)+rcount > replyBufferSize)
        {
            printf("Reply buffer size exceeded: %d of %d\n", strlen(reply)+rcount, replyBufferSize);
            //printf("Full reply %d [%s]\n", strlen(reply), reply);
            //printf("recv %d [%s]\n", rcount, rbuffer);
            strcat(reply, "\x03");
            rcount = strlen(reply);
            break;   
        }
        
        char* pstart;
        char* pend;
        pend = strchr(rbuffer, '\x03'); // look for end of message
        if (pend)
        {
            //printf("rbuffer [%s]\n",rbuffer);
            pstart = strchr(pend,'\x02'); //look for start of next measurement
            if (pstart)
            {
                strcpy(tempBuffer, pstart); //copy new measurement to temporary buffer
                //printf("Temp Buffer [%s]\n", tempBuffer);
            }
            rbuffer[pend-rbuffer+1] = {}; //assign end of first message to \0
        }
            
        strcat(reply, rbuffer);
        //printf("Reply buffer bytes remaining: %d\n", replyBufferSize-strlen(reply));
        if (strchr(rbuffer, '\x03')) //poll for end of message
        {
            //printf("End of Message\n");
            rcount = strlen(reply);
            break;
        }
    } 
    
    if (printReply && rcount > 0)
    {
        printf("Full reply %d [%s]\n", strlen(reply), reply);
    }
    
    if(strstr(reply, "sFA"))
    {
        char errorCode = '0';
        errorCode = reply[5];
        char SOPASERROR[20] = {};
        switch(errorCode)
        {
        case '0': strcpy(SOPASERROR, "Sopas_OK");
            break;
        case '1': strcpy(SOPASERROR, "Sopas_Error_METHODIN_ACCESSDENIED");
            break;
        case '2': strcpy(SOPASERROR, "Sopas_Error_METHODIN_UNKNOWNINDEX");
            break;
        case '3': strcpy(SOPASERROR, "Sopas_Error_VARIABLE_UNKNOWNINDEX");
            break;
        case '4': strcpy(SOPASERROR, "Sopas_Error_LOCALCONDITIONFAILED");
            break;
        case '5': strcpy(SOPASERROR, "Sopas_Error_INVALID_DATA");
            break;
        case '6': strcpy(SOPASERROR, "Sopas_Error_UNKNOWN_ERROR");
            break;
        case '7': strcpy(SOPASERROR, "Sopas_Error_BUFFER_OVERFLOW");
            break;
        case '8': strcpy(SOPASERROR, "Sopas_Error_BUFFER_UNDERFLOW");
            break;
        case '9': strcpy(SOPASERROR, "Sopas_Error_ERROR_UNKNOWN_TYPE");
            break;
        case 'A': strcpy(SOPASERROR, "Sopas_Error_VARIABLE_WRITE_ACCESSDENIED");
            break;
        case 'B': strcpy(SOPASERROR, "Sopas_Error_UNKNOWN_CMD_FOR_NAMESERVER");
            break;
        case 'C': strcpy(SOPASERROR, "Sopas_Error_UNKNOWN_COLA_COMMAND");
            break;
        case 'D': strcpy(SOPASERROR, "Sopas_Error_METHODIN_SERVER_BUSY");
            break;
        case 'E': strcpy(SOPASERROR, "Sopas_Error_FLEX_OUT_OF_BOUNDS");
            break;
        case 'F': strcpy(SOPASERROR, "Sopas_Error_EVENTREG_UNKNOWNINDEX");
            break;
        default: strcpy(SOPASERROR, "Unknown_Error");
            break;
        }
        printf("SOPASERROR: %s [%c]\n", SOPASERROR, errorCode);        
    }
    
    return(rcount);
}