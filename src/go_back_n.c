#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <winsock2.h>

#include "../include/protocol.h"
#include "../include/go_back_n.h"


/*
 * ============================================================
 * GO-BACK-N SENDER
 * ============================================================
 */
int go_back_n_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
)
{
    Packet window[GBN_WINDOW_SIZE];

    uint32_t base = 0;
    uint32_t next_seq = 0;

    int window_count = 0;
    int eof = 0;

    /*
     * Continue until all data has been
     * acknowledged.
     */
    while (!eof || base < next_seq)
    {
        /*
         * ----------------------------------------------------
         * Fill the current sending window
         * ----------------------------------------------------
         */
        while (!eof &&
               next_seq < base + GBN_WINDOW_SIZE)
        {
            Packet *packet;

            /*
             * Position inside window.
             */
            int index =
                (int)(next_seq % GBN_WINDOW_SIZE);

            packet = &window[index];

            memset(
                packet,
                0,
                sizeof(Packet)
            );

            /*
             * Read data from file.
             */
            size_t bytes_read = fread(
                packet->data,
                1,
                DATA_SIZE,
                file
            );

            if (bytes_read == 0)
            {
                eof = 1;
                break;
            }

            /*
             * Fill packet information.
             */
            packet->seq_no = next_seq;
            packet->ack_no = 0;
            packet->length = (uint16_t)bytes_read;
            packet->type = TYPE_DATA;

            /*
             * Calculate checksum.
             */
            set_checksum(packet);

            printf(
                "Prepared DATA packet %u (%zu bytes)\n",
                packet->seq_no,
                bytes_read
            );

            next_seq++;
            window_count++;
        }


        /*
         * ----------------------------------------------------
         * Send all unacknowledged packets
         *
         * This is the main Go-Back-N behavior.
         * ----------------------------------------------------
         */
        {
            uint32_t seq;

            for (seq = base; seq < next_seq; seq++)
            {
                int index =
                    (int)(seq % GBN_WINDOW_SIZE);

                Packet *packet = &window[index];

                int sent = sendto(
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
                        seq,
                        WSAGetLastError()
                    );

                    return 0;
                }

                printf(
                    "Sent DATA packet %u\n",
                    seq
                );
            }
        }


        /*
         * ----------------------------------------------------
         * If there are no outstanding packets, continue
         * ----------------------------------------------------
         */
        if (base == next_seq)
        {
            continue;
        }


        /*
         * ----------------------------------------------------
         * Wait for ACK
         * ----------------------------------------------------
         */
        {
            Packet ack_packet;

            DWORD timeout = GBN_RTO_MS;

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

            int received = recvfrom(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                NULL,
                NULL
            );


            /*
             * ------------------------------------------------
             * Timeout
             *
             * Go-Back-N retransmits the entire
             * unacknowledged window.
             * ------------------------------------------------
             */
            if (received == SOCKET_ERROR)
            {
                int error = WSAGetLastError();

                if (error == WSAETIMEDOUT)
                {
                    printf(
                        "TIMEOUT - Go-Back-N retransmitting "
                        "from packet %u\n",
                        base
                    );

                    continue;
                }

                printf(
                    "recvfrom failed: %d\n",
                    error
                );

                return 0;
            }


            /*
             * ------------------------------------------------
             * Verify ACK checksum
             * ------------------------------------------------
             */
            if (!verify_checksum(&ack_packet))
            {
                printf(
                    "Corrupted ACK received - ignoring\n"
                );

                continue;
            }


            /*
             * ------------------------------------------------
             * Check ACK type
             * ------------------------------------------------
             */
            if (ack_packet.type != TYPE_ACK)
            {
                printf(
                    "Unexpected packet received - ignoring\n"
                );

                continue;
            }


            /*
             * ------------------------------------------------
             * Cumulative ACK
             * ------------------------------------------------
             */
            if (ack_packet.ack_no >= base)
            {
                printf(
                    "Cumulative ACK %u received\n",
                    ack_packet.ack_no
                );

                /*
                 * ACK N means packets through N
                 * have been successfully received.
                 */
                base = ack_packet.ack_no + 1;
            }
            else
            {
                /*
                 * Duplicate ACK.
                 */
                printf(
                    "Duplicate ACK %u received\n",
                    ack_packet.ack_no
                );
            }
        }
    }


    /*
     * ========================================================
     * SEND END PACKET
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

        set_checksum(&end_packet);

        printf("-------------------------------------\n");
        printf(
            "Sending END packet %u...\n",
            next_seq
        );


        /*
         * END must also be acknowledged.
         */
        while (1)
        {
            int sent;
            int received;

            sent = sendto(
                sock,
                (char *)&end_packet,
                sizeof(end_packet),
                0,
                (struct sockaddr *)server_addr,
                sizeof(*server_addr)
            );

            if (sent == SOCKET_ERROR)
            {
                printf(
                    "Failed to send END packet: %d\n",
                    WSAGetLastError()
                );

                return 0;
            }

            printf(
                "Sent END packet %u\n",
                next_seq
            );


            /*
             * Timeout.
             */
            {
                DWORD timeout = GBN_RTO_MS;

                setsockopt(
                    sock,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    (const char *)&timeout,
                    sizeof(timeout)
                );
            }


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
                if (WSAGetLastError() == WSAETIMEDOUT)
                {
                    printf(
                        "END timeout - retransmitting...\n"
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
 * GO-BACK-N RECEIVER
 * ============================================================
 */
int go_back_n_receive(
    SOCKET sock,
    FILE *file
)
{
    Packet packet;
    Packet ack_packet;

    struct sockaddr_in client_addr;
    int client_len;

    uint32_t expected_seq = 0;


    while (1)
    {
        int bytes_received;

        memset(
            &packet,
            0,
            sizeof(packet)
        );

        client_len = sizeof(client_addr);


        /*
         * ----------------------------------------------------
         * Receive packet
         * ----------------------------------------------------
         */
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
         * Check checksum
         * ----------------------------------------------------
         */
        if (!verify_checksum(&packet))
        {
            printf(
                "Corrupted packet %u - discarded\n",
                packet.seq_no
            );

            /*
             * Do not ACK corrupted packet.
             *
             * Sender will eventually timeout
             * and retransmit the window.
             */
            continue;
        }


        /*
         * ----------------------------------------------------
         * DATA packet
         * ----------------------------------------------------
         */
        if (packet.type == TYPE_DATA)
        {
            printf(
                "Received DATA packet %u (%u bytes)\n",
                packet.seq_no,
                packet.length
            );


            /*
             * Correct packet.
             */
            if (packet.seq_no == expected_seq)
            {
                fwrite(
                    packet.data,
                    1,
                    packet.length,
                    file
                );

                printf(
                    "Packet %u accepted\n",
                    packet.seq_no
                );


                /*
                 * Send cumulative ACK.
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

                set_checksum(&ack_packet);


                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
                );


                printf(
                    "Cumulative ACK %u sent\n",
                    packet.seq_no
                );


                expected_seq++;
            }


            /*
             * ------------------------------------------------
             * Out-of-order packet
             * ------------------------------------------------
             */
            else if (packet.seq_no > expected_seq)
            {
                printf(
                    "Out-of-order packet %u "
                    "(expected %u) - discarded\n",
                    packet.seq_no,
                    expected_seq
                );


                /*
                 * Send duplicate ACK for the last
                 * correctly received packet.
                 *
                 * For expected_seq == 0, ACK 0 is used
                 * as the initial duplicate ACK.
                 */
                memset(
                    &ack_packet,
                    0,
                    sizeof(ack_packet)
                );

                ack_packet.type = TYPE_ACK;

                if (expected_seq == 0)
                {
                    ack_packet.ack_no = 0;
                }
                else
                {
                    ack_packet.ack_no =
                        expected_seq - 1;
                }

                ack_packet.seq_no = 0;
                ack_packet.length = 0;

                set_checksum(&ack_packet);


                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
                );


                printf(
                    "Duplicate ACK %u sent\n",
                    ack_packet.ack_no
                );
            }


            /*
             * ------------------------------------------------
             * Duplicate/old packet
             * ------------------------------------------------
             */
            else
            {
                printf(
                    "Duplicate packet %u received\n",
                    packet.seq_no
                );


                /*
                 * Re-send ACK.
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

                set_checksum(&ack_packet);


                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
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
            printf(
                "END packet %u received\n",
                packet.seq_no
            );


            /*
             * ACK END.
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

            set_checksum(&ack_packet);


            sendto(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                (struct sockaddr *)&client_addr,
                client_len
            );


            printf(
                "END ACK %u sent\n",
                packet.seq_no
            );

            break;
        }
    }


    return 1;
}