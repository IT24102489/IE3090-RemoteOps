
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
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Step 1: Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Step 2: Configure Agent address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    inet_pton(AF_INET, "127.0.0.1",
              &server_addr.sin_addr);

    // Step 3: Connect to Agent
    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {

        perror("Connection failed");
        close(sockfd);
        return 1;
    }

    printf("Connected to RemoteOps Agent!\n");

    // Step 4: Send test message
    char message[] = "HELLO FROM CONTROLLER\n";

    send(sockfd, message, strlen(message), 0);

    // Step 5: Receive Agent response
    memset(buffer, 0, BUFFER_SIZE);

    int bytes = recv(sockfd, buffer,
                     BUFFER_SIZE - 1, 0);

    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Agent response: %s", buffer);
    }

    // Step 6: Close connection
    close(sockfd);

    return 0;
}
