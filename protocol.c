#include <stdint.h>
#include <stddef.h>

#include "include/protocol.h"

uint32_t calculate_checksum(Packet *packet)
{
    uint32_t checksum = 0;

    uint32_t original_checksum =
        packet->checksum;

    packet->checksum = 0;

    unsigned char *data =
        (unsigned char *)packet;

    for (size_t i = 0;
         i < sizeof(Packet);
         i++)
    {
        checksum += data[i];
    }

    packet->checksum =
        original_checksum;

    return checksum;
}


void set_checksum(Packet *packet)
{
    packet->checksum = 0;

    packet->checksum =
        calculate_checksum(packet);
}


int verify_checksum(Packet *packet)
{
    uint32_t received_checksum;
    uint32_t calculated_checksum;

    received_checksum =
        packet->checksum;

    calculated_checksum =
        calculate_checksum(packet);

    return received_checksum ==
           calculated_checksum;
}