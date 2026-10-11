#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#include "../include/protocol.h"
#include "../include/stop_wait.h"
#include "../include/go_back_n.h"
#include "../include/selective_repeat.h"

#define CHANNEL_PORT 5001
#define CHANNEL_IP   "127.0.0.1"

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in channel_addr;
    FILE *file;
    int success = 0;

    if (argc != 3)
    {
        printf(
            "Usage:\n"
            "  sender.exe <file> <sw|gbn|sr>\n\n"
            "Examples:\n"
            "  sender.exe data/input.txt sw\n"
            "  sender.exe data/input.txt gbn\n"
            "  sender.exe data/input.txt sr\n"
        );
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    memset(&channel_addr, 0, sizeof(channel_addr));

    channel_addr.sin_family = AF_INET;
    channel_addr.sin_addr.s_addr = inet_addr(CHANNEL_IP);
    channel_addr.sin_port = htons(CHANNEL_PORT);

    file = fopen(argv[1], "rb");

    if (file == NULL)
    {
        printf("Could not open file: %s\n", argv[1]);

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("=====================================\n");
    printf("          UDP FILE SENDER\n");
    printf("=====================================\n");
    printf("File     : %s\n", argv[1]);
    printf("Protocol : %s\n", argv[2]);
    printf("Channel  : %s:%d\n", CHANNEL_IP, CHANNEL_PORT);
    printf("-------------------------------------\n");

    if (strcmp(argv[2], "sw") == 0)
    {
        printf("Using Stop-and-Wait ARQ\n");
        success = stop_wait_send(
            sock,
            &channel_addr,
            file
        );
    }
    else if (strcmp(argv[2], "gbn") == 0)
    {
        printf("Using Go-Back-N ARQ\n");
        printf("Window size: %d\n", GBN_WINDOW_SIZE);

        success = go_back_n_send(
            sock,
            &channel_addr,
            file
        );
    }
    else if (strcmp(argv[2], "sr") == 0)
    {
        printf("Using Selective Repeat ARQ\n");
        printf("Window size: %d\n", SR_WINDOW_SIZE);

        success = selective_repeat_send(
            sock,
            &channel_addr,
            file
        );
    }
    else
    {
        printf("Unknown protocol: %s\n", argv[2]);
        printf("Use: sw, gbn, or sr\n");

        fclose(file);
        closesocket(sock);
        WSACleanup();

        return 1;
    }

    if (success)
        printf("File sent successfully!\n");
    else
        printf("File transfer failed.\n");

    fclose(file);
    closesocket(sock);
    WSACleanup();

    return success ? 0 : 1;
}