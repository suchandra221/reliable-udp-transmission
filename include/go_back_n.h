#ifndef GO_BACK_N_H
#define GO_BACK_N_H

#include <stdio.h>
#include <winsock2.h>

#include "protocol.h"

#define GBN_WINDOW_SIZE 4
#define GBN_RTO_MS 1000

int go_back_n_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
);

int go_back_n_receive(
    SOCKET sock,
    FILE *file
);

#endif