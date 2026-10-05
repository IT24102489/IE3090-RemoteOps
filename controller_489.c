#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define BUFFER_SIZE 1024

int recv_line(int sockfd, char *buffer, int max_size)
{
    int i = 0;
    char ch;

    while (i < max_size - 1) {

        int bytes = recv(sockfd, &ch, 1, 0);

        if (bytes == 0) {
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
    int sockfd;
    struct sockaddr_in server_addr;

    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    // 1. Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // 2. Configure Agent address
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    inet_pton(AF_INET,
              "127.0.0.1",
              &server_addr.sin_addr);

    // 3. Connect to Agent
    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {

        perror("Connection failed");
        close(sockfd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");
    printf("Enter commands below.\n");

    while (1) {

        printf("RemoteOps> ");
        fflush(stdout);

        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL) {
            break;
        }

        // Send complete command including newline
        send(sockfd,
             command,
             strlen(command),
             0);

        // Receive one response line
        int result =
            recv_line(sockfd,
                      response,
                      BUFFER_SIZE);

        if (result <= 0) {
            printf("Agent disconnected.\n");
            break;
        }

        printf("%s\n", response);

        // Remove newline from local input for comparison
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "QUIT") == 0) {
            break;
        }
    }

    close(sockfd);

    return 0;
}
