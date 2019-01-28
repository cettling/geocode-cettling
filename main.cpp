/** main.cpp
*/

#include <iostream>
#include <string>
#ifdef WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "GeoConnect.h"
#include "ClientConnect.h"

using namespace std;

#define USEC_WAIT   100000


class GeoConfig 
{
    /* class holds program configuration */

public:
    // properties
    bool running;

    GeoConfig(int argc, char* argv[]) {
        // expected cmd line: geocode -c cfg_file
        char cfg_file[256];
        memset(cfg_file, 0, sizeof(cfg_file));
        int i, ch = 0;
        for (i=1; i < argc; i++) {
            if (0 == strncmp(argv[i], "-c", 2)) {
                if (strlen(argv[i]) > 2) {
                    strncpy(cfg_file, &argv[i][2], sizeof(cfg_file)-1);
                }
                else {
                    ch = 'c';
                }
            }
            else if ('c' == ch) {
                strncpy(cfg_file, argv[i], sizeof(cfg_file)-1);
                ch = 0;
            }
            else {
                cout << "Unknown arg: " << argv[i] << endl;
            }
        }
        GeoConnect::ReadConfig(cfg_file);

      /*  while (-1 != (ch = getopt(argc, argv, "c:"))) {
            switch (ch) {
            case 'c':
                // configuration file in optarg
                GeoConnect::ReadConfig(optarg);
                break;
            default:
                cout << "unknown arg " << optopt << endl;
                break;
            }
        } */

        running = true;
    }

   // ~GeoConfig() {}
};

int main(int argc, char* argv[])
{
    GeoConfig cfg(argc, argv);
    ClientConnect clConn(HTTP_PORT);
    const int numFds = MAX_CONNECTIONS + 1;  // num geos + listener
    while (cfg.running) {
        int i, loaded = 1;
        fd_set fds[numFds];
        FD_ZERO(&fds[0]);
        FD_SET(clConn.GetListenerSocket(), &fds[0]);
        for (i=1; i < numFds; i++) {
            FD_ZERO(&fds[i]);
            int sock = ReadQueueSocket(i-1);
            if (sock >= 0) {
                FD_SET(sock, &fds[i]);
                loaded++;
            }
        }
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = USEC_WAIT;
        int res = select(loaded, fds, NULL, NULL, &tv);
        if (res > 0) {
            if (FD_ISSET(fds[0].fd_array[0], &fds[0])) {
                int rc = clConn.GetClientRequest();
            }
            for (i=1; i < loaded; i++) {
                if (FD_ISSET(fds[i].fd_array[0], &fds[i])) {
                    GeoConnect::ProcessRequest(fds[i].fd_array[0]);
                }
            }
        }
    }

    return 0;
}

