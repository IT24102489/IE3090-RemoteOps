#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define BUFFER_SIZE 1024

#define AUTH_TOKEN "OPS-2489"
#define SID "9842"

/*
 * Receives one complete line from the TCP socket.
 * The function continues reading until '\n' is found.
 */
int recv_line(int sockfd, char *buffer, int max_size)
{
    int i = 0;
    char ch;

    while (i < max_size - 1) {

        int bytes = recv(sockfd, &ch, 1, 0);

        if (bytes == 0) {
            // Controller disconnected
            return 0;
        }

        if (bytes < 0) {
            return -1;
        }

        if (ch == '\n') {
            break;
        }

        buffer[i++] = ch;
    }

    buffer[i] = '\0';

    return i;
}

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];
    int authenticated = 0;

    // 1. Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // 2. Configure Agent address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    // 3. Bind socket
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {

        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // 4. Listen for Controller connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent started on port %d\n", PORT);
    printf("Waiting for Controller...\n");

    // 5. Accept one Controller for this development stage
    client_fd = accept(server_fd, NULL, NULL);

    if (client_fd < 0) {
        perror("Accept failed");
        close(server_fd);
        return 1;
    }

    printf("Controller connected!\n");

    // 6. Continue receiving commands on the same TCP connection
    while (1) {

        int result = recv_line(client_fd, buffer, BUFFER_SIZE);

        if (result == 0) {
            printf("Controller disconnected.\n");
            break;
        }

        if (result < 0) {
            perror("Receive failed");
            break;
        }

        printf("Received command: %s\n", buffer);

        // AUTH command
        if (strncmp(buffer, "AUTH ", 5) == 0) {

            char *token = buffer + 5;

            if (strcmp(token, AUTH_TOKEN) == 0) {

                authenticated = 1;

                char response[] =
                    "OK AUTHENTICATED SID:" SID "\n";

                send(client_fd,
                     response,
                     strlen(response),
                     0);
            }
            else {

                char response[] =
                    "ERR 001 AUTH_FAILED SID:" SID "\n";

                send(client_fd,
                     response,
                     strlen(response),
                     0);
            }

            continue;
        }

        // Reject all non-AUTH commands until authenticated
        if (!authenticated) {

            char response[] =
                "ERR 003 NOT_AUTHENTICATED SID:" SID "\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            continue;
        }

        // QUIT command
        if (strcmp(buffer, "QUIT") == 0) {

            char response[] =
                "OK BYE SID:" SID "\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("Controller ended the session.\n");

            break;
        }

        // Temporary response for commands not implemented yet
        char response[] =
            "ERR 006 UNKNOWN_COMMAND SID:" SID "\n";

        send(client_fd,
             response,
             strlen(response),
             0);
    }

    close(client_fd);
    close(server_fd);

    return 0;
}
