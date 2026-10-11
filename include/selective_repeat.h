#ifndef SELECTIVE_REPEAT_H
#define SELECTIVE_REPEAT_H

#include <stdio.h>
#include <winsock2.h>

#include "protocol.h"

#define SR_WINDOW_SIZE 4
#define SR_RTO_MS 1000

int selective_repeat_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
);

int selective_repeat_receive(
    SOCKET sock,
    FILE *file
);

#endif