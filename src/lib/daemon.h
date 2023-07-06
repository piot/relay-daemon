/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef RELAY_DAEMON_DAEMON_H
#define RELAY_DAEMON_DAEMON_H

#include <relay-server-lib/server.h>
#include <udp-server/udp_server.h>

typedef struct RelayDaemon {
    UdpServerSocket socket;
} RelayDaemon;

int relayDaemonInit(RelayDaemon* self);

#endif
