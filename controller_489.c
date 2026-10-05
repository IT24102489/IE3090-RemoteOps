#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define PORT 9410
#define BUFFER_SIZE 16384

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

int send_all(int sockfd, const void *buffer, size_t length)
{
    size_t total_sent = 0;
    const char *data = (const char *)buffer;

    while (total_sent < length) {

        ssize_t sent = send(sockfd,
                            data + total_sent,
                            length - total_sent,
                            0);

        if (sent <= 0) {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

int controller_put(int sockfd, const char *filename)
{
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        printf("Local file not found: %s\n", filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);

    const char *base = strrchr(filename, '/');

    if (base != NULL) {
        base++;
    }
    else {
        base = filename;
    }

    char header[512];

    snprintf(header,
             sizeof(header),
             "PUT %s %ld\n",
             base,
             filesize);

    if (send_all(sockfd,
                 header,
                 strlen(header)) < 0) {

        fclose(fp);
        return -1;
    }

    char file_buffer[4096];
    size_t bytes_read;

    while ((bytes_read =
            fread(file_buffer,
                  1,
                  sizeof(file_buffer),
                  fp)) > 0) {

        if (send_all(sockfd,
                     file_buffer,
                     bytes_read) < 0) {

            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    char response[BUFFER_SIZE];

    if (recv_line(sockfd,
                  response,
                  sizeof(response)) <= 0) {

        return -1;
    }

    printf("%s\n", response);

    return 0;
}


int controller_get(int sockfd, const char *filename)
{
    char request[512];

    snprintf(request,
             sizeof(request),
             "GET %s\n",
             filename);

    if (send_all(sockfd,
                 request,
                 strlen(request)) < 0) {
        return -1;
    }

    char response[BUFFER_SIZE];

    if (recv_line(sockfd,
                  response,
                  sizeof(response)) <= 0) {

        return -1;
    }

    if (strncmp(response,
                "OK FILE_SEND ",
                13) != 0) {

        printf("%s\n", response);
        return 0;
    }

    char received_name[256];
    long filesize;

    if (sscanf(response,
               "OK FILE_SEND %255s %ld",
               received_name,
               &filesize) != 2) {

        printf("Invalid FILE_SEND response.\n");
        return -1;
    }

    char local_name[512];

    snprintf(local_name,
             sizeof(local_name),
             "downloaded_%s",
             received_name);

    FILE *fp = fopen(local_name, "wb");

    if (fp == NULL) {
        printf("Unable to create local file.\n");
        return -1;
    }

    long remaining = filesize;
    char file_buffer[4096];

    while (remaining > 0) {

        size_t chunk_size =
          remaining < (long)sizeof(file_buffer)
          ? (size_t)remaining
          : sizeof(file_buffer);

        ssize_t received =
            recv(sockfd,
                 file_buffer,
                 chunk_size,
                 0);

        if (received <= 0) {
            fclose(fp);
            return -1;
        }

        fwrite(file_buffer,
               1,
               received,
               fp);

        remaining -= received;
    }

    fclose(fp);

    printf("%s\n", response);
    printf("Saved as: %s\n", local_name);

    return 0;
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

    command[strcspn(command, "\n")] = '\0';

    if (strncmp(command, "PUT ", 4) == 0) {

        char *filename = command + 4;

        controller_put(sockfd, filename);

        continue;
    }

    if (strncmp(command, "GET ", 4) == 0) {

        char *filename = command + 4;

        controller_get(sockfd, filename);

        continue;
    }

    char wire_command[BUFFER_SIZE + 2 ];

    snprintf(wire_command,
             sizeof(wire_command),
             "%s\n",
             command);

    send_all(sockfd,
             wire_command,
             strlen(wire_command));

    int result =
        recv_line(sockfd,
                  response,
                  BUFFER_SIZE);

    if (result <= 0) {
        printf("Agent disconnected.\n");
        break;
    }

    printf("%s\n", response);

    if (strcmp(command, "QUIT") == 0) {
        break;
    }
}

    close(sockfd);

    return 0;
}
