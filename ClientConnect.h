/** ClientConnect.h
*/

#pragma once

//#include <string>

class ClientConnect
{
    // the listener socket
    int sockfd;

    int InitClientConnection(int portno);

public:
    ClientConnect(int portno);
    ~ClientConnect();

    int GetClientRequest(void);
    int GetListenerSocket(void) { return sockfd; }
    static const char* ParseRequest(const char* buffer);
    static void SendResponse(const char* resp, int newsockfd);
};