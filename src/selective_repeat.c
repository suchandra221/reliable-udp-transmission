#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <winsock2.h>

#include "../include/protocol.h"
#include "../include/selective_repeat.h"


/*
 * Send one packet.
 */
static int send_packet(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    Packet *packet
)
{
    int sent;

    sent = sendto(
        sock,
        (char *)packet,
        sizeof(Packet),
        0,
        (struct sockaddr *)server_addr,
        sizeof(*server_addr)
    );

    if (sent == SOCKET_ERROR)
    {
        printf(
            "sendto failed for packet %u: %d\n",
            packet->seq_no,
            WSAGetLastError()
        );

        return 0;
    }

    return 1;
}


/*
 * ============================================================
 * SELECTIVE REPEAT SENDER
 * ============================================================
 */
int selective_repeat_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
)
{
    Packet window[SR_WINDOW_SIZE];

    int acked[SR_WINDOW_SIZE];
    DWORD send_time[SR_WINDOW_SIZE];

    uint32_t base = 0;
    uint32_t next_seq = 0;

    int eof = 0;


    /*
     * Initialize window state.
     */
    memset(
        window,
        0,
        sizeof(window)
    );

    memset(
        acked,
        0,
        sizeof(acked)
    );

    memset(
        send_time,
        0,
        sizeof(send_time)
    );


    /*
     * --------------------------------------------------------
     * Continue until EOF and all packets are ACKed.
     * --------------------------------------------------------
     */
    while (!eof || base < next_seq)
    {
        /*
         * ----------------------------------------------------
         * Fill available window positions.
         * ----------------------------------------------------
         */
        while (!eof &&
               next_seq < base + SR_WINDOW_SIZE)
        {
            int index;
            size_t bytes_read;

            index =
                (int)(next_seq % SR_WINDOW_SIZE);


            memset(
                &window[index],
                0,
                sizeof(Packet)
            );

            bytes_read = fread(
                window[index].data,
                1,
                DATA_SIZE,
                file
            );


            if (bytes_read == 0)
            {
                eof = 1;
                break;
            }


            window[index].seq_no = next_seq;
            window[index].ack_no = 0;
            window[index].length =
                (uint16_t)bytes_read;
            window[index].type = TYPE_DATA;

            set_checksum(
                &window[index]
            );


            acked[index] = 0;
            send_time[index] = 0;


            printf(
                "Prepared DATA packet %u (%zu bytes)\n",
                next_seq,
                bytes_read
            );


            next_seq++;
        }


        /*
         * ----------------------------------------------------
         * Send packets that have not been ACKed.
         * ----------------------------------------------------
         */
        {
            uint32_t seq;

            for (seq = base; seq < next_seq; seq++)
            {
                int index;

                index =
                    (int)(seq % SR_WINDOW_SIZE);


                if (!acked[index] &&
                    send_time[index] == 0)
                {
                    if (!send_packet(
                            sock,
                            server_addr,
                            &window[index]))
                    {
                        return 0;
                    }


                    printf(
                        "Sent DATA packet %u\n",
                        seq
                    );


                    send_time[index] =
                        GetTickCount();
                }
            }
        }


        /*
         * ----------------------------------------------------
         * Wait until the current window advances.
         * ----------------------------------------------------
         */
        while (base < next_seq)
        {
            Packet ack_packet;

            DWORD timeout = 100;

            int received;


            /*
             * Short receive timeout.
             *
             * This allows us to check each packet's
             * individual timer.
             */
            setsockopt(
                sock,
                SOL_SOCKET,
                SO_RCVTIMEO,
                (const char *)&timeout,
                sizeof(timeout)
            );


            memset(
                &ack_packet,
                0,
                sizeof(ack_packet)
            );


            received = recvfrom(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                NULL,
                NULL
            );


            /*
             * ------------------------------------------------
             * ACK received
             * ------------------------------------------------
             */
            if (received != SOCKET_ERROR)
            {
                if (!verify_checksum(&ack_packet))
                {
                    printf(
                        "Corrupted ACK received - ignoring\n"
                    );
                }
                else if (ack_packet.type != TYPE_ACK)
                {
                    printf(
                        "Unexpected packet received - ignoring\n"
                    );
                }
                else
                {
                    uint32_t ack_no =
                        ack_packet.ack_no;


                    /*
                     * Is ACK inside current window?
                     */
                    if (ack_no >= base &&
                        ack_no < next_seq)
                    {
                        int index =
                            (int)(
                                ack_no %
                                SR_WINDOW_SIZE
                            );


                        if (!acked[index])
                        {
                            acked[index] = 1;

                            printf(
                                "Selective ACK %u received\n",
                                ack_no
                            );
                        }
                        else
                        {
                            printf(
                                "Duplicate ACK %u received\n",
                                ack_no
                            );
                        }


                        /*
                         * Slide window over consecutive
                         * ACKed packets.
                         */
                        while (base < next_seq)
                        {
                            int base_index =
                                (int)(
                                    base %
                                    SR_WINDOW_SIZE
                                );


                            if (!acked[base_index])
                            {
                                break;
                            }


                            acked[base_index] = 0;
                            send_time[base_index] = 0;

                            base++;
                        }
                    }
                    else
                    {
                        printf(
                            "Old/out-of-window ACK %u ignored\n",
                            ack_no
                        );
                    }
                }
            }
            else
            {
                int error =
                    WSAGetLastError();


                if (error != WSAETIMEDOUT)
                {
                    printf(
                        "recvfrom failed: %d\n",
                        error
                    );

                    return 0;
                }
            }


            /*
             * ------------------------------------------------
             * Check individual packet timers.
             *
             * This is the major difference from GBN.
             *
             * Only packets whose timers expire are
             * retransmitted.
             * ------------------------------------------------
             */
            {
                uint32_t seq;

                DWORD now =
                    GetTickCount();


                for (seq = base;
                     seq < next_seq;
                     seq++)
                {
                    int index =
                        (int)(
                            seq %
                            SR_WINDOW_SIZE
                        );


                    if (!acked[index] &&
                        send_time[index] != 0)
                    {
                        DWORD elapsed =
                            now -
                            send_time[index];


                        if (elapsed >= SR_RTO_MS)
                        {
                            printf(
                                "TIMEOUT packet %u - "
                                "retransmitting only this packet\n",
                                seq
                            );


                            if (!send_packet(
                                    sock,
                                    server_addr,
                                    &window[index]))
                            {
                                return 0;
                            }


                            send_time[index] =
                                GetTickCount();
                        }
                    }
                }
            }
        }
    }


    /*
     * ========================================================
     * SEND END PACKET RELIABLY
     * ========================================================
     */
    {
        Packet end_packet;
        Packet ack_packet;


        memset(
            &end_packet,
            0,
            sizeof(end_packet)
        );


        end_packet.type = TYPE_END;
        end_packet.seq_no = next_seq;
        end_packet.ack_no = 0;
        end_packet.length = 0;


        set_checksum(
            &end_packet
        );


        printf("-------------------------------------\n");
        printf(
            "Sending END packet %u...\n",
            next_seq
        );


        while (1)
        {
            int received;
            DWORD timeout;


            if (!send_packet(
                    sock,
                    server_addr,
                    &end_packet))
            {
                return 0;
            }


            printf(
                "Sent END packet %u\n",
                next_seq
            );


            timeout = SR_RTO_MS;


            setsockopt(
                sock,
                SOL_SOCKET,
                SO_RCVTIMEO,
                (const char *)&timeout,
                sizeof(timeout)
            );


            memset(
                &ack_packet,
                0,
                sizeof(ack_packet)
            );


            received = recvfrom(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                NULL,
                NULL
            );


            if (received == SOCKET_ERROR)
            {
                if (WSAGetLastError() ==
                    WSAETIMEDOUT)
                {
                    printf(
                        "END timeout - "
                        "retransmitting...\n"
                    );

                    continue;
                }


                printf(
                    "recvfrom failed: %d\n",
                    WSAGetLastError()
                );

                return 0;
            }


            if (!verify_checksum(&ack_packet))
            {
                printf(
                    "Invalid END ACK - ignoring\n"
                );

                continue;
            }


            if (ack_packet.type == TYPE_ACK &&
                ack_packet.ack_no == next_seq)
            {
                printf(
                    "END ACK %u received\n",
                    next_seq
                );

                break;
            }
        }
    }


    return 1;
}


/*
 * ============================================================
 * SELECTIVE REPEAT RECEIVER
 * ============================================================
 */
int selective_repeat_receive(
    SOCKET sock,
    FILE *file
)
{
    Packet buffer[SR_WINDOW_SIZE];

    int received_packet[SR_WINDOW_SIZE];

    uint32_t base = 0;


    struct sockaddr_in client_addr;


    memset(
        buffer,
        0,
        sizeof(buffer)
    );

    memset(
        received_packet,
        0,
        sizeof(received_packet)
    );


    /*
     * --------------------------------------------------------
     * Receive packets continuously.
     * --------------------------------------------------------
     */
    while (1)
    {
        Packet packet;
        Packet ack_packet;

        int client_len;
        int bytes_received;


        memset(
            &packet,
            0,
            sizeof(packet)
        );


        client_len =
            sizeof(client_addr);


        bytes_received = recvfrom(
            sock,
            (char *)&packet,
            sizeof(packet),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );


        if (bytes_received == SOCKET_ERROR)
        {
            printf(
                "recvfrom failed: %d\n",
                WSAGetLastError()
            );

            return 0;
        }


        /*
         * ----------------------------------------------------
         * Check checksum.
         * ----------------------------------------------------
         */
        if (!verify_checksum(&packet))
        {
            printf(
                "Corrupted packet %u - discarded\n",
                packet.seq_no
            );

            continue;
        }


        /*
         * ----------------------------------------------------
         * DATA packet
         * ----------------------------------------------------
         */
        if (packet.type == TYPE_DATA)
        {
            uint32_t seq =
                packet.seq_no;


            printf(
                "Received DATA packet %u (%u bytes)\n",
                seq,
                packet.length
            );


            /*
             * ------------------------------------------------
             * Packet is inside receiver window.
             * ------------------------------------------------
             */
            if (seq >= base &&
                seq < base + SR_WINDOW_SIZE)
            {
                int index =
                    (int)(
                        seq %
                        SR_WINDOW_SIZE
                    );


                /*
                 * Store packet if it hasn't already
                 * been received.
                 */
                if (!received_packet[index])
                {
                    memcpy(
                        &buffer[index],
                        &packet,
                        sizeof(Packet)
                    );


                    received_packet[index] = 1;


                    printf(
                        "Packet %u buffered\n",
                        seq
                    );
                }
                else
                {
                    printf(
                        "Duplicate packet %u\n",
                        seq
                    );
                }


                /*
                 * ------------------------------------------------
                 * Immediately ACK this specific packet.
                 *
                 * This is SELECTIVE ACK.
                 * ------------------------------------------------
                 */
                memset(
                    &ack_packet,
                    0,
                    sizeof(ack_packet)
                );


                ack_packet.type = TYPE_ACK;
                ack_packet.ack_no = seq;
                ack_packet.seq_no = 0;
                ack_packet.length = 0;


                set_checksum(
                    &ack_packet
                );


                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
                );


                printf(
                    "Selective ACK %u sent\n",
                    seq
                );


                /*
                 * ------------------------------------------------
                 * Deliver all consecutive packets starting
                 * from base.
                 * ------------------------------------------------
                 */
                while (1)
                {
                    int base_index =
                        (int)(
                            base %
                            SR_WINDOW_SIZE
                        );


                    if (!received_packet[base_index])
                    {
                        break;
                    }


                    fwrite(
                        buffer[base_index].data,
                        1,
                        buffer[base_index].length,
                        file
                    );


                    printf(
                        "Packet %u delivered to file\n",
                        base
                    );


                    memset(
                        &buffer[base_index],
                        0,
                        sizeof(Packet)
                    );


                    received_packet[base_index] = 0;


                    base++;
                }
            }


            /*
             * ------------------------------------------------
             * Packet is older than current window.
             *
             * It is a duplicate, so ACK it again.
             * ------------------------------------------------
             */
            else if (seq < base)
            {
                printf(
                    "Old duplicate packet %u - "
                    "sending ACK again\n",
                    seq
                );


                memset(
                    &ack_packet,
                    0,
                    sizeof(ack_packet)
                );


                ack_packet.type = TYPE_ACK;
                ack_packet.ack_no = seq;
                ack_packet.seq_no = 0;
                ack_packet.length = 0;


                set_checksum(
                    &ack_packet
                );


                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
                );
            }


            /*
             * ------------------------------------------------
             * Packet is beyond receiver window.
             * ------------------------------------------------
             */
            else
            {
                printf(
                    "Packet %u is outside receiver "
                    "window - discarded\n",
                    seq
                );
            }
        }


        /*
         * ----------------------------------------------------
         * END packet
         * ----------------------------------------------------
         */
        else if (packet.type == TYPE_END)
        {
            printf("-------------------------------------\n");


            /*
             * ACK END packet.
             */
            memset(
                &ack_packet,
                0,
                sizeof(ack_packet)
            );


            ack_packet.type = TYPE_ACK;
            ack_packet.ack_no = packet.seq_no;
            ack_packet.seq_no = 0;
            ack_packet.length = 0;


            set_checksum(
                &ack_packet
            );


            sendto(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                (struct sockaddr *)&client_addr,
                client_len
            );


            printf(
                "END packet %u received\n",
                packet.seq_no
            );


            printf(
                "END ACK %u sent\n",
                packet.seq_no
            );


            printf(
                "File transfer completed\n"
            );


            break;
        }
    }


    return 1;
}