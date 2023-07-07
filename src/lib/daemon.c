/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include "daemon.h"

int relayDaemonInit(RelayDaemon* self)
{
    int err = udpServerStartup();
    if (err < 0) {
        return err;
    }

    return udpServerInit(&self->socket, 27005, true);
}
