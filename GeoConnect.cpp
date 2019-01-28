/** GeoConnect.cpp
*/

#include <iostream>
#include <fstream>
#include <queue>
#include <string.h>
#include <sys/types.h>

#ifdef WIN32
#include <winsock2.h>
WORD versionWanted = MAKEWORD(1, 1);
WSADATA wsaData;
//WSAStartup(versionWanted, &wsaData);
#define CLOSESOCK  closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#define CLOSESOCK  close
#endif

#include "GeoConnect.h"
#include "ClientConnect.h"
#include "Errors.h"

//#include "lib_json/include/json.h"
//#include "lib_json/include/reader.h"
//#include "lib_json/include/writer.h"
//#include "lib_json/include/value.h"

// Default geocode query strings.  Actual ones should be given in cfg file.
#define GEOCODE_SITE_HERE       "https://geocoder.api.here.com/6.2/geocode.json?app_id={APP_ID}&app_code={APP_CODE}&searchtext={REQ_ADDR}"
#define GEOCODE_SITE_GOOGLE     "https://maps.googleapis.com/maps/api/geocode/json?address={REQ_ADDR}&key={API_KEY}"
     
#define SUBST_APP_ID    "{APP_ID}"
#define SUBST_APP_CODE  "{APP_CODE}"
#define SUBST_API_KEY   "{API_KEY}"
#define SUBST_REQ_ADDR  "{REQ_ADDR}"

//test
#define GOOGLE_API_KEY  "AIzaSyArGoihZ66qRPdnYqnSox2gofuhWrYIE4g"
//  AIzaSyCByO0v_YMnDkt1qWlD3egY1oDA6O7XhBo 
// https://maps.googleapis.com/maps/api/geocode/json?address=3549+Jordan+Rd,+Oakland+CA&key=AIzaSyCByO0v_YMnDkt1qWlD3egY1oDA6O7XhBo
// https://maps.googleapis.com/maps/api/staticmap/json?address=3549+Jordan+Rd,+Oakland+CA&key=AIzaSyArGoihZ66qRPdnYqnSox2gofuhWrYIE4g

#define HERE_APP_ID     "nsAZMhXUr4UIVZOdyUjS"
#define HERE_APP_CODE   "5IRTYqBmncHJhDNzyahEiA"
// https://geocoder.api.here.com/6.2/geocode.json?app_id=nsAZMhXUr4UIVZOdyUjS&app_code=5IRTYqBmncHJhDNzyahEiA&searchtext=3549+Jordan+Rd,+Oakland+CA

enum { QSTATE_IDLE, QSTATE_WAIT_SERVER };

// Info that gets queued, ready to send to geo server
typedef struct OutElem {
    struct OutElem* next;
    int clientSock;     // client socket, will use to return response
    int serverSock;     // server socket, for easy access
    GeoConnect* server; // the geo server
    int index;          // the current index in the connectOrderArray
    string geoReq;      // the string to send to the geo server
    int state;          // waiting for server response?
} OutElem_t;

class GeoQueue
{
    OutElem_t* qHead;

public:
    GeoQueue() { qHead = NULL; };
    void push(OutElem_t* elem);
    OutElem_t* pop(void);
    OutElem_t* front(void);
    OutElem_t* find(int serverfd, bool remove);
    OutElem_t* locate(int index);
    bool empty(void) { return (NULL == qHead); }
};

// for geo server setup, creates server object if given in cfg file.
typedef struct {
    const char* key;
    const char* defAddr;
    GeoConnect* fn;
} GeoMapChoices_t;
static GeoMapChoices_t s_geoMaps[MAX_CONNECTIONS] ={
    { "GeoConnectGoogle", GEOCODE_SITE_GOOGLE, GeoConnectGoogle::create() },
    { "GeoConnectHERE", GEOCODE_SITE_HERE, GeoConnectHERE::create() },
    { NULL, NULL, NULL },
    { NULL, NULL, NULL }
};

// array of servers to access, first access from lower index
static GeoConnect* s_connectOrderArray[MAX_CONNECTIONS] ={ {NULL} };

// queue up the server requests
static GeoQueue s_outQ;

void
GeoQueue::push(OutElem_t* elem)
{
    OutElem_t* p = qHead;
    OutElem_t* last = NULL;
    while (p) {
        last = p;
        p = p->next;
    }
    if (NULL == last)
        qHead = elem;
    else
        last->next = elem;
    elem->next = NULL;
}

OutElem_t*
GeoQueue::pop(void)
{
    OutElem_t* p = qHead;
    if (p) {
        qHead = p->next;
        p->next = NULL;
    }
    return p;
}

OutElem_t*
GeoQueue::front(void)
{
    return qHead;
}

OutElem_t*
GeoQueue::find(int sock, bool remove)
{
    OutElem_t* p = qHead;
    OutElem_t* last = NULL;
    while (p) {
        if (sock == p->serverSock) {
            if (remove) {
                if (last)
                    last->next = p->next;
                else
                    qHead = p->next;
                p->next = NULL;
            }
            return p;
        }
        last = p;
        p = p->next;
    }
    return NULL;
}

OutElem_t* 
GeoQueue::locate(int index)
{
    OutElem_t* p = qHead;
    int i = 0;
    while (p) {
        if (i == index)
            return p;
        p = p->next;
        i++;
    }
    return NULL;
}

GeoConnect::GeoConnect()
{
    
}

/** Given a string like "https://server.com/path?addr=....",
*   pull out "server.com"
*/
static string
GetHostName(const char* addr)
{
    const char* start = strstr(addr, "://");
    if (start) {
        start += 3;
        const char* end = strchr(start, '/');
        if (end) {
            int len = end - start;
            return string(start, len);
        }
    }
    return string();
}

// Init the TCP or UDP connection to server.
static int
InitGeoConnection(const char* hostname, int portno)
{
    if( (NULL == hostname) || !hostname[0]) {
        return -1;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        cout << "ERROR opening geo socket" << endl;
    else {
        struct hostent* server = gethostbyname(hostname);
        if (server == NULL) {
            cout << "ERROR, no such host: " << hostname << endl;
            CLOSESOCK(sockfd);
            sockfd = -1;
        }
        else {
            struct sockaddr_in serv_addr;
            memset((char *)&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            memmove((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
            serv_addr.sin_port = htons(portno);
            if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
                cout << "ERROR connecting to geo" << endl;
                CLOSESOCK(sockfd);
                sockfd = -1;
            }
        }
    }
    return sockfd;
}

// set up the server object to the ordered array per the index
static void
MakeGeoConnect(const char* className, const char* addr, int index)
{
    for (int i=0; i < MAX_CONNECTIONS; i++) {
        if (0 == strcmp(className, s_geoMaps[i].key)) {
            GeoConnect* p = s_geoMaps[i].fn;
            s_connectOrderArray[index] = p;
            p->connectAddr = addr;
            string hostname = GetHostName(addr);
            p->geoSock = InitGeoConnection(hostname.c_str(), HTTP_PORT);
            break;
        }
    }
}

// read the config file and set up the servers
void
GeoConnect::ReadConfig(string cfgFileName)
{
    enum { OUT_SITE, IN_SITE };
    if (0 == cfgFileName.size()) {
        SetDefaultOrder();
    }
    else {
        ifstream sites;
        sites.open(cfgFileName.c_str(), ios::in);
        if (sites.fail())
            SetDefaultOrder();
        else {
            const int LINE_SIZE = 256;
            const int KEY_SIZE = 12;
            const int VALUE_SIZE = LINE_SIZE - KEY_SIZE;
            int index = 0;
            char addrBuf[VALUE_SIZE];
            char classBuf[VALUE_SIZE];
            addrBuf[0] = '\0';
            classBuf[0] = '\0';
            int readState = OUT_SITE;
            char buf[LINE_SIZE];
            sites.getline(buf, LINE_SIZE);
            while ((!sites.fail()) && (index < MAX_CONNECTIONS)) {
                char key[KEY_SIZE];
                char value[VALUE_SIZE];
                int convs;
                switch (readState) {
                case OUT_SITE:
                    if ('[' == buf[0]) {
                        readState = IN_SITE;
                        if (addrBuf[0] && classBuf[0]) {
                            MakeGeoConnect(classBuf, addrBuf, index);
                            addrBuf[0] = '\0';
                            classBuf[0] = '\0';
                            index++;
                        }
                        else if (addrBuf[0]) {
                            cout << "Missing class name for site " << index + 1 << endl;
                        }
                        else if (classBuf[0]) {
                            cout << "Missing address name for site " << index + 1 << endl;
                        }
                    }
                    break;
                case IN_SITE:
                    // format defined by Postmates: "addrs=" or "class="
                    convs = sscanf(buf, "%5s=%s", key, value);
                    if (2 == convs) {
                        if (0 == strncmp("addrs", key, 5)) {
                            if (strstr(value, SUBST_REQ_ADDR)) {
                                // must have request address spot
                                strncpy(addrBuf, value, sizeof(addrBuf));
                            }
                        }
                        else if (0 == strncmp("class", key, 5)) {
                            strncpy(classBuf, value, sizeof(classBuf));
                        }
                    }
                    break;
                default:
                    break;
                }
                sites.getline(buf, LINE_SIZE);
            }
        }
        sites.close();
    }
}

// use default settings if no proper config file
void
GeoConnect::SetDefaultOrder(void)
{
    MakeGeoConnect(s_geoMaps[0].key, s_geoMaps[0].defAddr, 0);
    MakeGeoConnect(s_geoMaps[1].key, s_geoMaps[1].defAddr, 1);
}

// push a geo request to the queue
int
GeoConnect::SetRequest(const string& reqAddr, int currIndex, int reqSock)
{
    int rc = ERR_NO_CONNECTION;
    for (int i= currIndex+1; i < MAX_CONNECTIONS; i++) {
        if (NULL == s_connectOrderArray[i]) {
            break;
        }
        if (0 > s_connectOrderArray[i]->geoSock) {
            continue;
        }
        OutElem_t* elem = new OutElem_t;
        elem->geoReq = SubstReqString(i, reqAddr);
        if (elem->geoReq.size() == 0) {
            delete elem;
        }
        else {
            elem->server = s_connectOrderArray[i];
            elem->serverSock = s_connectOrderArray[i]->geoSock;
            elem->clientSock = reqSock;
            elem->index = i;
            elem->state = QSTATE_IDLE;
            s_outQ.push(elem);
            rc = ERR_OK;
        }
        break;
    }
    return rc;
}

// push a geo request to the queue
void
GeoConnect::PushRequest(const char* reqAddr, int reqSock)
{
    int rc = SetRequest(string(reqAddr), -1, reqSock);
    if (ERR_NO_CONNECTION == rc)
        cout << "Cannot find geo server" << endl;
}

// pop a geo request off the queue
void
GeoConnect::ProcessRequest(int sock)
{
    if (!s_outQ.empty()) {
        OutElem_t* elem = s_outQ.find(sock, false);
        if (NULL == elem)
            return;
        GeoConnect* p = s_connectOrderArray[elem->index];
        if (QSTATE_IDLE == elem->state) {
            int rc = p->SendGeoRequest(sock, elem->geoReq);
            if (rc >= 0) {
                elem->state = QSTATE_WAIT_SERVER;
            }
            else {
                string err("Unable to send: ");
                err.append(elem->geoReq);
                err.append("\n");
                ClientConnect::SendResponse(err.c_str(), elem->clientSock);
                s_outQ.find(sock, true);
                delete elem;
            }
        }
        else if (QSTATE_WAIT_SERVER == elem->state) {
            s_outQ.find(sock, true);
            string jsonResp;
            int rc = p->ReceiveGeoRequest(sock, jsonResp);
            if (rc >= 0) {
                string resp = elem->server->HandleReply(elem->geoReq);
                ClientConnect::SendResponse(resp.c_str(), elem->clientSock);
            }
            else {
                // try again with another server
                int newIndex = elem->index + 1;
                string reqAddr(ClientConnect::ParseRequest(elem->geoReq.c_str()));
                SetRequest(reqAddr, newIndex, elem->clientSock);
            }
            delete elem;
        }
    }
}

// return the server socket associated with the nth queue element
int
ReadQueueSocket(int index)
{
    int sock = -1;
    if (!s_outQ.empty()) {
        OutElem_t* elem = s_outQ.locate(index);
        if(elem ) {
            sock = elem->serverSock;
        }
    }
    return sock;
}

// return a string with the user's address plugged into the request template
string
GeoConnect::SubstReqString(int index, const string& reqAddr)
{
    if (index >= MAX_CONNECTIONS)
        return string();
    string out(s_connectOrderArray[index]->connectAddr);
    size_t /* pos = out.find(SUBST_APP_ID);
    if (string::npos != pos) {
        out.replace(pos, strlen(SUBST_APP_ID), cfg.app_id);
    }
    pos = out.find(SUBST_APP_CODE);
    if (string::npos != pos) {
        out.replace(pos, strlen(SUBST_APP_CODE), cfg.app_code);
    }
    pos = out.find(SUBST_API_KEY);
    if (string::npos != pos) {
        out.replace(pos, strlen(SUBST_API_KEY), cfg.api_key);
    } */
    pos = out.find(SUBST_REQ_ADDR);
    if (string::npos != pos) {
        out.replace(pos, strlen(SUBST_REQ_ADDR), reqAddr);
    }
    return out;
}

// convert a json string into a json object
Json::Value
GeoConnect::JsonString2Object(string jsonStr)
{
    Json::CharReaderBuilder builder;
    Json::CharReader* reader = builder.newCharReader();

    Json::Value json;
    string errors;

    bool parsingSuccessful = reader->parse(
        jsonStr.c_str(),
        jsonStr.c_str() + jsonStr.size(),
        &json,
        &errors
    );
    delete reader;

    if (!parsingSuccessful) {
        cout << "Failed to parse the JSON, errors:" << endl;
        cout << errors << endl;
    }

    return json;
}

// return a string with the response info
string
GeoConnect::BuildOutString(const string& fullAddr, double latitude, double longitude)
{
    string out("{\"geocode\":{\"address\":\"");
    out.append(fullAddr);
    out.append("\",\"map\":{\"lat\":");
    char buf[32];
    sprintf(buf, "%.5lf", latitude);
    out.append(buf);
    out.append(",\"lng\":");
    sprintf(buf, "%.5lf", longitude);
    out.append(buf);
    out.append("}}}");
    return out;
}

// handle the response from a geo server
string
GeoConnectGoogle::HandleReply(string jsonStr)
{
    Json::Value json = JsonString2Object(jsonStr);
    if (!json.empty()) {
        string fullAddr = json["results[0].formatted_address"].asString();
        double latitude = json["results[0].geometry.location.lat"].asDouble();
        double longitude = json["results[0].geometry.location.lng"].asDouble();
        return BuildOutString(fullAddr, latitude, longitude);
    }
    return string("No information available");
}

// handle the response from a geo server
string
GeoConnectHERE::HandleReply(string jsonStr)
{
    Json::Value json = JsonString2Object(jsonStr);
    if (!json.empty()) {
        string fullAddr = json["Response.View[0].Result[0].Location.Address.Label"].asString();
        double latitude = json["Response.View[0].Result[0].Location.NavigationPosition[0].Latitude"].asDouble();
        double longitude = json["Response.View[0].Result[0].Location.NavigationPosition[0].Longitude"].asDouble();
        return BuildOutString(fullAddr, latitude, longitude);
    }
    return string("No information available");
}

// send request string to geo server
int
GeoConnect::SendGeoRequest(int sock, string req)
{
    int rc = -1;
    int n = send(sock, req.c_str(), req.size(), 0);
    if (n < 0)
        cout << "ERROR writing to geo socket: " << req << endl;
    else
        rc = 0;
    return rc;
}

// get response string from geo server
int
GeoConnect::ReceiveGeoRequest(int sock, string& resp)
{
    int rc = -1;
    char buffer[256];
    memset(buffer, 0, 256);

    int n = recv(sock, buffer, 255, 0);
    if (n < 0)
        cout << "ERROR reading from geo socket" << endl;
    else {
        buffer[n] = '\0';
        resp = string(buffer);
        rc = 0;
    }
    return rc;
}