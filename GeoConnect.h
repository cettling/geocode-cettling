/** GeoConnect.h
*/
#pragma once

#include <map>
#include <string>
#include "lib_json/include/json.h"


using namespace std;

#define HTTP_PORT  80

#define MAX_CONNECTIONS  4

/** Base class for connection and communication with geo servers
*/
class GeoConnect
{
    static string SubstReqString(int index, const string& reqAddr);
    static int SetRequest(const string& reqAddr, int currIndex, int reqSock);
    static void SetDefaultOrder(void);
    int SendGeoRequest(int sock, string req);
    int ReceiveGeoRequest(int sock, string& resp);

protected:
    Json::Value JsonString2Object(string jsonStr);
    string BuildOutString(const string& fullAddr, double latitude, double longitude);
    virtual string HandleReply(string json) = 0;

public:
    virtual ~GeoConnect() { }
    GeoConnect();

    // template string for each geo server
    string connectAddr;
    // socket ID for each geo server
    int geoSock;

    static void ReadConfig(string cfgFileName);
    static void PushRequest(const char* reqAddr, int reqSock);
    static void ProcessRequest(int sock);
};

/** Derived classes for each geo server
*/
class GeoConnectGoogle : public GeoConnect
{
public:
    GeoConnectGoogle() {};
    static GeoConnectGoogle* create() { return new GeoConnectGoogle; }
    string HandleReply(string json);
};

class GeoConnectHERE : public GeoConnect
{
public:
    GeoConnectHERE() {}
    static GeoConnectHERE* create() { return new GeoConnectHERE; }
    string HandleReply(string json);
};

/** Get the current server socket
*/
int ReadQueueSocket(int i);
