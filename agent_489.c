#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#define PORT 9410
#define BUFFER_SIZE 16384

#define AUTH_TOKEN "OPS-2489"
#define SID "9842"

#define STORAGE_DIR "./agentfiles/IT24102489"
#define LOG_FILE "remoteops_IT24102489.log"

#define MAX_FILE_SIZE (10 * 1024 * 1024)
#define MONITOR_INTERVAL 5


//UDP MONITOR STRUCTURE

typedef struct {
    int udp_socket;
    struct sockaddr_in destination;
    pthread_t thread;
    atomic_int stop_requested;
    int active;
} MonitorContext;


//TCP HELPER FUNCTIONS

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

        total_sent += (size_t)sent;
    }

    return 0;
}


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
            buffer[i] = '\0';

            /*
             * Return a positive value even for an empty line,
             * so it is not confused with a disconnection.
             */
            return i + 1;
        }

        if (ch != '\r') {
            buffer[i++] = ch;
        }
    }

    buffer[i] = '\0';

    return i + 1;
}

//FILE STORAGE

void ensure_storage_directory()
{
    mkdir("./agentfiles", 0755);
    mkdir(STORAGE_DIR, 0755);
}


int valid_filename(const char *filename)
{
    if (filename == NULL || strlen(filename) == 0) {
        return 0;
    }

    if (strstr(filename, "..") != NULL) {
        return 0;
    }
    if (strchr(filename, '/') != NULL) {
        return 0;
    }

    return 1;
}


//LOGGIN

void log_activity(const char *message)
{
    FILE *fp = fopen(LOG_FILE, "a");

    if (fp == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char timestamp[64];

    strftime(timestamp,
             sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S",
             t);

    fprintf(fp,
            "[%s] %s\n",
            timestamp,
            message);

    fclose(fp);
}


//   SYSINFO FUNCTIONS

double get_cpu_load()
{
    FILE *fp;
    double load = 0.0;

    fp = fopen("/proc/loadavg", "r");

    if (fp == NULL) {
        return -1.0;
    }

    if (fscanf(fp, "%lf", &load) != 1) {
        fclose(fp);
        return -1.0;
    }

    fclose(fp);

    return load;
}


long get_memory_used_mb()
{
    FILE *fp;
    char line[256];

    long mem_total = 0;
    long mem_available = 0;

    fp = fopen("/proc/meminfo", "r");

    if (fp == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {

        if (sscanf(line,
                   "MemTotal: %ld kB",
                   &mem_total) == 1) {
            continue;
        }

        if (sscanf(line,
                   "MemAvailable: %ld kB",
                   &mem_available) == 1) {
            continue;
        }
    }

    fclose(fp);

    if (mem_total <= 0) {
        return -1;
    }

    long used_kb = mem_total - mem_available;

    return used_kb / 1024;
}


long get_uptime_seconds()
{
    FILE *fp;
    double uptime = 0.0;

    fp = fopen("/proc/uptime", "r");

    if (fp == NULL) {
        return -1;
    }

    if (fscanf(fp, "%lf", &uptime) != 1) {
        fclose(fp);
        return -1;
    }

    fclose(fp);

    return (long)uptime;
}


//   LISTPROC

int get_process_list(char *output, size_t output_size)
{
    FILE *fp;
    char line[256];

    output[0] = '\0';

    fp = popen("ps -eo pid=,comm=", "r");

    if (fp == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {

        int pid;
        char process_name[128];

        if (sscanf(line,
                   "%d %127s",
                   &pid,
                   process_name) == 2) {

            char entry[180];

            snprintf(entry,
                     sizeof(entry),
                     "%d:%s,",
                     pid,
                     process_name);

            size_t current_length = strlen(output);
            size_t entry_length = strlen(entry);

            if (current_length + entry_length
                < output_size - 1) {

                strcat(output, entry);
            }
            else {
                break;
            }
        }
    }

    pclose(fp);

    size_t len = strlen(output);

    if (len > 0 && output[len - 1] == ',') {
        output[len - 1] = '\0';
    }

    return 0;
}


//   EXEC

void make_single_line(char *text)
{
    for (int i = 0; text[i] != '\0'; i++) {

        if (text[i] == '\n' ||
            text[i] == '\r') {

            text[i] = ' ';
        }
    }
}


int execute_allowed_command(const char *name,
                            char *output,
                            size_t output_size)
{
    const char *linux_command = NULL;

    if (strcmp(name, "DATE") == 0) {
        linux_command = "date";
    }
    else if (strcmp(name, "UPTIME") == 0) {
        linux_command = "uptime -p";
    }
    else if (strcmp(name, "DISKFREE") == 0) {
        linux_command = "df -h /";
    }
    else if (strcmp(name, "HOSTNAME") == 0) {
        linux_command = "hostname";
    }
    else if (strcmp(name, "WHOAMI") == 0) {
        linux_command = "whoami";
    }
    else {
        return 0;
    }

    FILE *fp = popen(linux_command, "r");

    if (fp == NULL) {
        return -1;
    }

    output[0] = '\0';

    char line[512];

    while (fgets(line,
                 sizeof(line),
                 fp) != NULL) {

        if (strlen(output) + strlen(line)
            < output_size - 1) {

            strcat(output, line);
        }
        else {
            break;
        }
    }

    pclose(fp);

    make_single_line(output);

    return 1;
}


//   UDP MONITOR THREAD

void *monitor_thread(void *arg)
{
    MonitorContext *monitor =
        (MonitorContext *)arg;

    while (!atomic_load(
               &monitor->stop_requested)) {

        double cpu_load =
            get_cpu_load();

        long mem_used_mb =
            get_memory_used_mb();

        long uptime_sec =
            get_uptime_seconds();

        char message[512];

        snprintf(message,
                 sizeof(message),
                 "SYSINFO %.2f %ld %ld SID:%s",
                 cpu_load,
                 mem_used_mb,
                 uptime_sec,
                 SID);

        sendto(monitor->udp_socket,
               message,
               strlen(message),
               0,
               (struct sockaddr *)
               &monitor->destination,
               sizeof(monitor->destination));

        /*
         * Sleep one second at a time so STOP
         * does not have to wait the full interval.
         */
        for (int i = 0;
             i < MONITOR_INTERVAL &&
             !atomic_load(
                 &monitor->stop_requested);
             i++) {

            sleep(1);
        }
    }

    return NULL;
}


void stop_monitor(MonitorContext *monitor)
{
    if (!monitor->active) {
        return;
    }

    atomic_store(
        &monitor->stop_requested,
        1);

    pthread_join(
        monitor->thread,
        NULL);

    close(monitor->udp_socket);

    monitor->udp_socket = -1;
    monitor->active = 0;
}


//   MAIN

int main()
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len =
        sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    int authenticated = 0;

    ensure_storage_directory();


//       Create TCP socket

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0) {

        perror("Socket creation failed");

        return 1;
    }


    int reuse = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &reuse,
               sizeof(reuse));


//       Configure Agent address

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    server_addr.sin_port =
        htons(PORT);


//  Bind

    if (bind(server_fd,
             (struct sockaddr *)
             &server_addr,
             sizeof(server_addr)) < 0) {

        perror("Bind failed");

        close(server_fd);

        return 1;
    }


//Listen

    if (listen(server_fd, 5) < 0) {

        perror("Listen failed");

        close(server_fd);

        return 1;
    }


    printf("RemoteOps Agent started on port %d\n",
           PORT);

    printf("Waiting for Controller...\n");

    log_activity("Agent started");


// Accept Controller

    client_fd =
        accept(server_fd,
               (struct sockaddr *)
               &client_addr,
               &client_len);

    if (client_fd < 0) {

        perror("Accept failed");

        close(server_fd);

        return 1;
    }


    printf("Controller connected!\n");

    log_activity("Controller connected");


//UDP monitor context for this session

    MonitorContext monitor;

    memset(&monitor,
           0,
           sizeof(monitor));

    monitor.udp_socket = -1;
    monitor.active = 0;

    atomic_init(
        &monitor.stop_requested,
        0);


// COMMAND LOOP

    while (1) {

        int result =
            recv_line(client_fd,
                      buffer,
                      BUFFER_SIZE);

        if (result == 0) {

            printf("Controller disconnected.\n");

            log_activity(
                "Controller disconnected unexpectedly");

            stop_monitor(&monitor);

            break;
        }


        if (result < 0) {

            perror("Receive failed");

            log_activity(
                "Controller connection receive error");

            stop_monitor(&monitor);

            break;
        }


        printf("Received command: %s\n",
               buffer);


        char log_message[
            BUFFER_SIZE + 16];

        snprintf(log_message,
                 sizeof(log_message),
                 "Command: %s",
                 buffer);

        log_activity(log_message);


// AUTH

        if (strncmp(buffer,
                    "AUTH ",
                    5) == 0) {

            char *token =
                buffer + 5;

            if (strcmp(token,
                       AUTH_TOKEN) == 0) {

                authenticated = 1;

                char response[] =
                    "OK AUTHENTICATED SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));
            }
            else {

                char response[] =
                    "ERR 001 AUTH_FAILED SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));
            }

            continue;
        }


// Block everything else until AUTH

        if (!authenticated) {

            char response[] =
                "ERR 003 NOT_AUTHENTICATED SID:"
                SID
                "\n";

            send_all(client_fd,
                     response,
                     strlen(response));

            continue;
        }


//  SYSINFO

        if (strcmp(buffer,
                   "SYSINFO") == 0) {

            double cpu_load =
                get_cpu_load();

            long mem_used_mb =
                get_memory_used_mb();

            long uptime_sec =
                get_uptime_seconds();

            char response[512];

            snprintf(response,
                     sizeof(response),
                     "OK SYSINFO %.2f %ld %ld SID:%s\n",
                     cpu_load,
                     mem_used_mb,
                     uptime_sec,
                     SID);

            send_all(client_fd,
                     response,
                     strlen(response));

            continue;
        }


//LISTPROC

        if (strcmp(buffer,
                   "LISTPROC") == 0) {

            char processes[
                BUFFER_SIZE - 128];

            char response[
                BUFFER_SIZE];

            if (get_process_list(
                    processes,
                    sizeof(processes)) == 0) {

                snprintf(response,
                         sizeof(response),
                         "OK PROCS %s SID:%s\n",
                         processes,
                         SID);
            }
            else {

                snprintf(response,
                         sizeof(response),
                         "ERR 007 PROCESS_LIST_FAILED SID:%s\n",
                         SID);
            }

            send_all(client_fd,
                     response,
                     strlen(response));

            continue;
        }


//   EXEC

        if (strncmp(buffer,
                    "EXEC ",
                    5) == 0) {

            char *command_name =
                buffer + 5;

            char command_output[
                BUFFER_SIZE - 128];

            char response[
                BUFFER_SIZE];

            int exec_result =
                execute_allowed_command(
                    command_name,
                    command_output,
                    sizeof(command_output));

            if (exec_result == 1) {

                snprintf(response,
                         sizeof(response),
                         "OK EXEC_RESULT %s SID:%s\n",
                         command_output,
                         SID);
            }

            else if (exec_result == 0) {

                snprintf(response,
                         sizeof(response),
                         "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                         SID);
            }

            else {

                snprintf(response,
                         sizeof(response),
                         "ERR 008 EXEC_FAILED SID:%s\n",
                         SID);
            }

            send_all(client_fd,
                     response,
                     strlen(response));

            continue;
        }


//    PUT FILE

        if (strncmp(buffer,
                    "PUT ",
                    4) == 0) {

            char filename[256];
            long filesize;


            if (sscanf(buffer,
                       "PUT %255s %ld",
                       filename,
                       &filesize) != 2) {

                char response[] =
                    "ERR 009 INVALID_PUT_FORMAT SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                /*
                 * Cannot safely recover framing if raw
                 * bytes may already follow.
                 */
                break;
            }


            if (!valid_filename(
                    filename)) {

                char response[] =
                    "ERR 010 INVALID_FILENAME SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                break;
            }


            if (filesize < 0 ||
                filesize >
                MAX_FILE_SIZE) {

                char response[] =
                    "ERR 004 FILE_TOO_LARGE SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                break;
            }


            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "%s/%s",
                     STORAGE_DIR,
                     filename);


            FILE *fp =
                fopen(filepath,
                      "wb");

            if (fp == NULL) {

                char response[] =
                    "ERR 011 FILE_CREATE_FAILED SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                break;
            }


            long remaining =
                filesize;

            char file_buffer[4096];

            int transfer_failed = 0;


            while (remaining > 0) {

                size_t chunk_size =
                    remaining <
                    (long)sizeof(file_buffer)
                    ?
                    (size_t)remaining
                    :
                    sizeof(file_buffer);


                ssize_t received =
                    recv(client_fd,
                         file_buffer,
                         chunk_size,
                         0);


                if (received <= 0) {

                    transfer_failed = 1;

                    break;
                }


                size_t written =
                    fwrite(file_buffer,
                           1,
                           (size_t)received,
                           fp);


                if (written !=
                    (size_t)received) {

                    transfer_failed = 1;

                    break;
                }


                remaining -=
                    received;
            }


            fclose(fp);


            if (transfer_failed ||
                remaining != 0) {

                remove(filepath);

                log_activity(
                    "File upload failed");

                break;
            }


            char response[512];

            snprintf(response,
                     sizeof(response),
                     "OK FILE_RECEIVED %s SID:%s\n",
                     filename,
                     SID);


            send_all(client_fd,
                     response,
                     strlen(response));


            printf(
                "File received: %s (%ld bytes)\n",
                filename,
                filesize);


            char file_log[512];

            snprintf(
                file_log,
                sizeof(file_log),
                "File uploaded: %s (%ld bytes)",
                filename,
                filesize);

            log_activity(file_log);


            continue;
        }


//           GET FILE

        if (strncmp(buffer,
                    "GET ",
                    4) == 0) {

            char filename[256];


            if (sscanf(buffer,
                       "GET %255s",
                       filename) != 1) {

                char response[] =
                    "ERR 012 INVALID_GET_FORMAT SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            if (!valid_filename(
                    filename)) {

                char response[] =
                    "ERR 010 INVALID_FILENAME SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "%s/%s",
                     STORAGE_DIR,
                     filename);


            FILE *fp =
                fopen(filepath,
                      "rb");


            if (fp == NULL) {

                char response[] =
                    "ERR 005 FILE_NOT_FOUND SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            fseek(fp,
                  0,
                  SEEK_END);

            long filesize =
                ftell(fp);

            rewind(fp);


            char response[512];

            snprintf(response,
                     sizeof(response),
                     "OK FILE_SEND %s %ld SID:%s\n",
                     filename,
                     filesize,
                     SID);


            if (send_all(
                    client_fd,
                    response,
                    strlen(response)) < 0) {

                fclose(fp);

                break;
            }


            char file_buffer[4096];

            size_t bytes_read;


            while ((bytes_read =
                    fread(file_buffer,
                          1,
                          sizeof(file_buffer),
                          fp)) > 0) {

                if (send_all(
                        client_fd,
                        file_buffer,
                        bytes_read) < 0) {

                    break;
                }
            }


            fclose(fp);


            printf(
                "File sent: %s (%ld bytes)\n",
                filename,
                filesize);


            char file_log[512];

            snprintf(
                file_log,
                sizeof(file_log),
                "File downloaded: %s (%ld bytes)",
                filename,
                filesize);

            log_activity(file_log);


            continue;
        }


//           MONITOR START

        if (strncmp(
                buffer,
                "MONITOR START ",
                14) == 0) {

            int udp_port;


            if (sscanf(
                    buffer,
                    "MONITOR START %d",
                    &udp_port) != 1 ||
                udp_port < 1 ||
                udp_port > 65535) {

                char response[] =
                    "ERR 013 INVALID_UDP_PORT SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            if (monitor.active) {

                char response[] =
                    "ERR 014 MONITOR_ALREADY_RUNNING SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            monitor.udp_socket =
                socket(AF_INET,
                       SOCK_DGRAM,
                       0);


            if (monitor.udp_socket < 0) {

                char response[] =
                    "ERR 015 UDP_SOCKET_FAILED SID:"
                    SID
                    "\n";

                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            memset(
                &monitor.destination,
                0,
                sizeof(
                    monitor.destination));


            monitor.destination.sin_family =
                AF_INET;


            monitor.destination.sin_addr =
                client_addr.sin_addr;


            monitor.destination.sin_port =
                htons(udp_port);


            atomic_store(
                &monitor.stop_requested,
                0);


            if (pthread_create(
                    &monitor.thread,
                    NULL,
                    monitor_thread,
                    &monitor) != 0) {

                close(
                    monitor.udp_socket);

                monitor.udp_socket =
                    -1;


                char response[] =
                    "ERR 016 MONITOR_START_FAILED SID:"
                    SID
                    "\n";


                send_all(
                    client_fd,
                    response,
                    strlen(response));


                continue;
            }


            monitor.active = 1;


            char response[] =
                "OK MONITOR_STARTED SID:"
                SID
                "\n";


            send_all(
                client_fd,
                response,
                strlen(response));


            log_activity(
                "UDP monitoring started");


            continue;
        }


//           MONITOR STOP

        if (strcmp(
                buffer,
                "MONITOR STOP") == 0) {

            stop_monitor(
                &monitor);


            char response[] =
                "OK MONITOR_STOPPED SID:"
                SID
                "\n";


            send_all(
                client_fd,
                response,
                strlen(response));


            log_activity(
                "UDP monitoring stopped");


            continue;
        }


//           QUIT

        if (strcmp(buffer,
                   "QUIT") == 0) {

            stop_monitor(
                &monitor);


            char response[] =
                "OK BYE SID:"
                SID
                "\n";


            send_all(
                client_fd,
                response,
                strlen(response));


            printf(
                "Controller ended the session.\n");


            log_activity(
                "Controller ended session with QUIT");


            break;
        }


//           UNKNOWN COMMAND

        char response[] =
            "ERR 006 UNKNOWN_COMMAND SID:"
            SID
            "\n";


        send_all(client_fd,
                 response,
                 strlen(response));
    }


    stop_monitor(&monitor);

    close(client_fd);
    close(server_fd);

    return 0;
}
