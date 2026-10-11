#ifndef STOP_WAIT_H
#define STOP_WAIT_H

#include <stdio.h>
#include <winsock2.h>

#include "protocol.h"

/*
 * Stop-and-Wait sender
 */
int stop_wait_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
);

/*
 * Stop-and-Wait receiver
 */
int stop_wait_receive(
    SOCKET sock,
    FILE *file
);

#endif