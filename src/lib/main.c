/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include "daemon.h"
#include "version.h"
#include <clog/console.h>
#include <flood/in_stream.h>
#include <flood/out_stream.h>
#include <flood/text_in_stream.h>
#include <guise-client/client.h>
#include <guise-serialize/parse_text.h>
#include <imprint/default_setup.h>
#include <relay-server-lib/utils.h>
#include <time.h>
#include <udp-client/udp_client.h>

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

    return udpServerSend(self->serverSocket, buf, count, self->sockAddrIn);
}

typedef struct UdpClientSocketInfo {
    UdpClientSocket* clientSocket;
} UdpClientSocketInfo;

static int udpClientSocketInfoSend(void* _self, const uint8_t* data, size_t size)
{
    UdpClientSocketInfo* self = (UdpClientSocketInfo*) _self;

    return udpClientSend(self->clientSocket, data, size);
}

static ssize_t udpClientSocketInfoReceive(void* _self, uint8_t* data, size_t size)
{
    UdpClientSocketInfo* self = (UdpClientSocketInfo*) _self;

    return udpClientReceive(self->clientSocket, data, size);
}

typedef struct Secret {
    GuiseSerializeUserId userId;
    uint64_t passwordHash;
} Secret;

static int readSecret(Secret* secret)
{
    CLOG_DEBUG("reading secret file")
    FILE* fp = fopen("users.txt", "r");
    if (fp == 0) {
        CLOG_ERROR("could not find users.txt")
        //        return -4;
    }

    char line[1024];
    char* ptr = fgets(line, 1024, fp);
    if (ptr == 0) {
        return -39;
    }
    fclose(fp);

    FldTextInStream textInStream;
    FldInStream inStream;

    fldInStreamInit(&inStream, (const uint8_t*) line, tc_strlen(line));
    fldTextInStreamInit(&textInStream, &inStream);

    GuiseSerializeUserInfo userInfo;

    int err = guiseTextStreamReadUser(&textInStream, &userInfo);
    if (err < 0) {
        return err;
    }

    secret->userId = userInfo.userId;
    secret->passwordHash = userInfo.passwordHash;

    return 0;
}

int main(int argc, char* argv[])
{
    (void) argc;
    (void) argv;

    g_clog.log = clog_console;
    g_clog.level = CLOG_TYPE_VERBOSE;

    CLOG_OUTPUT("relay daemon v%s starting up", RELAY_DAEMON_VERSION)

    Secret secret;
    int secretErr = readSecret(&secret);
    if (secretErr < 0) {
        CLOG_SOFT_ERROR("could not read lines from secret.txt %d", secretErr)
        return secretErr;
    }

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

    ImprintDefaultSetup memory;
    imprintDefaultSetupInit(&memory, 16 * 1024 * 1024);

    // TODO:    ConclaveSerializeVersion applicationVersion = {0x10, 0x20, 0x30};

    Clog serverLog;
    serverLog.constantPrefix = "RelayServer";
    serverLog.config = &g_clog;

    UdpClientSocket udpClient;
    const static char* GUISE_SERVER_URL = "127.0.0.1";
    int udpClientErr = udpClientInit(&udpClient, GUISE_SERVER_URL, 27004);
    if (udpClientErr < 0) {
        return udpClientErr;
    }

    UdpClientSocketInfo guiseSocket;
    guiseSocket.clientSocket = &udpClient;

    DatagramTransport guiseTransport;
    guiseTransport.receive = udpClientSocketInfoReceive;
    guiseTransport.send = udpClientSocketInfoSend;
    guiseTransport.self = &guiseSocket;

    GuiseClient guiseClient;

    Clog guiseClientLog;
    guiseClientLog.config = &g_clog;
    guiseClientLog.constantPrefix = "GuiseClient";

    guiseClientInit(&guiseClient, &memory.tagAllocator.info, guiseClientLog);
    guiseClientReInit(&guiseClient, &guiseTransport, secret.userId, secret.passwordHash);
    GuiseClientState reportedState = GuiseClientStateIdle;

    uint8_t buf[DATAGRAM_TRANSPORT_MAX_SIZE];
    struct sockaddr_in address;
    int errorCode;

    uint8_t reply[DATAGRAM_TRANSPORT_MAX_SIZE];
    FldOutStream outStream;
    fldOutStreamInit(&outStream, reply, DATAGRAM_TRANSPORT_MAX_SIZE);

    CLOG_OUTPUT("ready for incoming UDP packets")
    bool hasCreatedRelayServer = false;

    while (true) {
        struct timespec ts;

        ts.tv_sec = 0;
        ts.tv_nsec = 16 * 1000000;
        nanosleep(&ts, &ts);

        if (!hasCreatedRelayServer) {

            MonotonicTimeMs now = monotonicTimeMsNow();
            guiseClientUpdate(&guiseClient, now);
        }

        if (reportedState != guiseClient.state) {
            reportedState = guiseClient.state;
            if (reportedState == GuiseClientStateLoggedIn && !hasCreatedRelayServer) {
                relayServerInit(&server, &memory.tagAllocator.info, guiseClient.mainUserSessionId,
                                guiseTransport, serverLog);
                CLOG_C_INFO(&server.log, "server authenticated")
                hasCreatedRelayServer = true;
                guiseClientDestroy(&guiseClient);
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
