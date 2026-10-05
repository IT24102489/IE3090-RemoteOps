#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdatomic.h>

#define PORT 9410
#define BUFFER_SIZE 16384


//   UDP LISTENER STRUCTURE

typedef struct {
    int socket_fd;
    pthread_t thread;
    atomic_int stop_requested;
    int active;
} UDPListener;


//   TCP HELPER FUNCTIONS

int send_all(int sockfd,
             const void *buffer,
             size_t length)
{
    size_t total_sent = 0;
    const char *data =
        (const char *)buffer;

    while (total_sent < length) {

        ssize_t sent =
            send(sockfd,
                 data + total_sent,
                 length - total_sent,
                 0);

        if (sent <= 0) {
            return -1;
        }

        total_sent +=
            (size_t)sent;
    }

    return 0;
}


int recv_line(int sockfd,
              char *buffer,
              int max_size)
{
    int i = 0;
    char ch;

    while (i < max_size - 1) {

        int bytes =
            recv(sockfd,
                 &ch,
                 1,
                 0);

        if (bytes == 0) {
            return 0;
        }

        if (bytes < 0) {
            return -1;
        }

        if (ch == '\n') {

            buffer[i] = '\0';

            return i + 1;
        }

        if (ch != '\r') {
            buffer[i++] = ch;
        }
    }

    buffer[i] = '\0';

    return i + 1;
}


//   PUT FILE

int controller_put(int sockfd,
                   const char *filename)
{
    FILE *fp =
        fopen(filename, "rb");

    if (fp == NULL) {

        printf(
            "Local file not found: %s\n",
            filename);

        return -1;
    }


    fseek(fp,
          0,
          SEEK_END);

    long filesize =
        ftell(fp);

    rewind(fp);


    if (filesize < 0) {

        fclose(fp);

        printf(
            "Unable to determine file size.\n");

        return -1;
    }


    /*
     * If user enters a path such as
     * folder/test.txt, only send test.txt
     * to the Agent.
     */

    const char *base =
        strrchr(filename, '/');

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
                  BUFFER_SIZE) <= 0) {

        printf(
            "Agent disconnected during upload.\n");

        return -1;
    }


    printf("%s\n",
           response);


    return 0;
}


//   GET FILE

int controller_get(int sockfd,
                   const char *filename)
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
                  BUFFER_SIZE) <= 0) {

        printf(
            "Agent disconnected during download.\n");

        return -1;
    }


    /*
     * If Agent returned an ERR response,
     * simply display it.
     */

    if (strncmp(response,
                "OK FILE_SEND ",
                13) != 0) {

        printf("%s\n",
               response);

        return 0;
    }


    char received_name[256];
    long filesize;


    if (sscanf(response,
               "OK FILE_SEND %255s %ld",
               received_name,
               &filesize) != 2) {

        printf(
            "Invalid FILE_SEND response.\n");

        return -1;
    }


    if (filesize < 0) {

        printf(
            "Invalid received file size.\n");

        return -1;
    }


    char local_name[512];

    snprintf(local_name,
             sizeof(local_name),
             "downloaded_%s",
             received_name);


    FILE *fp =
        fopen(local_name,
              "wb");


    if (fp == NULL) {

        printf(
            "Unable to create local file.\n");

        return -1;
    }


    long remaining =
        filesize;

    char file_buffer[4096];


    while (remaining > 0) {

        size_t chunk_size =
            remaining <
            (long)sizeof(file_buffer)
            ?
            (size_t)remaining
            :
            sizeof(file_buffer);


        ssize_t received =
            recv(sockfd,
                 file_buffer,
                 chunk_size,
                 0);


        if (received <= 0) {

            fclose(fp);

            remove(local_name);

            printf(
                "Download failed.\n");

            return -1;
        }


        size_t written =
            fwrite(file_buffer,
                   1,
                   (size_t)received,
                   fp);


        if (written !=
            (size_t)received) {

            fclose(fp);

            remove(local_name);

            printf(
                "Unable to write downloaded file.\n");

            return -1;
        }


        remaining -=
            received;
    }


    fclose(fp);


    printf("%s\n",
           response);

    printf(
        "Saved as: %s\n",
        local_name);


    return 0;
}


//   UDP MONITOR LISTENER

void *udp_listener_thread(void *arg)
{
    UDPListener *listener =
        (UDPListener *)arg;

    char buffer[1024];


    while (!atomic_load(
               &listener->stop_requested)) {

        ssize_t bytes =
            recvfrom(
                listener->socket_fd,
                buffer,
                sizeof(buffer) - 1,
                0,
                NULL,
                NULL);


        if (bytes > 0) {

            buffer[bytes] = '\0';

            printf(
                "\n[UDP MONITOR] %s\n",
                buffer);

            printf(
                "RemoteOps> ");

            fflush(stdout);
        }
    }


    return NULL;
}


void stop_udp_listener(
    UDPListener *listener)
{
    if (!listener->active) {
        return;
    }


    atomic_store(
        &listener->stop_requested,
        1);


    pthread_join(
        listener->thread,
        NULL);


    close(
        listener->socket_fd);


    listener->socket_fd = -1;
    listener->active = 0;
}


//   START LOCAL UDP LISTENER

int start_udp_listener(
    UDPListener *listener,
    int udp_port)
{
    if (listener->active) {

        printf(
            "UDP monitor listener is already active.\n");

        return -1;
    }


    listener->socket_fd =
        socket(AF_INET,
               SOCK_DGRAM,
               0);


    if (listener->socket_fd < 0) {

        perror(
            "UDP socket creation failed");

        return -1;
    }


    struct sockaddr_in udp_addr;

    memset(&udp_addr,
           0,
           sizeof(udp_addr));


    udp_addr.sin_family =
        AF_INET;

    udp_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);

    udp_addr.sin_port =
        htons(udp_port);


    if (bind(
            listener->socket_fd,
            (struct sockaddr *)
            &udp_addr,
            sizeof(udp_addr)) < 0) {

        perror(
            "UDP bind failed");

        close(
            listener->socket_fd);

        listener->socket_fd =
            -1;

        return -1;
    }


    /*
     * Timeout allows the UDP thread
     * to periodically check whether
     * STOP was requested.
     */

    struct timeval timeout;

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;


    setsockopt(
        listener->socket_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout));


    atomic_store(
        &listener->stop_requested,
        0);


    if (pthread_create(
            &listener->thread,
            NULL,
            udp_listener_thread,
            listener) != 0) {

        perror(
            "UDP listener thread failed");

        close(
            listener->socket_fd);

        listener->socket_fd =
            -1;

        return -1;
    }


    listener->active = 1;


    return 0;
}


//   MAIN

int main()
{
    int sockfd;

    struct sockaddr_in server_addr;

    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];


//       UDP listener context

    UDPListener udp_listener;

    memset(&udp_listener,
           0,
           sizeof(udp_listener));

    udp_listener.socket_fd = -1;
    udp_listener.active = 0;

    atomic_init(
        &udp_listener.stop_requested,
        0);


//       Create TCP socket

    sockfd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (sockfd < 0) {

        perror(
            "Socket creation failed");

        return 1;
    }


//       Configure Agent address

    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(PORT);


    if (inet_pton(
            AF_INET,
            "127.0.0.1",
            &server_addr.sin_addr) <= 0) {

        printf(
            "Invalid Agent address.\n");

        close(sockfd);

        return 1;
    }


//       Connect to Agent

    if (connect(
            sockfd,
            (struct sockaddr *)
            &server_addr,
            sizeof(server_addr)) < 0) {

        perror(
            "Connection failed");

        close(sockfd);

        return 1;
    }


    printf(
        "Connected to RemoteOps Agent.\n");

    printf(
        "Enter commands below.\n");


//       COMMAND LOOP

    while (1) {

        printf(
            "RemoteOps> ");

        fflush(stdout);


        if (fgets(
                command,
                sizeof(command),
                stdin) == NULL) {

            break;
        }


        /*
         * Remove local newline.
         */

        command[
            strcspn(
                command,
                "\n")] = '\0';


        /*
         * Ignore empty input.
         */

        if (strlen(command) == 0) {
            continue;
        }


//           PUT

        if (strncmp(
                command,
                "PUT ",
                4) == 0) {

            char *filename =
                command + 4;


            controller_put(
                sockfd,
                filename);


            continue;
        }


//           GET

        if (strncmp(
                command,
                "GET ",
                4) == 0) {

            char *filename =
                command + 4;


            controller_get(
                sockfd,
                filename);


            continue;
        }


//           MONITOR START

        int monitor_start_requested = 0;


        if (strncmp(
                command,
                "MONITOR START ",
                14) == 0) {

            int udp_port;


            if (sscanf(
                    command,
                    "MONITOR START %d",
                    &udp_port) != 1 ||
                udp_port < 1 ||
                udp_port > 65535) {

                printf(
                    "Invalid UDP port.\n");

                continue;
            }


            if (start_udp_listener(
                    &udp_listener,
                    udp_port) != 0) {

                continue;
            }


            monitor_start_requested = 1;
        }


//           SEND NORMAL TCP COMMAND

        char wire_command[
            BUFFER_SIZE + 2];


        snprintf(
            wire_command,
            sizeof(wire_command),
            "%s\n",
            command);


        if (send_all(
                sockfd,
                wire_command,
                strlen(wire_command)) < 0) {

            printf(
                "Failed to send command.\n");

            stop_udp_listener(
                &udp_listener);

            break;
        }


//           RECEIVE TCP RESPONSE

        int result =
            recv_line(
                sockfd,
                response,
                BUFFER_SIZE);


        if (result <= 0) {

            printf(
                "Agent disconnected.\n");

            stop_udp_listener(
                &udp_listener);

            break;
        }


        printf(
            "%s\n",
            response);


//           If Agent rejected MONITOR START,
//         stop the local UDP listener.

        if (monitor_start_requested) {

            if (strncmp(
                    response,
                    "OK MONITOR_STARTED",
                    18) != 0) {

                stop_udp_listener(
                    &udp_listener);
            }
        }


//           MONITOR STOP

        if (strcmp(
                command,
                "MONITOR STOP") == 0) {

            stop_udp_listener(
                &udp_listener);
        }


//           QUIT

        if (strcmp(
                command,
                "QUIT") == 0) {

            stop_udp_listener(
                &udp_listener);

            break;
        }
    }


//       Final cleanup

    stop_udp_listener(
        &udp_listener);


    close(sockfd);


    return 0;
}
