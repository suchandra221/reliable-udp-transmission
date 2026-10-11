#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "../include/protocol.h"
#include "../include/stop_wait.h"
#include "../include/go_back_n.h"
#include "../include/selective_repeat.h"

#define RECEIVER_PORT 5000

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in receiver_addr;
    FILE *file;

    const char *protocol;
    const char *output_file;

    int success = 0;

    /*
     * Command:
     * receiver.exe <sw|gbn|sr> [output_file]
     */

    if (argc < 2 || argc > 3)
    {
        printf(
            "Usage:\n"
            "  receiver.exe <sw|gbn|sr> [output_file]\n\n"
            "Examples:\n"
            "  receiver.exe sw\n"
            "  receiver.exe gbn\n"
            "  receiver.exe sr\n"
            "  receiver.exe sw received/output.txt\n"
        );

        return 1;
    }

    protocol = argv[1];

    /*
     * Default output file
     */

    if (argc == 3)
    {
        output_file = argv[2];
    }
    else
    {
        output_file = "received/output.txt";
    }

    /*
     * Start Winsock
     */

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    /*
     * Create UDP socket
     */

    sock = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (sock == INVALID_SOCKET)
    {
        printf(
            "Socket creation failed: %d\n",
            WSAGetLastError()
        );

        WSACleanup();
        return 1;
    }

    /*
     * Receiver address
     */

    memset(
        &receiver_addr,
        0,
        sizeof(receiver_addr)
    );

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = INADDR_ANY;
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    /*
     * Bind receiver to port 5000
     */

    if (bind(
            sock,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)
        ) == SOCKET_ERROR)
    {
        printf(
            "Bind failed: %d\n",
            WSAGetLastError()
        );

        closesocket(sock);
        WSACleanup();

        return 1;
    }

    /*
     * Open output file
     */

    file = fopen(
        output_file,
        "wb"
    );

    if (file == NULL)
    {
        printf(
            "Could not create output file: %s\n",
            output_file
        );

        closesocket(sock);
        WSACleanup();

        return 1;
    }

    /*
     * Display information
     */

    printf(
        "=====================================\n"
    );

    printf(
        "         UDP FILE RECEIVER\n"
    );

    printf(
        "=====================================\n"
    );

    printf(
        "Protocol : %s\n",
        protocol
    );

    printf(
        "Port     : %d\n",
        RECEIVER_PORT
    );

    printf(
        "Output   : %s\n",
        output_file
    );

    printf(
        "-------------------------------------\n"
    );

    /*
     * Select ARQ protocol
     */

    if (strcmp(protocol, "sw") == 0)
    {
        printf(
            "Using Stop-and-Wait ARQ\n"
        );

        printf(
            "Waiting for packets...\n"
        );

        success =
            stop_wait_receive(
                sock,
                file
            );
    }
    else if (strcmp(protocol, "gbn") == 0)
    {
        printf(
            "Using Go-Back-N ARQ\n"
        );

        printf(
            "Window size: %d\n",
            GBN_WINDOW_SIZE
        );

        printf(
            "Waiting for packets...\n"
        );

        success =
            go_back_n_receive(
                sock,
                file
            );
    }
    else if (strcmp(protocol, "sr") == 0)
    {
        printf(
            "Using Selective Repeat ARQ\n"
        );

        printf(
            "Window size: %d\n",
            SR_WINDOW_SIZE
        );

        printf(
            "Waiting for packets...\n"
        );

        success =
            selective_repeat_receive(
                sock,
                file
            );
    }
    else
    {
        printf(
            "Unknown protocol: %s\n",
            protocol
        );

        printf(
            "Use: sw, gbn, or sr\n"
        );

        fclose(file);
        closesocket(sock);
        WSACleanup();

        return 1;
    }

    /*
     * Transfer result
     */

    printf(
        "-------------------------------------\n"
    );

    if (success)
    {
        printf(
            "File received successfully!\n"
        );

        printf(
            "Saved as: %s\n",
            output_file
        );
    }
    else
    {
        printf(
            "File reception failed.\n"
        );
    }

    /*
     * Cleanup
     */

    fclose(file);

    closesocket(sock);

    WSACleanup();

    return success ? 0 : 1;
}