#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define BUFFER_SIZE 16384

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

double get_cpu_load()
{
    FILE *fp;
    double load = 0.0;

    fp = fopen("/proc/loadavg", "r");

    if (fp == NULL) {
        return -1.0;
    }

    fscanf(fp, "%lf", &load);

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

        if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1) {
            continue;
        }

        if (sscanf(line, "MemAvailable: %ld kB", &mem_available) == 1) {
            continue;
        }
    }

    fclose(fp);

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

    fscanf(fp, "%lf", &uptime);

    fclose(fp);

    return (long)uptime;
}


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

        if (sscanf(line, "%d %127s", &pid, process_name) == 2) {

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

    // Remove final comma
    size_t len = strlen(output);

    if (len > 0 && output[len - 1] == ',') {
        output[len - 1] = '\0';
    }

    return 0;
}

void make_single_line(char *text)
{
    for (int i = 0; text[i] != '\0'; i++) {

        if (text[i] == '\n' || text[i] == '\r') {
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

    while (fgets(line, sizeof(line), fp) != NULL) {

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

	// SYSINFO command
    if (strcmp(buffer, "SYSINFO") == 0) {

    double cpu_load = get_cpu_load();
    long mem_used_mb = get_memory_used_mb();
    long uptime_sec = get_uptime_seconds();

    char response[BUFFER_SIZE];

    snprintf(response,
             sizeof(response),
             "OK SYSINFO %.2f %ld %ld SID:%s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SID);

    send(client_fd,
         response,
         strlen(response),
         0);

    continue;
    }


     // LISTPROC command
    if (strcmp(buffer, "LISTPROC") == 0) {

    char processes[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    if (get_process_list(processes,
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

    send(client_fd,
         response,
         strlen(response),
         0);

    continue;
  }


// EXEC command
if (strncmp(buffer, "EXEC ", 5) == 0) {

    char *command_name = buffer + 5;

    char command_output[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    int result =
        execute_allowed_command(command_name,
                                command_output,
                                sizeof(command_output));

    if (result == 1) {

        snprintf(response,
                 sizeof(response),
                 "OK EXEC_RESULT %s SID:%s\n",
                 command_output,
                 SID);
    }
    else if (result == 0) {

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
