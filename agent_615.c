#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_PORT 9410
#define BACKLOG 5

#define RECEIVE_BUFFER_SIZE 4096
#define LINE_SIZE 1024

#define AUTH_TOKEN "OPS-0615"
#define SID "5160"
/*
 * Stores TCP bytes that have been received but
 * have not yet been returned as a complete line.
 */
typedef struct
{
    char buffer[RECEIVE_BUFFER_SIZE];
    size_t start;
    size_t end;
} LineReader;


/*
 * Receives one complete newline-terminated line.
 *
 * Return values:
 *  1  = complete line received
 *  0  = client disconnected
 * -1  = recv() error
 * -2  = line was too long
 */
int receive_line(int socket_fd,
                 LineReader *reader,
                 char *line,
                 size_t line_size)
{
    size_t line_length = 0;

    while (1)
    {
        /*
         * First use any bytes already stored
         * in our receive buffer.
         */
        while (reader->start < reader->end)
        {
            char current_char = reader->buffer[reader->start++];

            /*
             * A newline means one complete
             * protocol line has been received.
             */
            if (current_char == '\n')
            {
                /*
                 * Remove optional carriage return.
                 * This also makes testing with some
                 * terminal programs easier.
                 */
                if (line_length > 0 &&
                    line[line_length - 1] == '\r')
                {
                    line_length--;
                }

                line[line_length] = '\0';

                /*
                 * If all buffered data was used,
                 * reset the indexes.
                 */
                if (reader->start == reader->end)
                {
                    reader->start = 0;
                    reader->end = 0;
                }

                return 1;
            }

            /*
             * Leave one byte for '\0'.
             */
            if (line_length + 1 >= line_size)
            {
                return -2;
            }

            line[line_length++] = current_char;
        }

        /*
         * Existing buffered bytes have been used.
         * Receive another block from TCP.
         */
        reader->start = 0;
        reader->end = 0;

        ssize_t bytes_received =
            recv(socket_fd,
                 reader->buffer,
                 sizeof(reader->buffer),
                 0);

        if (bytes_received == 0)
        {
            /*
             * The Controller closed its TCP
             * connection.
             */
            return 0;
        }

        if (bytes_received < 0)
        {
            /*
             * Retry if recv() was interrupted
             * by a signal.
             */
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        reader->end = (size_t)bytes_received;
    }
}

int send_all(int socket_fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t bytes_sent = send(
            socket_fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (bytes_sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (bytes_sent == 0)
        {
            return -1;
        }

        total_sent += (size_t)bytes_sent;
    }

    return 0;
}

int send_response(int socket_fd, const char *response)
{
    return send_all(socket_fd, response, strlen(response));
}

int get_sysinfo(double *cpu_load,
                long *mem_used_mb,
                long *uptime_sec)
{
    FILE *file;

    /*
     * Read the 1-minute system load average.
     */
    file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return -1;
    }

    if (fscanf(file, "%lf", cpu_load) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    /*
     * Read memory information.
     */
    file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return -1;
    }

    long mem_total_kb = 0;
    long mem_available_kb = 0;

    char key[64];
    long value;
    char unit[32];

    while (fscanf(file, "%63s %ld %31s", key, &value, unit) == 3)
    {
        if (strcmp(key, "MemTotal:") == 0)
        {
            mem_total_kb = value;
        }
        else if (strcmp(key, "MemAvailable:") == 0)
        {
            mem_available_kb = value;
        }
    }

    fclose(file);

    if (mem_total_kb == 0)
    {
        return -1;
    }

    *mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    /*
     * Read system uptime.
     */
    file = fopen("/proc/uptime", "r");

    if (file == NULL)
    {
        return -1;
    }

    double uptime;

    if (fscanf(file, "%lf", &uptime) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    *uptime_sec = (long)uptime;

    return 0;
}

int main(void)
{
    int server_socket;
    int client_socket;

    struct sockaddr_in server_address;
    struct sockaddr_in client_address;

    socklen_t client_address_length;

    /*
     * Create an IPv4 TCP socket.
     */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(AGENT_PORT);

    /*
     * Bind to personalised port 9410.
     */
    if (bind(server_socket,
             (struct sockaddr *)&server_address,
             sizeof(server_address)) < 0)
    {
        perror("bind");
        close(server_socket);
        return 1;
    }

    /*
     * Start listening.
     */
    if (listen(server_socket, BACKLOG) < 0)
    {
        perror("listen");
        close(server_socket);
        return 1;
    }

    printf("RemoteOps Agent - IT24100615\n");
    printf("Listening on TCP port %d...\n", AGENT_PORT);

    client_address_length = sizeof(client_address);

    /*
     * Accept one connection for this stage.
     */
    client_socket = accept(
        server_socket,
        (struct sockaddr *)&client_address,
        &client_address_length
    );

    if (client_socket < 0)
    {
        perror("accept");
        close(server_socket);
        return 1;
    }

    printf("Controller connected from %s\n",
           inet_ntoa(client_address.sin_addr));

    /*
     * Each connection has its own line buffer.
     */
    LineReader reader = {0};
    char line[LINE_SIZE];

    int authenticated = 0;

    /*
     * Continue reading complete lines until
     * the Controller disconnects.
     */
    while (1)
    {
        int result = receive_line(
            client_socket,
            &reader,
            line,
            sizeof(line)
        );

        if (result == 1)
        {
            if (result == 1)
{
    printf("Received complete line: %s\n", line);

    /*
     * AUTH command
     */
    if (strncmp(line, "AUTH ", 5) == 0)
    {
        const char *token = line + 5;

        if (strcmp(token, AUTH_TOKEN) == 0)
        {
            authenticated = 1;

            if (send_response(
                    client_socket,
                    "OK AUTHENTICATED SID:5160\n") < 0)
            {
                perror("send");
                break;
            }

            printf("Authentication successful.\n");
        }
        else
        {
            if (send_response(
                    client_socket,
                    "ERR 001 AUTH_FAILED SID:5160\n") < 0)
            {
                perror("send");
                break;
            }

            printf("Authentication failed.\n");
        }
    }

    /*
     * Reject commands before AUTH succeeds.
     */
    else if (!authenticated)
    {
        if (send_response(
                client_socket,
                "ERR 003 NOT_AUTHENTICATED SID:5160\n") < 0)
        {
            perror("send");
            break;
        }

        printf("Command rejected: client not authenticated.\n");
    }
    else if (strcmp(line, "SYSINFO") == 0)
{
    double cpu_load;
    long mem_used_mb;
    long uptime_sec;

    if (get_sysinfo(
            &cpu_load,
            &mem_used_mb,
            &uptime_sec) == 0)
    {
        char response[256];

        snprintf(
            response,
            sizeof(response),
            "OK SYSINFO %.2f %ld %ld SID:5160\n",
            cpu_load,
            mem_used_mb,
            uptime_sec
        );

        if (send_response(client_socket, response) < 0)
        {
            perror("send");
            break;
        }

        printf("SYSINFO sent successfully.\n");
    }
    else
    {
        if (send_response(
                client_socket,
                "ERR 006 SYSINFO_FAILED SID:5160\n") < 0)
        {
            perror("send");
            break;
        }

        printf("Failed to read system information.\n");
    }
}
    /*
     * Other commands will be implemented later.
     */
    else
    {
        if (send_response(
                client_socket,
                "ERR 003 COMMAND_NOT_IMPLEMENTED SID:5160\n") < 0)
        {
            perror("send");
            break;
        }

        printf("Authenticated command not implemented yet.\n");
    }
}
        }
        else if (result == 0)
        {
            printf("Controller disconnected.\n");
            break;
        }
        else if (result == -2)
        {
            printf("Received line was too long.\n");
            break;
        }
        else
        {
            perror("recv");
            break;
        }
    }

    close(client_socket);
    close(server_socket);

    printf("Agent stopped.\n");

    return 0;
}
