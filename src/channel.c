#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include "../include/protocol.h"


#define CHANNEL_PORT 5001
#define RECEIVER_PORT 5000


/*
 * START WITH 0% FOR TESTING.
 *
 * After everything works, we will
 * change these for experiments.
 */

#define LOSS_RATE        0.0
#define DUPLICATE_RATE   0.0
#define CORRUPTION_RATE  0.0
#define REORDER_RATE     0.0

#define DELAY_MS         20
#define JITTER_MS        10

#define RANDOM_SEED      12345


struct sockaddr_in sender_addr;

int sender_known = 0;


Packet buffered_to_receiver;
int receiver_buffered = 0;


Packet buffered_to_sender;
int sender_buffered = 0;


/* ------------------------------------------------ */
/* RANDOM */
/* ------------------------------------------------ */

double random_probability()
{
    return (double)rand() /
           (double)RAND_MAX;
}


int random_jitter()
{
    if (JITTER_MS <= 0)
        return 0;

    return
        (rand() %
         (2 * JITTER_MS + 1))
        - JITTER_MS;
}


/* ------------------------------------------------ */
/* CORRUPTION */
/* ------------------------------------------------ */

void corrupt_packet(
    Packet *packet
)
{
    if (packet->length > 0)
    {
        packet->data[0] ^=
            0xFF;
    }
    else
    {
        packet->seq_no ^=
            0xFFFFFFFF;
    }
}


/* ------------------------------------------------ */
/* FORWARD PACKET */
/* ------------------------------------------------ */

int forward_packet(
    SOCKET sock,
    Packet *packet,
    struct sockaddr_in *destination
)
{
    Packet copy;

    memcpy(
        &copy,
        packet,
        sizeof(Packet)
    );


    /*
     * LOSS
     */

    if (random_probability() <
        LOSS_RATE)
    {
        printf(
            "[CHANNEL] Packet %u LOST\n",
            packet->seq_no
        );

        return 1;
    }


    /*
     * CORRUPTION
     */

    if (random_probability() <
        CORRUPTION_RATE)
    {
        printf(
            "[CHANNEL] Packet %u CORRUPTED\n",
            packet->seq_no
        );

        corrupt_packet(
            &copy
        );
    }


    /*
     * DELAY
     */

    int delay =
        DELAY_MS +
        random_jitter();

    if (delay < 0)
        delay = 0;

    if (delay > 0)
        Sleep(delay);


    /*
     * SEND
     */

    if (sendto(
            sock,
            (char *)&copy,
            sizeof(Packet),
            0,
            (struct sockaddr *)destination,
            sizeof(*destination)
        ) == SOCKET_ERROR)
    {
        printf(
            "[CHANNEL] sendto failed: %d\n",
            WSAGetLastError()
        );

        return 0;
    }


    if (copy.type ==
        TYPE_ACK)
    {
        printf(
            "[CHANNEL] ACK %u forwarded\n",
            copy.ack_no
        );
    }
    else
    {
        printf(
            "[CHANNEL] Packet %u forwarded\n",
            copy.seq_no
        );
    }


    /*
     * DUPLICATION
     */

    if (random_probability() <
        DUPLICATE_RATE)
    {
        printf(
            "[CHANNEL] Packet duplicated\n"
        );

        sendto(
            sock,
            (char *)&copy,
            sizeof(Packet),
            0,
            (struct sockaddr *)destination,
            sizeof(*destination)
        );
    }


    return 1;
}


/* ------------------------------------------------ */
/* SENDER -> RECEIVER */
/* ------------------------------------------------ */

void handle_sender_packet(
    SOCKET sock,
    Packet *packet,
    struct sockaddr_in *receiver
)
{
    /*
     * END must be sent after any buffered
     * DATA packet.
     */

    if (packet->type ==
        TYPE_END)
    {
        if (receiver_buffered)
        {
            printf(
                "[CHANNEL] Flushing buffered DATA %u\n",
                buffered_to_receiver.seq_no
            );

            forward_packet(
                sock,
                &buffered_to_receiver,
                receiver
            );

            receiver_buffered =
                0;
        }

        forward_packet(
            sock,
            packet,
            receiver
        );

        return;
    }


    /*
     * REORDERING
     */

    if (!receiver_buffered &&
        random_probability() <
        REORDER_RATE)
    {
        memcpy(
            &buffered_to_receiver,
            packet,
            sizeof(Packet)
        );

        receiver_buffered =
            1;

        printf(
            "[CHANNEL] Packet %u buffered for reordering\n",
            packet->seq_no
        );

        return;
    }


    if (receiver_buffered)
    {
        Packet old_packet;

        memcpy(
            &old_packet,
            &buffered_to_receiver,
            sizeof(Packet)
        );

        receiver_buffered =
            0;


        printf(
            "[CHANNEL] Reordering packets %u and %u\n",
            packet->seq_no,
            old_packet.seq_no
        );


        forward_packet(
            sock,
            packet,
            receiver
        );

        forward_packet(
            sock,
            &old_packet,
            receiver
        );

        return;
    }


    forward_packet(
        sock,
        packet,
        receiver
    );
}


/* ------------------------------------------------ */
/* RECEIVER -> SENDER */
/* ------------------------------------------------ */

void handle_receiver_packet(
    SOCKET sock,
    Packet *packet
)
{
    if (!sender_known)
    {
        printf(
            "[CHANNEL] Sender unknown\n"
        );

        return;
    }


    /*
     * ACK reordering.
     */

    if (!sender_buffered &&
        random_probability() <
        REORDER_RATE)
    {
        memcpy(
            &buffered_to_sender,
            packet,
            sizeof(Packet)
        );

        sender_buffered =
            1;

        printf(
            "[CHANNEL] ACK %u buffered\n",
            packet->ack_no
        );

        return;
    }


    if (sender_buffered)
    {
        Packet old_ack;

        memcpy(
            &old_ack,
            &buffered_to_sender,
            sizeof(Packet)
        );

        sender_buffered =
            0;


        forward_packet(
            sock,
            packet,
            &sender_addr
        );

        forward_packet(
            sock,
            &old_ack,
            &sender_addr
        );

        return;
    }


    forward_packet(
        sock,
        packet,
        &sender_addr
    );
}


/* ------------------------------------------------ */
/* MAIN */
/* ------------------------------------------------ */

int main()
{
    WSADATA wsa;

    SOCKET sock;

    struct sockaddr_in channel_addr;
    struct sockaddr_in receiver_addr;
    struct sockaddr_in source_addr;

    int source_len;

    Packet packet;


    srand(
        RANDOM_SEED
    );


    if (WSAStartup(
            MAKEWORD(2, 2),
            &wsa
        ) != 0)
    {
        printf(
            "WSAStartup failed\n"
        );

        return 1;
    }


    sock =
        socket(
            AF_INET,
            SOCK_DGRAM,
            0
        );


    if (sock ==
        INVALID_SOCKET)
    {
        printf(
            "Socket creation failed: %d\n",
            WSAGetLastError()
        );

        WSACleanup();

        return 1;
    }


    /*
     * CHANNEL ADDRESS
     */

    memset(
        &channel_addr,
        0,
        sizeof(channel_addr)
    );

    channel_addr.sin_family =
        AF_INET;

    channel_addr.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    channel_addr.sin_port =
        htons(CHANNEL_PORT);


    if (bind(
            sock,
            (struct sockaddr *)&channel_addr,
            sizeof(channel_addr)
        ) == SOCKET_ERROR)
    {
        printf(
            "Channel bind failed: %d\n",
            WSAGetLastError()
        );

        closesocket(sock);
        WSACleanup();

        return 1;
    }


    /*
     * RECEIVER ADDRESS
     */

    memset(
        &receiver_addr,
        0,
        sizeof(receiver_addr)
    );

    receiver_addr.sin_family =
        AF_INET;

    receiver_addr.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    receiver_addr.sin_port =
        htons(RECEIVER_PORT);


    printf(
        "=====================================\n"
    );

    printf(
        "             UDP CHANNEL\n"
    );

    printf(
        "=====================================\n"
    );

    printf(
        "Channel port    : %d\n",
        CHANNEL_PORT
    );

    printf(
        "Receiver port   : %d\n",
        RECEIVER_PORT
    );

    printf(
        "-------------------------------------\n"
    );

    printf(
        "Loss rate       : %.0f%%\n",
        LOSS_RATE * 100
    );

    printf(
        "Duplicate rate  : %.0f%%\n",
        DUPLICATE_RATE * 100
    );

    printf(
        "Corruption rate : %.0f%%\n",
        CORRUPTION_RATE * 100
    );

    printf(
        "Reorder rate    : %.0f%%\n",
        REORDER_RATE * 100
    );

    printf(
        "Delay           : %d ms\n",
        DELAY_MS
    );

    printf(
        "Jitter          : %d ms\n",
        JITTER_MS
    );

    printf(
        "-------------------------------------\n"
    );

    printf(
        "Channel waiting for packets...\n"
    );


    while (1)
    {
        source_len =
            sizeof(source_addr);


        memset(
            &packet,
            0,
            sizeof(Packet)
        );


        int received =
            recvfrom(
                sock,
                (char *)&packet,
                sizeof(Packet),
                0,
                (struct sockaddr *)&source_addr,
                &source_len
            );


        if (received ==
            SOCKET_ERROR)
        {
            printf(
                "recvfrom failed: %d\n",
                WSAGetLastError()
            );

            break;
        }


        /*
         * Receiver uses port 5000.
         */

        if (ntohs(
                source_addr.sin_port)
            ==
            RECEIVER_PORT)
        {
            printf(
                "[CHANNEL] ACK %u received from RECEIVER\n",
                packet.ack_no
            );

            handle_receiver_packet(
                sock,
                &packet
            );

            continue;
        }


        /*
         * Packet came from sender.
         */

        memcpy(
            &sender_addr,
            &source_addr,
            sizeof(source_addr)
        );

        sender_known =
            1;


        printf(
            "[CHANNEL] Packet %u received from SENDER\n",
            packet.seq_no
        );


        handle_sender_packet(
            sock,
            &packet,
            &receiver_addr
        );
    }


    closesocket(sock);

    WSACleanup();

    return 0;
}