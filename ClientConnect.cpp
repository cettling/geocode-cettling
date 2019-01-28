/** ClientConnect.cpp
*/

#include <iostream>
#include <sys/types.h> 
#ifdef WIN32
#include <winsock2.h>
#define CLOSESOCK  closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#define CLOSESOCK  close
#endif
#include "ClientConnect.h"
#include "GeoConnect.h"

// init the listener for client requests
int
ClientConnect::InitClientConnection(int portno)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        cout << "ERROR opening socket" << endl;
    else {
        struct sockaddr_in serv_addr;
        memset((char *)&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(portno);

        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)opt, sizeof(opt));
       /* struct timeval tv;
        tv.tv_sec = timeout_in_seconds;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));*/

        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
            cout << "ERROR on binding socket " << sockfd << endl;
            CLOSESOCK(sockfd);
            sockfd = -1;
        }
        else 
            listen(sockfd, 5);
    }
    return sockfd;
}

// get client request
int
ClientConnect::GetClientRequest(void)
{
    if (sockfd < 0) {
        return -1;
    }
    char buffer[256];
    struct sockaddr_in cli_addr;
    int clilen = sizeof(cli_addr);
    int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) {
        cout << "ERROR on accept" << endl;
    }
    else {
        memset(buffer, 0, 256);
        int n = recv(newsockfd, buffer, 255, 0);
        if (n < 0) {
            cout << "ERROR reading from socket " << newsockfd << endl;
            string err("Could not get coordinates\n");
            send(newsockfd, err.c_str(), err.size(), 0);
            CLOSESOCK(newsockfd);
            newsockfd = -1;
        }
        else {
            buffer[n] = '\0';  // make sure it's a proper string
            const char* req = ParseRequest( buffer);
            if (NULL == req) {
                string err("'addr' not found in query\n");
                send(newsockfd, err.c_str(), err.size(), 0);
                CLOSESOCK(newsockfd);
                newsockfd = -1;
            }
            else {
                GeoConnect::PushRequest(req, newsockfd);
            }
        }
    }
    return newsockfd;
}

// given a client's full request string, return the requested address
const char*
ClientConnect::ParseRequest(const char* buffer)
{
    static const char* addrKey = "?addr=";
    const char* addr = strstr(buffer, addrKey);
    if (addr) {
        const char* req = addr + sizeof(addrKey) - 1;
        return req;
    }
    return NULL;
}

ClientConnect::ClientConnect(int portno)
{
    sockfd = InitClientConnection(portno);
}

ClientConnect::~ClientConnect()
{
    if(sockfd >= 0) {
        CLOSESOCK(sockfd);
    }
}

void
ClientConnect::SendResponse(const char* resp, int newsockfd)
{
    send(newsockfd, resp, strlen(resp), 0);
    CLOSESOCK(newsockfd);
}