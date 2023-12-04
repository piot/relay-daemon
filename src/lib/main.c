/*----------------------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved. https://github.com/piot/relay-daemon
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------------------*/
#include "daemon.h"
#include "version.h"
#include <clog/console.h>
#include <flood/in_stream.h>
#include <flood/out_stream.h>
#include <flood/text_in_stream.h>
#include <guise-client-udp/client.h>
#include <guise-client-udp/read_secret.h>
#include <guise-client/client.h>
#include <guise-serialize/parse_text.h>
#include <imprint/default_setup.h>
#include <relay-server-lib/utils.h>
#include <time.h>

#if !defined TORNADO_OS_WINDOWS
#include <errno.h>
#include <unistd.h>
#endif

clog_config g_clog;
char g_clog_temp_str[CLOG_TEMP_STR_SIZE];

typedef struct UdpServerSocketSendToAddress {
    struct sockaddr_in* sockAddrIn;
    UdpServerSocket* serverSocket;
} UdpServerSocketSendToAddress;

static int sendToAddress(void* self_, const RelayAddress* address, const uint8_t* buf, size_t count)
{
    (void) address;
    UdpServerSocketSendToAddress* self = (UdpServerSocketSendToAddress*) self_;

    return udpServerSend(self->serverSocket, buf, count, address);
}

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;

    g_clog.log = clog_console;
    g_clog.level = CLOG_TYPE_VERBOSE;

    CLOG_OUTPUT("relay daemon v%s starting up", RELAY_DAEMON_VERSION)

    ImprintDefaultSetup memory;
    imprintDefaultSetupInit(&memory, 16 * 1024 * 1024);

    GuiseClientUdp guiseClient;

    Clog guiseClientLog;
    guiseClientLog.config = &g_clog;
    guiseClientLog.constantPrefix = "GuiseClient";

    const char* guiseHost = "127.0.0.1";
    uint16_t guisePort = 27004;

    GuiseClientUdpSecret secret;
    guiseClientUdpReadSecret(&secret, 0);

    guiseClientUdpInit(&guiseClient, &memory.tagAllocator.info, guiseHost, guisePort, &secret);

    RelayDaemon daemon;

    int err = relayDaemonInit(&daemon);
    if (err < 0) {
        return err;
    }

    UdpServerSocketSendToAddress socketSendToAddress;
    socketSendToAddress.serverSocket = &daemon.socket;

    RelayServerSendDatagram sendDatagram;
    sendDatagram.send = sendToAddress;
    sendDatagram.self = &socketSendToAddress;

    RelayServerResponse response;
    response.sendDatagram = sendDatagram;

    RelayServer server;

    // TODO:    ConclaveSerializeVersion applicationVersion = {0x10, 0x20, 0x30};

    Clog serverLog;
    serverLog.constantPrefix = "RelayServer";
    serverLog.config = &g_clog;

    uint8_t buf[DATAGRAM_TRANSPORT_MAX_SIZE];
    struct sockaddr_in address;
    int errorCode;

    uint8_t reply[DATAGRAM_TRANSPORT_MAX_SIZE];
    FldOutStream outStream;
    fldOutStreamInit(&outStream, reply, DATAGRAM_TRANSPORT_MAX_SIZE);

    CLOG_OUTPUT("ready for incoming UDP packets")
    bool hasCreatedRelayServer = false;
    GuiseClientState reportedState = GuiseClientStateIdle;
    while (true) {
        struct timespec ts;

        ts.tv_sec = 0;
        ts.tv_nsec = 16 * 1000000;
        nanosleep(&ts, &ts);

        if (!hasCreatedRelayServer) {

            MonotonicTimeMs now = monotonicTimeMsNow();
            guiseClientUdpUpdate(&guiseClient, now);
        }

        if (reportedState != guiseClient.guiseClient.state) {
            reportedState = guiseClient.guiseClient.state;
            if (reportedState == GuiseClientStateLoggedIn && !hasCreatedRelayServer) {
                relayServerInit(&server, &memory.tagAllocator.info, guiseClient.guiseClient.mainUserSessionId,
                                guiseClient.transport, serverLog);
                CLOG_C_INFO(&server.log, "server authenticated")
                hasCreatedRelayServer = true;
                // guiseClientUdpDestroy(&guiseClient);
            }
        }
        if (!hasCreatedRelayServer) {
            continue;
        }

        relayServerUpdate(&server);

        ssize_t receivedOctetCount = udpServerReceive(&daemon.socket, buf, DATAGRAM_TRANSPORT_MAX_SIZE,
                                                      &address);
        if (receivedOctetCount < 0) {
            CLOG_WARN("problem with receive %zd", receivedOctetCount)
        } else {
            socketSendToAddress.sockAddrIn = &address;

            fldOutStreamRewind(&outStream);
#if 0
            nimbleSerializeDebugHex("received", buf, size);
#endif
            errorCode = relayServerFeed(&server, &address, buf, (size_t) receivedOctetCount, &response);
            if (errorCode < 0) {
                CLOG_WARN("relayServerFeed: error %d", errorCode)
            }
        }
    }

    // imprintDefaultSetupDestroy(&memory);
    // return 0;
}
