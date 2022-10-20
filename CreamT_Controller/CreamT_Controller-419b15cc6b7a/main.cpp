/*
* Copyright (c) 2006-2020 Arm Limited and affiliates.
* SPDX-License-Identifier: Apache-2.0
*/
#include "mbed.h"
#include "mbed_stats.h"
#include "EthernetInterface.h"
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
#include "NTPClient.h"
#include "HTTPServer.h"
#include "Sick_TiM7xx.h"
#include <stdio.h>
#include <errno.h>

//Size of data buffers
#define DATA_BUFFER_SIZE        8192
#define FILE_BUFFER_SIZE        17920 //35 x 512B blocks change to ring buffer
#define HTTP_BUFFER_SIZE        2048
//SD setup
SDBlockDevice blockDevice(PC_12, PC_11, PC_10, PD_2, 15000000); // mosi, miso, sck, cs, clock speed [Hz]
FATFileSystem fileSystem("fs"); //only use SD cards formatted to fat32
//DIO pins
DigitalOut led1(PB_0); //onboard LEDs
DigitalOut led2(PB_7);
DigitalOut led3(PB_14);
DigitalOut scannerPower(PF_13); //turn on power to scanner [D7] - Penzance & Dawlish (Dawlish Rewired from PD14)
DigitalOut rutOut(PE_2);        //signal to RUT DI
//DigitalIn  tiltSwitch(PF_15);   //                          [D3]
//AIO pins
AnalogIn   battVoltage(PB_1); //monitor main battery voltage

//Network interface
EthernetInterface net;
NTPClient         ntp(net);
TCPSocket         socket; //scanner socket
TCPSocket         server; //webserver socket
TCPSocket*        client; //pointer to bind incoming connections
HTTPServer        webserver;

//scanner instance
TiM7xx scanner(socket);

//Interrupts
InterruptIn userbutton(PC_13); //blue button on nucleo board
Ticker polltcp;
Ticker scanReq;
Ticker tideCheck; //interrupt to start scheduled scan
Timer t;
Timer debounce;
Timeout tSocket; //timeout to monitor downtime of server during scans (TCPSocket must wait 120 seconds between close and re-open)

//IP addresses
char deviceIP[]  = {"192.168.0.22"}; //static IP of controller
char scannerIP[] = {"192.168.0.1"}; //IP address of Sick scanner
//char scannerIP[] = {"192.168.1.88"}; //IP address of Sick scanner
char gatewayIP[] = {"192.168.0.23"}; //subnet gateway - RUT LAN IP
//char gatewayIP[] = {"192.168.0.254"}; //subnet gateway - RUT LAN IP
char netMask[]   = {"255.255.255.0"};

//global variables
volatile bool tcpFlag    = 0;
volatile bool sampleFlag = 0;
volatile bool serverFlag = 0;
char filenameLog[20]     = {"device.log"};
char filenameData[32]    = {"data.dat"};
//[1=vBat][2=nextTide][3=slowScanTime][4=fastScanTime][5=fastScanNum]
char* updateVals[10]     = {}; //container for AJAX request values
//allocate heap memory for data buffers
char distanceBuffer[DATA_BUFFER_SIZE] = {};
char rssiBuffer[DATA_BUFFER_SIZE]     = {};
char fileBuffer[FILE_BUFFER_SIZE]     = {};
char httpBuffer[HTTP_BUFFER_SIZE]     = {};

//Function definitions
//setup & helper functions
bool   updateRTC(NTPClient* ntp);
int    device_log(char* filename, char* logStr);
int    load_config(char* filename, char** updateVals); //load config values file saved in logfile
FILE*  open_file(TiM7xx scanner, char* filename, int fileType); //prepare file for writing fileType[0=log; 1=data]
//TCP socket functions
int server_connect(EthernetInterface &net, TCPSocket &server);
//memory usage for debugging hard faults
void   print_memory_info();
//interrupt & state handler functions
void   button_press();
void   reset_device();
void   poll_TCP();
void   scan_request();
void   tide_check();
void   server_ready();
//CreamT specific functions
int    scanner_connect(EthernetInterface &net, TCPSocket &socket, char* scannerIP, SocketAddress &a);
time_t get_next_tide(char* filename); //load next valid tide time from file

int main()
{
    printf("\nSick TiM7xx Scanner Controller\n...running on Mbed OS %d.%d.%d.\n", MBED_MAJOR_VERSION, MBED_MINOR_VERSION, MBED_PATCH_VERSION);
    //Manually set RTC time for testing - https://www.unixtimestamp.com/index.php
    //set_time(1621342020); //get from NTP server in field
    //print_memory_info();
    debounce.start();
    //variables
    FILE* df;
    float cBat = 18.3; //battery voltage divider coefficient (Changed From 18.5 to 18.3 MS 20220706)
    float vLow = 11.5; //low battery voltage level
    int i = 0;
    int rcount = 0;
    int scount = 0;
    int scanNum = 0;              //scan counter
    char logStr[64] = {};
    char password[] = {"CreamT"}; //webserver URL: IP/PASSWORD/
    //float vBat = battVoltage.read()*cBat; //R1 = 1M R2 = 220k
    float batcal = 0.28;
    float vBat1 = battVoltage.read()*cBat; //R1 = 1M R2 = 220k
    float vBat = vBat1 - batcal; // added minus of 0.28 for 3rd system as battery votlage is off by this amount
    time_t nextTide;
    time_t lastTide;
    time_t now = time(NULL);
    time_t lastHour; //time to schedule hourly system checks
    //setup filesystem
    printf("Mounting filesystem... ");
    fflush(stdout);
    int err = fileSystem.mount(&blockDevice);
    printf("%s\n", (err ? "Fail :(" : "OK"));
    if (err) {
        //error thrown if SD card is not available
        printf("No compatible filesystem found...SD logging not available\n");
    }
    //Setup the network interface - if this fails then nothing can be done must keep resetting
    //until the RUT is reachable - maybe reset RUT power??
    printf("Set up network interface\n");
    net.set_network(deviceIP,netMask,gatewayIP);  //Static IP
    nsapi_error_t status = net.connect();
    if(status < 0) {
        printf("Failed to connect to Ethernet socket - ERROR: %d\n", status);
        printf("\nDone!");
        ThisThread::sleep_for(500ms);
        //handle disconnects with timed reset
        return(0);
    } else {
        printf("Ethernet connected: %d\n", status);
    }
    //Get network address
    SocketAddress a;
    net.get_ip_address(&a);
    printf("IP address: %s\n", a.get_ip_address() ? a.get_ip_address() : "None");
    //state flags
    bool buttonPress  = 0;
    bool firstCall    = 1;
    bool scannerState = 0;  //[0=not ready; 1=ready]
    int  logData      = 0;  //[0=scanner off; 1=low speed scan; 2=high speed scan]
    bool clientDone   = 1;
    int scanTime      = 0;
    int scanStart     = 0;
    int burstInd      = 0;
    bool burstState   = 0;

    //attach interrupts
    userbutton.rise(&button_press);
    tideCheck.attach(&tide_check, 60s);
    //start TCP server
    //server.open(&net);
    //server.bind(80); //bind http port to server
    //server.set_blocking(false);
    //server.set_timeout(100);
    //server.listen();
    server_connect(net, server);
    webserver.setPassword(password, strlen(password));
    //assign default update values
//[0=vBat][1=nextTide][2=slowScanTime][3=fastScanTime][4=fastScanNum][5=filename][6=scannerState]
    char* pend;
    char vBatStr[6] = "--.-";
    updateVals[0] = vBatStr;
    char nTideStr[32] = "---------- --:--:--";
    updateVals[1] = nTideStr;
    int sScanTime = 40;
    char sScanStr[6] = {};
    sprintf(sScanStr, "%u", sScanTime);
    updateVals[2] = sScanStr;
    int fScanTime = 600;
    char fScanStr[6] = {};
    sprintf(fScanStr, "%u", fScanTime);
    updateVals[3] = fScanStr;
    int fScanNum = 1;
    char fNumStr[6] = {};
    sprintf(fNumStr, "%u", fScanNum);
    updateVals[4] = fNumStr;
    updateVals[5] = filenameData;
    char scanState[64] = "Waiting for command!";
    updateVals[6] = scanState;

    load_config(filenameLog, updateVals);
    updateRTC(&ntp);
/*----------------------------------------------------------------------------*/
/*--------------------------------RUNNING-------------------------------------*/
/*----------------------------------------------------------------------------*/
    while(1) {
        if (vBat <= vLow) {
            if (rutOut) {
                strcpy(logStr, "Warning::Low battery");
                device_log(filenameLog,logStr); //Update device log
            }
            rutOut = 0;
        } else {
            rutOut = 1;
        }
        if ((firstCall || led3) && !logData) {
            now = time(NULL);
            vBat = battVoltage.read()*cBat - batcal;
            printf("Battery Voltage: %.2f [%.2f]\n", vBat, battVoltage.read()*3.3);

            lastTide = nextTide;
            if (firstCall) {
                nextTide = get_next_tide("Tides.txt");
                strcpy(logStr, "Action::Device restarted");
                device_log(filenameLog,logStr); //Update device log
                sScanTime = strtol(updateVals[2], NULL, 10);
                fScanTime = strtol(updateVals[3], NULL, 10);
                fScanNum = strtol(updateVals[4], NULL, 10);
            }

            if(difftime(now, nextTide) >= 0) {
                printf("Auto scan! [%.f]\n", difftime(now, nextTide));
                logData = 1;
            }
            led3 = 0;
            nextTide = get_next_tide("Tides.txt");
            printf("Next Tide: %s", ctime(&nextTide));
            if (difftime(nextTide,lastTide) != 0) {
                strftime(logStr,sizeof(logStr),"Log::Next Tide = %Y-%m-%d %H:%M:%S",localtime(&nextTide));
                device_log(filenameLog,logStr); //Update device log
                if (nextTide == 2147483647) {
                    rutOut = 0;
                    ThisThread::sleep_for(2s);
                }
            }
            if (difftime(now, lastHour) >= 14400) {
                //Update RTC
                updateRTC(&ntp);
                sprintf(logStr, "Log::Battery Voltage = %0.2f", vBat);
                device_log(filenameLog,logStr); //Update device log
                lastHour = now;
            }
            firstCall = 0;
            //print_memory_info();
        }
        //handle webserver requests - block requests during logging
        if(!scannerState) {
            client = server.accept();
            if (client) {
                clientDone = 0;
                now = time(NULL);
                sprintf(vBatStr, "%.2f", vBat);
                if (nextTide == 2147483647) {
                    strcpy(nTideStr, "No Valid Tide!");
                } else {
                    strftime(nTideStr,20,"%Y-%m-%d %H:%M:%S",localtime(&nextTide));
                }
                client->set_timeout(1000);
                client->getpeername(&a);
                int cmd = webserver.TCPreqHandler(client, httpBuffer, updateVals);

                printf("Connection succeeded!\n\rIP: %s\n\r", a.get_ip_address());
                printf("Time: %s", ctime(&now));
                status = client->close();
                printf("Connection Closed: %d\n", status);

                switch (cmd) {
                    case 0:
                        printf("Slow Scan!\n\n");
                        strcpy(scanState, "Scanning [-]");
                        logData = 1;
                        break;
                    case 1:
                        printf("Fast Scan!\n\n");
                        logData = 2;
                        burstInd = 0;
                        sprintf(scanState, "Scanning [%d]", burstInd);
                        break;
                    case 3:
                        sScanTime = strtol(updateVals[2], NULL, 10);
                        sprintf(logStr, "Config::sTime=%d", sScanTime);
                        device_log(filenameLog,logStr); //Update device log
                        break;
                    case 4:
                        fScanTime = strtol(updateVals[3], NULL, 10);
                        sprintf(logStr, "Config::fTime=%d", fScanTime);
                        device_log(filenameLog,logStr); //Update device log
                        break;
                    case 5:
                        fScanNum = strtol(updateVals[4], NULL, 10);
                        sprintf(logStr, "Config::fNum=%d", fScanNum);
                        device_log(filenameLog,logStr); //Update device log
                        break;
                    case 6:
                        printf("Soft Reset!\n\n");
                        NVIC_SystemReset();
                        break;
                    default: ;
                }
            } else
                clientDone = 1;
        }
        if (led2) { //handle userbutton press to start scanner
            if (!logData) {
                printf("Start scanning!\n\n");
                logData = 1;
            }else {
                printf("Stop scanning!\n\n");
                logData = 0;
            }
            led2 = 0;
        }
        //scanner startup
        if (logData && clientDone) {
            if (!scannerPower || burstState) {
                server.close();
                tSocket.attach(&server_ready, 120s);
                printf("Powering on Scanner!\n");
                strcpy(logStr, "Action::scanner powered on");
                device_log(filenameLog,logStr); //Update device log
                scannerPower = 1; //power on scanner
                burstState = 0;
                if (scount = scanner_connect(net, socket, scannerIP, a) < 0) {
                    logData = 0;
                    scannerPower = 0;
                    rutOut = 0;
                    sprintf(logStr, "Error::Cannot connect to scanner! [%d]", scount);
                    device_log(filenameLog,logStr); //Update device log
                    ThisThread::sleep_for(2s);
                    strcpy(scanState, "Error: Cannot connect to scanner!");
                    printf("Scanner socket connected! [%d]\n", scount);
                } else {
                    printf("Scanner socket connected!\n");
                }
            }
            if (!scannerState && logData) {
                printf("Check Scanner State\n");
                if (scanner.readDeviceName() > 0) { //check scanner ready state
                    scannerState = 1;

                    if (logData == 2) {
                    rcount = scanner.init();}
                    else{}
                    rcount = scanner.initone();

                    if (logData == 2) {
                        scanner.pollContTelegram(1); //1 for high speed continuous output
                    } else {
                        scanner.pollContTelegram(0); //0 for output to wait for polled requests
                    }
                    now = time(NULL);
                    printf("Scan start time: %s\n", ctime(&now));
                    if (!sScanTime)
                        sScanTime = 40;
                    if (logData == 2) {
                        strftime (filenameData,sizeof(filenameData),"%y%m%d",localtime(&now));
                        strcpy(logStr, "Action::rapid scan start");
                        strcat(filenameData, "_HS_000");
                        printf("Scan length: %d s\n", fScanTime);
                    } else {
                        strftime (filenameData,sizeof(filenameData),"%y%m",localtime(&now));
                        strcpy(logStr, "Action::scheduled scan start");
                        strcat(filenameData, "_data");
                        printf("Scan length: %d s\n", sScanTime);
                        burstInd = 100;
                    }
                    device_log(filenameLog,logStr); //Update device log
                    df = open_file(scanner, filenameData, 1);  //open data file
                    if(!df) {
                        logData = 0;
                    } else {
                        while(scanner.pollTCPsocket(0) > 0);
                        if (logData == 2) {
                            polltcp.attach(&poll_TCP, 50ms);
                        } else {
                            polltcp.attach(&poll_TCP, 500ms);
                        }
                    }
                }
            }
            //poll scanner socket
            if (tcpFlag) {
                rcount = 0;
                if (logData == 1){
                     scanner.pollContTelegram(1); //1 for high speed continuous output
                }
                rcount = scanner.pollTCPsocket(0); //check for incoming TCP msg (1 to print output to serial)

                if (rcount > 0) {
                    int tscan_ms = 0;
                    if(int tscan_s = scanner.getData(tscan_ms, distanceBuffer, rssiBuffer)) {
                        scanTime = scanner.buildFile(tscan_s, tscan_ms, fileBuffer,distanceBuffer,rssiBuffer);
                        if (scanStart == 0) {
                            scanStart = scanTime;
                        }
                        if (df) {
                            fflush(stdout);
                            fprintf(df, "%s\r\n", fileBuffer);
                        }
                        printf("ScanNum: %d [%d] ScanTime: %d s [%d ms]\n", scanNum, rcount, scanTime, tscan_ms);
                        //printf("Scantime: %d s %d ms\r\n", scanTime, tscan_ms);
                        scanNum++;
                        i = 0;
                    }
                    tcpFlag = 0;
                } else if (rcount < 0) {
                    if (i >= 2000) {
                        logData = 0;
                        rutOut = 0;
                        sprintf(logStr, "Error::Scanner disconnected! [%d]", rcount);
                        device_log(filenameLog,logStr); //Update device log
                        ThisThread::sleep_for(2s);
                        strcpy(scanState, "Error: Scanner disconnected!");
                        printf("Error: Scanner disconnected! [%d]\n", rcount);
                        i = 0;
                    }
                    i++;
                }
                if (logData == 2) {
                     if(scanTime - scanStart >= fScanTime)
                        logData = 0;
                } else {
                    if(scanNum >= sScanTime * 2)
                        logData = 0;
                }
            }
        } else if (scannerState) {
            //scan timer
            polltcp.detach();
            scanner.pollContTelegram(0); //stop scanner data streaming
            while(scanner.pollTCPsocket(0) > 0); //flush TCP socket*/
            i            = 0;
            tcpFlag      = 0;
            scannerState = 0;
            scanNum      = 0;
            scanStart    = 0;
            scanTime     = 0;
            fclose(df);
            printf("Socket Closed!\n");
            socket.close();
            nextTide = get_next_tide("Tides.txt");
            printf("Next Tide: %s\n", ctime(&nextTide));
            strcpy(logStr, "Action::scan done");
            device_log(filenameLog,logStr); //Update device log
            if (++burstInd < fScanNum) {//start next fast burst
                logData = 2;
                burstState = 1;
                printf("Burst Num: [%d]\n", burstInd);
                sprintf(scanState, "Scanning [%d]", burstInd);
            } else {
                scannerPower = 0;
                burstState = 0;
                burstInd = 100;
                while(!serverFlag);
                server_connect(net, server);
                serverFlag = 0;
                strcpy(scanState, "Waiting for command!");
            }
        }
    }
    printf("Done\n");
    // Bring down the ethernet interface
    net.disconnect();
    net.disconnect();
    return(1);
}

/*----------------------------------------------------------------------------*/
/*---------------------------FUNCTION IMPLEMENTATIONS-------------------------*/
/*----------------------------------------------------------------------------*/
bool updateRTC(NTPClient* ntp)
{
    time_t now;
    printf("Get NTP time...");
    if(!ntp->setTime("pool.ntp.org",123,5000)) {
        printf("OK!\n");
        now = time(NULL);
        printf("RTC time: %s\n\n", ctime(&now));
        return(1);
    } else {
        printf("FAIL!\n");
        printf("Cannot connect to NTP server!!\n");
        now = time(NULL);
        printf("RTC time: %s\n", ctime(&now));
        return(0);
    }
}
int device_log(char* filename, char* logStr)
{
    char filepath[34] = "/fs/";
    time_t now = time(NULL);
    //printf("[Device Log] RTC time: %s\n\n", ctime(&now));
    fflush(stdout);
    FILE* lf;
    strcat(filepath, filename);
    if (!strstr(filename, ".log"))
        strcat(filepath, ".log");

    if (!(lf = fopen(filepath, "a"))) {
        lf = fopen(filepath, "w");
        printf("%s\r\n", (!lf ? "Fail :(" : "OK"));
        if (!lf) {
            printf("error: %s (%d)\r\n", strerror(errno), -errno);
            return(0);
        }
    }
    char timeStr[20];
    strftime(timeStr,20,"%Y-%m-%d %H:%M:%S",localtime(&now));
    fprintf(lf,"[%s]::%s\r\n", timeStr, logStr);
    fclose(lf);

    return(1);
}
int load_config(char* filename, char** updateVals)
{
    char strTemp[64];
    char filepath[34] = "/fs/";
    fflush(stdout);
    FILE* lf;
    strcat(filepath, filename);
    if (!strstr(filename, ".log"))
        strcat(filepath, ".log");
    if (!(lf = fopen(filepath, "r"))) {
        return(0);
    }
    while(!feof(lf)) {
        char* pind;
        fgets(strTemp, 64, lf);
        if(pind = strstr(strTemp, "::Config::sTime=")) {
            for (int i=0; i<strlen(pind+16)-2; i++) {
                updateVals[2][i] = pind[16+i];
                updateVals[2][i+1] = '\0';
            }
        } else if(pind = strstr(strTemp, "::Config::fTime=")) {
            for (int i=0; i<strlen(pind+16)-2; i++) {
                updateVals[3][i] = pind[16+i];
                updateVals[3][i+1] = '\0';
            }
        } else if(pind = strstr(strTemp, "::Config::fNum=")) {
            for (int i=0; i<strlen(pind+15)-2; i++) {
                updateVals[4][i] = pind[15+i];
                updateVals[4][i+1] = '\0';
            }
        }
    }
    fclose(lf);
    return(1);
}
FILE*  open_file(TiM7xx scanner, char* filename, int fileType)
{
    fflush(stdout);
    char filepath[34] = "/fs/";
    FILE* df;
    strcat(filepath, filename);
    strcat(filepath, ".dat");
    if (strstr(filename, "_HS")) {
        int fileNum = 1;
        char numStr[] = "000";
        while((df = fopen(filepath, "r")) != NULL) {
                sprintf(numStr, "%0.3d", fileNum);
                for (int i=0; i<3; i++) {
                    filepath[14+i] = numStr[i];
                }
                fileNum++;
                fclose(df);
        }
        printf("Fast filepath: %s\n", filepath);
    }
    if (!(df = fopen(filepath, "r"))) {
        df = fopen(filepath, "w");
        printf("%s\r\n", (!df ? "Fail :(" : "OK"));
        if (!df) {
            printf("error: %s (%d)\r\n", strerror(errno), -errno);
            device_log(filenameLog,"Error::file open error"); //Update device log
            return(df);
        } else if (fileType == 1) { //[1] = datafile - write header on open
            printf("Writing header\n");
            scanner.headerWrite(df);
        }
    } else {
        fclose(df);
        df = fopen(filepath, "a");
    }

    return(df);
}

int server_connect(EthernetInterface &net, TCPSocket &server)
{
    int status = server.open(&net);
    server.bind(80); //bind http port to server
    server.set_blocking(false);
    server.set_timeout(100);
    status = server.listen();
    return(status);
}

void print_memory_info()
{
//stack stats
    int cnt = osThreadGetCount();
    mbed_stats_stack_t *stats = (mbed_stats_stack_t*) malloc(cnt * sizeof(mbed_stats_stack_t));

    cnt = mbed_stats_stack_get_each(stats, cnt);
    for (int i = 0; i < cnt; i++) {
        printf("Thread: 0x%1X, Stack Size: %lu / %lu\r\n", stats[i].thread_id, stats[i].max_size, stats[i].reserved_size);
    }
    free(stats);

//heap stats
    mbed_stats_heap_t heap_stats;
    mbed_stats_heap_get(&heap_stats);
    printf("Heap Size: %lu / %lu bytes\r\n", heap_stats.current_size, heap_stats.reserved_size);
}
//interrupts
void button_press()
{
    if (debounce.read_ms()>100) {
        led2 = !led2;
        debounce.reset();
    }
}
void poll_TCP()
{
    tcpFlag = 1;
}
void scan_request()
{
    sampleFlag = 1;
}
void tide_check(){
    led3 = !led3;
}
void server_ready() {
    serverFlag = 1;
}
//CreamT
int scanner_connect(EthernetInterface &net, TCPSocket &socket, char* scannerIP, SocketAddress &a) {
    int status;
    int i = 0;
    ThisThread::sleep_for(5s); //allow time for scanner to power up before connecting
    //Open a TCP socket on the network interface
    socket.set_blocking(false);
    socket.set_timeout(5);
    //connect to scanner using TCP port:2112
    status = net.gethostbyname(scannerIP, &a);
    a.set_port(2112);
    //manage TCP connection - setup timeout reconnect
    while (1) {
        status = socket.connect(a);
        if (status == 0)
            break;
        socket.close();
        socket.open(&net);
        printf("Cannot connect to TCP socket! [%d] [%d]\n", status, i);
        ThisThread::sleep_for(1s);
        if (i >= 3) {
            device_log(filenameLog,"Error::Cannot connect to TCP socket"); //Update device log
            break;
        }
        i++;
    }
    return(status);
}
time_t get_next_tide(char* filename)
{
    struct tm timeinfo;
    time_t tide;
    time_t now = time(NULL);
    char tstr[32];
    fflush(stdout);
    char filepath[24] = "/fs/";
    strcat(filepath, filename);
    FILE* f = fopen(filepath, "r");
    if (!f) {
        printf("Tide file not found!\n");
        return(2147483647);
    }
    while(fgets(tstr, sizeof(tstr), f) != NULL) {//get line string from file
        char* pch;
        if (strncmp(tstr, "20", 2) != 0)
            continue;
        pch = strtok(tstr, "\t"); //split string into tokens - multiple delimiters allowed
        timeinfo.tm_year = atoi(pch)-1900;
        pch = strtok(NULL, "\t");
        timeinfo.tm_mon = atoi(pch)-1;
        pch = strtok(NULL, "\t");
        timeinfo.tm_mday = atoi(pch);
        pch = strtok(NULL, "\t");
        timeinfo.tm_hour = atoi(pch);
        pch = strtok(NULL, "\t");
        timeinfo.tm_min = atoi(pch);
        pch = strtok(NULL, "\t");
        timeinfo.tm_sec = 0;

        tide = mktime(&timeinfo);
        if (difftime(tide, now) >= 0)
            break;
    }
    fclose(f);
    if (difftime(tide, now) < 0)
        tide = 2147483647; //set max future time if no valid times are found
    return(tide);
}