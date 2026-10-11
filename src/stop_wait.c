#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>

#include "../include/protocol.h"
#include "../include/stop_wait.h"

#define RTO_MS 1000


/*
 * ============================================================
 * STOP-AND-WAIT SENDER
 * ============================================================
 */
int stop_wait_send(
    SOCKET sock,
    struct sockaddr_in *server_addr,
    FILE *file
)
{
    Packet packet;
    Packet ack_packet;

    uint32_t seq_no = 0;
    int bytes_read;

    /*
     * Read one packet at a time.
     */
    while (1)
    {
        memset(&packet, 0, sizeof(packet));

        bytes_read = fread(
            packet.data,
            1,
            DATA_SIZE,
            file
        );

        /*
         * End of file.
         */
        if (bytes_read <= 0)
        {
            break;
        }

        /*
         * Fill packet.
         */
        packet.seq_no = seq_no;
        packet.ack_no = 0;
        packet.length = (uint16_t)bytes_read;
        packet.type = TYPE_DATA;

        /*
         * Calculate checksum.
         */
        set_checksum(&packet);

        /*
         * Keep sending until the correct
         * ACK is received.
         */
        while (1)
        {
            int sent;
            int received;

            /*
             * Send DATA packet.
             */
            sent = sendto(
                sock,
                (char *)&packet,
                sizeof(packet),
                0,
                (struct sockaddr *)server_addr,
                sizeof(*server_addr)
            );

            if (sent == SOCKET_ERROR)
            {
                printf(
                    "sendto() failed for packet %u: %d\n",
                    seq_no,
                    WSAGetLastError()
                );

                return 0;
            }

            printf(
                "Sent DATA packet %u (%d bytes)\n",
                seq_no,
                bytes_read
            );

            /*
             * Set retransmission timeout.
             */
            {
                DWORD timeout = RTO_MS;

                setsockopt(
                    sock,
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    (const char *)&timeout,
                    sizeof(timeout)
                );
            }

            /*
             * Wait for ACK.
             */
            memset(&ack_packet, 0, sizeof(ack_packet));

            received = recvfrom(
                sock,
                (char *)&ack_packet,
                sizeof(ack_packet),
                0,
                NULL,
                NULL
            );

            /*
             * Timeout.
             */
            if (received == SOCKET_ERROR)
            {
                int error = WSAGetLastError();

                if (error == WSAETIMEDOUT)
                {
                    printf(
                        "TIMEOUT for packet %u - "
                        "retransmitting...\n",
                        seq_no
                    );

                    continue;
                }

                printf(
                    "recvfrom() failed: %d\n",
                    error
                );

                return 0;
            }

            /*
             * Check ACK checksum.
             */
            if (!verify_checksum(&ack_packet))
            {
                printf(
                    "Invalid ACK received - ignoring\n"
                );

                continue;
            }

            /*
             * Check packet type.
             */
            if (ack_packet.type != TYPE_ACK)
            {
                printf(
                    "Unexpected packet received - ignoring\n"
                );

                continue;
            }

            /*
             * Check ACK number.
             */
            if (ack_packet.ack_no == seq_no)
            {
                printf(
                    "ACK %u received\n",
                    ack_packet.ack_no
                );

                break;
            }
        }

        /*
         * Move to next packet.
         */
        seq_no++;
    }


    /*
     * ========================================================
     * SEND END PACKET
     * ========================================================
     */

    memset(&packet, 0, sizeof(packet));

    packet.type = TYPE_END;
    packet.seq_no = seq_no;
    packet.ack_no = 0;
    packet.length = 0;

    set_checksum(&packet);

    printf("-------------------------------------\n");
    printf("Sending END packet...\n");

    /*
     * END must also be acknowledged.
     */
    while (1)
    {
        int sent;
        int received;

        /*
         * Send END.
         */
        sent = sendto(
            sock,
            (char *)&packet,
            sizeof(packet),
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
            seq_no
        );

        /*
         * Set timeout.
         */
        {
            DWORD timeout = RTO_MS;

            setsockopt(
                sock,
                SOL_SOCKET,
                SO_RCVTIMEO,
                (const char *)&timeout,
                sizeof(timeout)
            );
        }

        memset(&ack_packet, 0, sizeof(ack_packet));

        /*
         * Wait for END ACK.
         */
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
                "recvfrom() failed: %d\n",
                WSAGetLastError()
            );

            return 0;
        }

        /*
         * Verify checksum.
         */
        if (!verify_checksum(&ack_packet))
        {
            printf(
                "Invalid END ACK - ignoring\n"
            );

            continue;
        }

        /*
         * Correct END ACK.
         */
        if (ack_packet.type == TYPE_ACK &&
            ack_packet.ack_no == seq_no)
        {
            printf("END ACK received\n");
            break;
        }
    }

    return 1;
}


/*
 * ============================================================
 * STOP-AND-WAIT RECEIVER
 * ============================================================
 */
int stop_wait_receive(
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

        memset(&packet, 0, sizeof(packet));

        client_len = sizeof(client_addr);

        /*
         * Receive packet.
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
         * Verify checksum.
         */
        if (!verify_checksum(&packet))
        {
            printf(
                "Corrupted packet %u received - discarded\n",
                packet.seq_no
            );

            /*
             * No ACK.
             *
             * Sender will timeout and retransmit.
             */
            continue;
        }


        /*
         * ====================================================
         * DATA PACKET
         * ====================================================
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
                 * Create ACK.
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

                /*
                 * Send ACK.
                 */
                sendto(
                    sock,
                    (char *)&ack_packet,
                    sizeof(ack_packet),
                    0,
                    (struct sockaddr *)&client_addr,
                    client_len
                );

                printf(
                    "ACK %u sent\n",
                    packet.seq_no
                );

                expected_seq++;
            }

            /*
             * Duplicate packet.
             */
            else if (packet.seq_no < expected_seq)
            {
                printf(
                    "Duplicate packet %u - "
                    "sending ACK again\n",
                    packet.seq_no
                );

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

            /*
             * Future packet.
             */
            else
            {
                printf(
                    "Unexpected packet %u "
                    "(expected %u)\n",
                    packet.seq_no,
                    expected_seq
                );
            }
        }


        /*
         * ====================================================
         * END PACKET
         * ====================================================
         */
        else if (packet.type == TYPE_END)
        {
            printf("-------------------------------------\n");
            printf("END packet received\n");

            /*
             * ACK the END packet.
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