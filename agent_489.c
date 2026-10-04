
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define BUFFER_SIZE 1024

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // 1. Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // 2. Configure server address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    // 3. Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    // 4. Listen for connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent started on port %d\n", PORT);
    printf("Waiting for Controller...\n");
    fflush(stdout);

    // 5. Accept a Controller
    client_fd = accept(server_fd, NULL, NULL);

    if (client_fd < 0) {
        perror("Accept failed");
        close(server_fd);
        return 1;
    }

    printf("Controller connected!\n");

    // 6. Receive test message
    int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Received: %s\n", buffer);

        // 7. Send response
        char response[] = "HELLO FROM AGENT\n";
        send(client_fd, response, strlen(response), 0);
    }

    // 8. Close sockets
    close(client_fd);
    close(server_fd);

    return 0;
}
