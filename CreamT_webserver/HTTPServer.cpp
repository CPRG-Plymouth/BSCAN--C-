#include "HTTPServer.h"


int HTTPServer::TCPreqHandler(TCPSocket* client, char* httpBuffer, char** updateVals)
{
    char httpHeader[256] = {};
    char TCPframe[536] = {};
    int cmd;
    client->recv(TCPframe, 536);
    strcpy(httpBuffer, TCPframe);
    if (strncmp(httpBuffer, "GET", 3) == 0)
        cmd = analyseGET(httpBuffer, updateVals);
    else if (strncmp(TCPframe, "POST", 4) == 0) {
        printf("POST: %s\n", httpBuffer);
        cmd = analysePOST(client, httpBuffer, updateVals);
    }
    else
        cmd = -1;

    //printf("CMD: [%d]\n", cmd);
    //printf("%s\n", httpBuffer);
    httpRespHandler(client, httpHeader, httpBuffer, cmd);
    return(cmd);
}
int HTTPServer::parseRequest(char* httpBuffer)
{
    return(1);
}
/**
* @brief   Analyses the received GET request
* @note    The string passed to this function will look like this:
*          GET /password HTTP/1.....
*          GET /password/ HTTP/1.....
*          GET /password/?sw=1 HTTP/1.....
*          GET /password/?sw=0 HTTP/1.....
* @param   url URL string
* @retval -1 invalid password
*         -2 no command given but password valid
*         -3 just refresh page
*          0 start scan
*          1 unused
*          2 update Battery Voltage
*          3 update Time Value
*/
int HTTPServer::analyseGET(char* httpBuffer, char** updateVals)
{
//seperate URL from full http header
    char GETreq[64] = {};
    strncpy(GETreq, httpBuffer, strstr(httpBuffer, "Host") - httpBuffer);
    //printf("GETreq: %s [%d]\n", GETreq, strlen(GETreq));

    if (strlen(GETreq) < (5 + strlen(PASSWORD) + 1))
        return(-1);

    int typeLen = 0;
    if (strncmp(GETreq, "GET", 3) == 0)
        typeLen = 5;

    if (strncmp(GETreq + typeLen, PASSWORD, strlen(PASSWORD)) != 0)
        return(-1);

    int pos = typeLen + strlen(PASSWORD);

//if (GETreq.substr(pos, 1) != "/")

    if (*(GETreq + pos) != '/')
        return(-1);

//if (GETreq.substr(pos++, 1) == " ")
    if (*(GETreq + pos++) == ' ')
        return(-2);
    char* pind = strstr(GETreq, " HTTP/");
//string  cmd(GETreq.substr(pos, 5));
    *(GETreq + (pind-GETreq)) = '\0';    // terminate the cmd string
    char*   cmd = ((GETreq + pos));
    //printf("commandStr: [%s]\n",cmd);
    if (strcmp(cmd, "?SSCAN") == 0) {
        strcpy(httpBuffer, "Scanning [-]");
        return(0);
    }
    if (strcmp(cmd, "?FSCAN") == 0) {
        strcpy(httpBuffer, "Scanning [0]");
        return(1);
    }
//handle AJAX data requests
    char timeStr[20];
    if (strcmp(cmd, "?Vval") == 0) {
        strcpy(httpBuffer, updateVals[0]);
        return(2);
    }
    if (strcmp(cmd, "?Tval") == 0) {
        getTime(timeStr);
        strcpy(httpBuffer, timeStr);
        return(2);
    }
    if (strcmp(cmd, "?TIDE") == 0) {
        strcpy(httpBuffer, updateVals[1]);
        return(2);
    }
    if (strcmp(cmd, "?State") == 0) {
        strcpy(httpBuffer, updateVals[6]);
        return(2);
    }
    if (strcmp(cmd, "?sTime") == 0) {
        strcpy(httpBuffer, "Scan time: ");
        strcat(httpBuffer, updateVals[2]);
        strcat(httpBuffer, " s");
        return(2);
    }
    if (strcmp(cmd, "?fTime") == 0) {
        strcpy(httpBuffer, "Burst time: ");
        strcat(httpBuffer, updateVals[3]);
        strcat(httpBuffer, " s");
        return(2);
    }
    if (strcmp(cmd, "?fNum") == 0) {
        strcpy(httpBuffer, "Burst num: ");
        strcat(httpBuffer, updateVals[4]);
        return(2);
    }
    if (strcmp(cmd, "?filename") == 0) {
        strcpy(httpBuffer, updateVals[5]);
        return(2);
    }
    if (strcmp(cmd, "?RESTART") == 0) {
        strcpy(httpBuffer, "Controller restarting!");
        return(6);
    }
    if (strcmp(cmd, "?FILE") == 0) {
        strcpy(httpBuffer, cmd);
        return(10);
    }
    if (strstr(cmd, ".dat") || strstr(cmd, ".log") || strstr(cmd, ".txt")) {
        strcpy(httpBuffer, cmd);
        return(11);
    }
//printf("CMDLEN [%d]\n",strlen(cmd));
    if (strlen(cmd) == 0)
        return(-3);
    return(-1);
}
int HTTPServer::analysePOST(TCPSocket* client, char* httpBuffer, char** updateVals)
{
    char* pind;
    char TCPframe[537];
    int cmd = 0;
    pind = strstr(httpBuffer, "Content-Type: ");
    if(!pind)
        return(-1);

    if (strncmp(pind+14, "multipart/form-data", 19) == 0) {
        client->set_blocking(false);
        client->set_timeout(10);
        cmd = saveFile(client, httpBuffer);
        client->set_blocking(true); 
    } else if (strncmp(pind+14, "text/plain", 10) == 0) {
        client->recv(TCPframe, 537);
        pind = strstr(TCPframe, "\r\n\r\n")+4;  
        printf("Data: %s\n", pind); 
        if(strncmp(pind, "sTime=", 6) == 0) {  
            if (strlen(pind+6)-2 != 0) {
                for (int i=0; i<strlen(pind+6)-2; i++) {
                    updateVals[2][i] = pind[6+i];
                    updateVals[2][i+1] = '\0';
                }
            }
            return(3);
        } else if(strncmp(pind, "fTime=", 6) == 0) {  
            if (strlen(pind+6)-2 != 0) {
                for (int i=0; i<strlen(pind+6)-2; i++) {
                    updateVals[3][i] = pind[6+i];
                    updateVals[3][i+1] = '\0';
                }
            }
            return(4);
        } else if(strncmp(pind, "fNum=", 5) == 0) {  
            if (strlen(pind+5)-2 != 0) {
                for (int i=0; i<strlen(pind+5)-2; i++) {
                    updateVals[4][i] = pind[5+i];
                    updateVals[4][i+1] = '\0';
                }
            }
            return(5);
        } 
    } else
        return(-1);
        
    return(20);
}
int HTTPServer::sendIDdata(TCPSocket* client, char* IDdata)
{
    return(1);
}
/**
* @brief
* @note
* @param
* @retval
*/
char* HTTPServer::movedPermanently(int flag)
{
    memset(responseBuf, 0, sizeof(responseBuf));
    if (flag == 1) {
        strcpy(responseBuf, "/");
        strcat(responseBuf, PASSWORD);
        strcat(responseBuf, "/");
    }

    strcat(responseBuf, "<h1>301 Moved Permanently</h1>\r\n");
    return(responseBuf);
}

/**
* @brief
* @note
* @param
* @retval
*/

int HTTPServer::setPassword(const char* password, int passLength)
{
    if (sizeof(PASSWORD) < passLength)
        return(0);

    strcpy(PASSWORD, password);
    return(1);
}
//private
int HTTPServer::httpRespHandler(TCPSocket* client, char* httpHeader, char* httpBuffer, int cmd)
{
    switch (cmd) {
        case 3:
        case 4:
        case 5:
        case -3:
// update webpage
            strcpy(httpHeader, "HTTP/1.1 200 OK");
            strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
            loadMainPage(client, httpHeader);
            break;
        case -2:
// redirect to the right base url
            strcpy(httpHeader, "HTTP/1.1 301 Moved Permanently\r\nLocation: ");
            strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
            sendHTTP(client, httpHeader, movedPermanently(1));
            break;

        case -1:
            strcpy(httpHeader, "HTTP/1.1 401 Unauthorized");
            strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
            strcpy(httpBuffer, "<h1>401 Unauthorized</h1>");
            sendHTTP(client, httpHeader, httpBuffer);
            break;
        case 0:
        case 1:
        case 2:
        case 6:
        case 7:
            strcpy(httpHeader, "HTTP/1.1 200 OK");
            strcat(httpHeader, "\r\nContent-Type: text/plain\r\n");
            sendHTTP(client, httpHeader, httpBuffer);
            break;
        case 10:
//update list of files
            strcpy(httpHeader, "HTTP/1.1 200 OK");
            strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
            if (readDir(client, httpHeader, httpBuffer) == -1) {
                strcpy(httpHeader, "HTTP/1.1 401 Unauthorized");
                strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
                strcpy(httpBuffer, "<h1>401 Unauthorized</h1>");
                sendHTTP(client, httpHeader, httpBuffer);
            }
            break;
        case 11:
//download file response
            strcpy(httpHeader, "HTTP/1.1 200 OK");
            strcat(httpHeader, "\r\nContent-Type: text/plain\r\n");
            if (sendFile(client, httpHeader, httpBuffer) == -1) {
                strcpy(httpHeader, "HTTP/1.1 401 Unauthorized");
                strcat(httpHeader, "\r\nContent-Type: text/html\r\n");
                strcpy(httpBuffer, "<h1>401 Unauthorized</h1>");
                sendHTTP(client, httpHeader, httpBuffer);
            }
            break;
        case 20:
// POST response
            strcpy(httpHeader, "HTTP/1.1 201 CREATED");
            strcat(httpHeader, "\r\nContent-Type: text/plain\r\n");
            sendHTTP(client, httpHeader, httpBuffer);
//saveFile(client, httpHeader);
            break;
        default: ;
    }
    return(1);
}
void HTTPServer::sendHTTP(TCPSocket* client, char* header, char* content)
{
    char content_length[10] = {};
    sprintf(content_length, "%u\r\n", strlen(content));
    strcat(header, "Content-Length: ");
    strcat(header, content_length);
    strcat(header, "Pragma: no-cache\r\n");
    strcat(header, "Connection: close\r\n\r\n");

    char c = content[0];
    memset(responseBuf, 0, sizeof(responseBuf));
    memmove(responseBuf + strlen(header), content, strlen(content)+1);    // make room for the header
    strcpy(responseBuf, header);                                        // copy the header on front of the content
    responseBuf[strlen(header)] = c;
    client->send((int*)responseBuf, strlen(responseBuf));
//printf("sendResponse: %s\n", responseBuf);
}
int HTTPServer::loadMainPage(TCPSocket* client, char* header) //load html site from SD card
{
    strcat(header, "Pragma: no-cache\r\n");
    strcat(header, "Connection: close\r\n\r\n");

    //printf("Opening index.html\n");
    fflush(stdout);
    FILE* f = fopen("/fs/index.html","r");
    if (!f) {
        printf("ERROR: File cannot be opened");
        return(0);
    }
    client->send((int*)header, strlen(header));
    while(!feof(f)) {
        char* plist;
        int result = fread(responseBuf, 1, 536, f);
        responseBuf[result] = '\0';
        client->send((int*)responseBuf, strlen(responseBuf));
    }
    fclose(f);

    return(1);
}
int HTTPServer::getTime(char* timeStr)
{
    time_t seconds = time(NULL);
    strftime(timeStr,20,"%Y-%m-%d %H:%M:%S",localtime(&seconds));
    return(1);
}
int HTTPServer::readDir(TCPSocket* client, char* httpHeader, char* httpBuffer)
{
    char filename[32];
    char filepath[36];
    char filesize[16];
//build header
    strcat(httpHeader, "Pragma: no-cache\r\n");
    strcat(httpHeader, "Connection: close\r\n\r\n");
// Display the root directory
    struct stat sd;
    //printf("Opening the root directory... ");
    fflush(stdout);
    DIR *d = opendir("/fs/");
    printf("%s\n", (!d ? "Fail :(" : "OK"));
    if (!d) {
//error("error: %s (%d)\n", strerror(errno), -errno);
    return(-1);
    }
//build table of available files
    client->send((int*)httpHeader, strlen(httpHeader));
    httpBuffer[0] = '\0';
    strcpy(httpBuffer,
           "<thead>\n"
           "  <th style=\"height: 30px; text-align:left\">File</th>\n"
           "  <th style=\"text-align:right;\">Size [KB]</th>\n"
           "  <th></th>\n"
           "</thead>\n"
           "<tbody>\n");
    client->send((int*)httpBuffer, strlen(httpBuffer));
    while (true) {
        struct dirent *e = readdir(d);
        if (!e) {
            break;
        }

        strcpy(filename, e->d_name);
        sprintf(filepath,"/fs/%s", filename);
        stat(filepath, &sd);
        sprintf(filesize, "%u", sd.st_size/1000);
        if(strstr(filename, ".dat")||strstr(filename, ".log")||strstr(filename, ".txt")) {

            strcpy(httpBuffer,
                   "  <tr style=\"text-align:left;\">\n"
                   "  <td>"
                  );
            strcat(httpBuffer, filename);
            strcat(httpBuffer,
                   "</td>\n"
                   "  <td style=\"text-align:right;\">"
                  );
            strcat(httpBuffer, filesize);
            strcat(httpBuffer,
                   "</td>\n"
                   "  <td style=\"text-align:center;\"><a href=\"/CreamT/"
                  );
            strcat(httpBuffer,filename);
            strcat(httpBuffer,
                   "\" class=\"button\">Download!</a></td>\n"
                   "  </tr>\n"
                  );
            client->send((int*)httpBuffer, strlen(httpBuffer));
        }
    }
    strcpy(httpBuffer, "</tbody>\n");
    client->send((int*)httpBuffer, strlen(httpBuffer));
    //printf("Closing the root directory... ");
    fflush(stdout);
    int err = closedir(d);
    printf("%s\n", (err < 0 ? "Fail :(" : "OK"));
    if (err < 0) {
//error("error: %s (%d)\n", strerror(errno), -errno);
    }

    return(1);
}
int HTTPServer::sendFile(TCPSocket* client, char* header, char* httpBuffer)
{
// Display the root directory
    strcat(header, "Pragma: no-cache\r\n");
    strcat(header, "Connection: close\r\n\r\n");
//printf("Opening the root directory... ");
    fflush(stdout);

    char filename[32] = {};

//printf("Opening File\n");
    sprintf(filename,"/fs/%s", httpBuffer);
//printf("Filename: [%s]", filename);
    fflush(stdout);
    client->send((int*)header, strlen(header));
    FILE* f = fopen(filename, "r");
    if(!f)
        return(-1);
    while(!feof(f)) {
        int result = fread(httpBuffer, 1, 536, f);
        httpBuffer[result] = '\0';

        client->send((int*)httpBuffer, strlen(httpBuffer));
    }
    fclose(f);

    return(1);
}
int HTTPServer::saveFile(TCPSocket* client, char* httpBuffer)
{
//Awfully messy - needs rewriting
//get header
    char* pfile;
    char* pend;
    char frameTemp[64] = {};
    char TCPframe[537] = {};
    
    char filename[32] = {"/fs/\0"};
    while(client->recv(TCPframe, 537)) {
        pfile = strstr(TCPframe, "\r\n\r\n");
        if(pfile)
            break;
        strcat(httpBuffer, TCPframe);
    }

    strncat(httpBuffer, TCPframe, pfile-TCPframe);
//printf("POST HEADER: %s\n", httpBuffer);

//get Multipart Body
    strcpy(httpBuffer, pfile+4);
    while(client->recv(TCPframe, 537)) {
        pfile = strstr(TCPframe, "\r\n\r\n");
        if(pfile)
            break;
        strcat(httpBuffer, TCPframe);
    }
    strncat(httpBuffer, TCPframe, pfile-TCPframe);
//printf("\nPOST BODY: %s\n", httpBuffer);
    if (strncmp(httpBuffer, "------WebKitFormBoundary", 24) !=0)
        return(-1);
    char* pname = strstr(httpBuffer, "filename=");
    pname = strchr(pname, '"') + 1;
    int count = strchr(pname, '"') - pname;
    strncat(filename, pname, count);
    /*if (strcmp(filename, "/fs/index.html") !=0)
    return(-1);*/

    //printf("Opening \"%s\"...\n", filename);
    fflush(stdout);

    FILE* f = fopen(filename, "w");
    fclose(f);
    f = fopen(filename, "a");
    if (!f) {
//Create the data file if it doesn't exist
        printf("Cannot create file!");
        strcpy(httpBuffer, "File cannot be created!");
        return(10);
    }

//get file contents - needs rewriting
    strcpy(httpBuffer, pfile+4);
    char* peof = strstr(httpBuffer, "\r\n------WebKitFormBoundary");
    if(peof)
        httpBuffer[peof-httpBuffer] = '\0';
    printf("\nPOST FILE1: %s\n", httpBuffer);
    //handle form boundary spanning frames
    pend = &httpBuffer[strlen(httpBuffer)-26];
    strcpy(frameTemp, pend);

    if (peof) {
        *peof = '\0';
        strcpy(httpBuffer, filename);
        strcat(httpBuffer, " successfully uploaded!");
        fclose(f);
        return(10);
    }
    while(client->recv(TCPframe, 537)) {
        pfile = strstr(TCPframe, "\r\n------WebKitFormBoundary");
        if(pfile) {
            strcpy(httpBuffer, TCPframe);
            break;
        }
        strcpy(httpBuffer, TCPframe);        
        fflush(stdout);
        fprintf(f, "%s", httpBuffer);
        pend = &httpBuffer[strlen(httpBuffer)-26];
        strcpy(frameTemp, pend);
        printf("\nPOST FILE2: %s\n", httpBuffer);
        strncat(frameTemp, TCPframe, 26);
        printf("FRAMETEMP = %s\n", frameTemp);
        pfile = strstr(frameTemp, "\r\n------WebKitFormBoundary");
        if(pfile) {
            strncat(httpBuffer, TCPframe, 26);
            printf("DONE!!!\n");
            break;
        }
    }
    pfile = strstr(httpBuffer, "\r\n------WebKitFormBoundary");
    httpBuffer[pfile-httpBuffer] = '\0';
    fflush(stdout);
    fprintf(f, "%s", httpBuffer);
    printf("\nPOST FILE3: %s\n", httpBuffer);
    strcpy(httpBuffer, filename);
    strcat(httpBuffer, " successfully uploaded!");
    fclose(f);
    return(10);
}