#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define DATA_SIZE 1024

#define TYPE_DATA 1
#define TYPE_ACK  2
#define TYPE_END  3

typedef struct
{
    uint32_t seq_no;
    uint32_t ack_no;

    uint16_t length;
    uint16_t type;

    uint32_t checksum;

    char data[DATA_SIZE];

} Packet;


/* Checksum functions */

uint32_t calculate_checksum(Packet *packet);

void set_checksum(Packet *packet);

int verify_checksum(Packet *packet);

#endif