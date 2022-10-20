#include "TCPSocket.h"
#include "FATFileSystem.h"

#ifndef HTTPSERV_H_
#define HTTPSERV_H_
#endif

class HTTPServer
{
public:
    HTTPServer() {};
    /**
     * @brief   Analyses the received URL
     * @note    The string passed to this function will look like this:
     *          GET /password HTTP/1.....
     *          GET /password/ HTTP/1.....
     *          GET /password/?sw=1 HTTP/1.....
     *          GET /password/?sw=0 HTTP/1.....
     * @param   url URL string
     * @retval -1 invalid password
     *         -2 no command given but password valid
     *         -3 just refresh page
     *          0 switch off
     *          1 switch on
     */
    int setPassword(const char* password, int passLength);
    int TCPreqHandler(TCPSocket* client, char* httpBuffer, char** updateVals);
    int parseRequest(char* httpBuffer);
    int analyseGET(char* httpBuffer, char** updateVals);
    int analysePOST(TCPSocket* client, char* httpBuffer, char** updateVals);
    int sendIDdata(TCPSocket* client, char* IDdata);
    char* movedPermanently(int flag);
private:
    char PASSWORD[16];
    char responseBuf[3000];
    
    int httpRespHandler(TCPSocket* client, char* httpHeader, char* httpBuffer, int cmd);
    void sendHTTP(TCPSocket* client, char* header, char* content);
    int loadMainPage(TCPSocket* client, char* header);
    int getTime(char* timeStr); 
    int readDir(TCPSocket* client, char* httpHeader, char* httpBuffer);   
    int sendFile(TCPSocket* client, char* httpHeader, char* httpBuffer);
    int saveFile(TCPSocket* client, char* httpPost);
};
