#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * Personalised values for IT24100615
 */
#define AGENT_PORT 9410
#define AUTH_TOKEN "OPS-0615"
#define SID "5160"

/*
 * General settings
 */
#define BACKLOG 5
#define RECEIVE_BUFFER_SIZE 4096
#define LINE_SIZE 1024
#define RESPONSE_SIZE 4096


/*
 * Stores TCP bytes that have already been received
 * but have not yet been returned as a complete line.
 */
typedef struct
{
    char buffer[RECEIVE_BUFFER_SIZE];
    size_t start;
    size_t end;
} LineReader;


/*
 * Send all bytes.
 *
 * send() may send fewer bytes than requested,
 * so this function keeps sending until all
 * bytes have been transmitted.
 */
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


/*
 * Send one text response.
 *
 * The response string should already contain \n.
 */
int send_response(int socket_fd, const char *response)
{
    return send_all(
        socket_fd,
        response,
        strlen(response)
    );
}


/*
 * Receive one complete newline-terminated line.
 *
 * Return:
 *  1 = complete line received
 *  0 = client disconnected
 * -1 = recv error
 * -2 = line too long
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
         * Process bytes that are already
         * stored in the receive buffer.
         */
        while (reader->start < reader->end)
        {
            char current_char =
                reader->buffer[reader->start++];

            /*
             * Newline marks the end of
             * one protocol command.
             */
            if (current_char == '\n')
            {
                /*
                 * Remove optional '\r'.
                 */
                if (line_length > 0 &&
                    line[line_length - 1] == '\r')
                {
                    line_length--;
                }

                line[line_length] = '\0';

                /*
                 * Reset indexes when the
                 * complete buffer was consumed.
                 */
                if (reader->start == reader->end)
                {
                    reader->start = 0;
                    reader->end = 0;
                }

                return 1;
            }

            /*
             * Keep one byte free for '\0'.
             */
            if (line_length + 1 >= line_size)
            {
                return -2;
            }

            line[line_length++] = current_char;
        }

        /*
         * Existing bytes have been processed.
         * Receive more bytes from TCP.
         */
        reader->start = 0;
        reader->end = 0;

        ssize_t bytes_received = recv(
            socket_fd,
            reader->buffer,
            sizeof(reader->buffer),
            0
        );

        if (bytes_received == 0)
        {
            return 0;
        }

        if (bytes_received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        reader->end = (size_t)bytes_received;
    }
}


/*
 * Read CPU/system load, memory usage and uptime
 * from Linux /proc files.
 */
int get_sysinfo(double *cpu_load,
                long *mem_used_mb,
                long *uptime_sec)
{
    FILE *file;

    /*
     * Read 1-minute load average.
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

    char mem_line[256];

    while (fgets(mem_line, sizeof(mem_line), file) != NULL)
    {
        long value;

        if (sscanf(mem_line, "MemTotal: %ld kB", &value) == 1)
        {
            mem_total_kb = value;
        }
        else if (sscanf(mem_line,
                        "MemAvailable: %ld kB",
                        &value) == 1)
        {
            mem_available_kb = value;
        }
    }

    fclose(file);

    if (mem_total_kb == 0)
    {
        return -1;
    }

    /*
     * If MemAvailable was not found,
     * fail rather than inventing a value.
     */
    if (mem_available_kb == 0)
    {
        return -1;
    }

    *mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;


    /*
     * Read uptime.
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


/*
 * Get a snapshot of running processes.
 *
 * Output format:
 * process_name/PID,process_name/PID,...
 */
int get_process_list(char *output, size_t output_size)
{
    FILE *pipe;
    char line[256];

    size_t used = 0;
    int first_process = 1;

    pipe = popen("ps -eo pid=,comm=", "r");

    if (pipe == NULL)
    {
        return -1;
    }

    output[0] = '\0';

    while (fgets(line, sizeof(line), pipe) != NULL)
    {
        int pid;
        char process_name[128];

        if (sscanf(
                line,
                "%d %127s",
                &pid,
                process_name) != 2)
        {
            continue;
        }

        char entry[180];

        if (first_process)
        {
            snprintf(
                entry,
                sizeof(entry),
                "%s/%d",
                process_name,
                pid
            );

            first_process = 0;
        }
        else
        {
            snprintf(
                entry,
                sizeof(entry),
                ",%s/%d",
                process_name,
                pid
            );
        }

        size_t entry_length = strlen(entry);

        if (used + entry_length + 1 >= output_size)
        {
            break;
        }

        memcpy(
            output + used,
            entry,
            entry_length
        );

        used += entry_length;
        output[used] = '\0';
    }

    pclose(pipe);

    return 0;
}


/*
 * EXEC whitelist.
 *
 * Only these five RemoteOps command names
 * are permitted.
 */
int get_exec_command(const char *name,
                     const char **system_command)
{
    if (strcmp(name, "DATE") == 0)
    {
        *system_command = "date";
    }
    else if (strcmp(name, "UPTIME") == 0)
    {
        *system_command = "uptime";
    }
    else if (strcmp(name, "DISKFREE") == 0)
    {
        *system_command = "df -h /";
    }
    else if (strcmp(name, "HOSTNAME") == 0)
    {
        *system_command = "hostname";
    }
    else if (strcmp(name, "WHOAMI") == 0)
    {
        *system_command = "whoami";
    }
    else
    {
        return -1;
    }

    return 0;
}


/*
 * Run one command that has already been
 * selected from the fixed whitelist.
 */
int run_allowed_command(const char *command,
                        char *output,
                        size_t output_size)
{
    FILE *pipe;
    char temp[256];

    size_t used = 0;

    pipe = popen(command, "r");

    if (pipe == NULL)
    {
        return -1;
    }

    output[0] = '\0';

    while (fgets(temp, sizeof(temp), pipe) != NULL)
    {
        size_t temp_length = strlen(temp);

        if (used + temp_length + 1 >= output_size)
        {
            break;
        }

        memcpy(
            output + used,
            temp,
            temp_length
        );

        used += temp_length;
        output[used] = '\0';
    }

    pclose(pipe);

    /*
     * RemoteOps text responses must be
     * one newline-terminated line.
     *
     * Replace command-output newlines
     * with spaces.
     */
    for (size_t i = 0; output[i] != '\0'; i++)
    {
        if (output[i] == '\n' ||
            output[i] == '\r')
        {
            output[i] = ' ';
        }
    }

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
     * Create IPv4 TCP socket.
     */
    server_socket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_socket < 0)
    {
        perror("socket");
        return 1;
    }


    /*
     * Configure Agent address.
     */
    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    server_address.sin_port =
        htons(AGENT_PORT);


    /*
     * Bind Agent to personalised port 9410.
     */
    if (bind(
            server_socket,
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
    printf(
        "Listening on TCP port %d...\n",
        AGENT_PORT
    );


    /*
     * Step 13 version accepts one Controller.
     * Concurrency will be implemented later.
     */
    client_address_length =
        sizeof(client_address);

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


    printf(
        "Controller connected from %s\n",
        inet_ntoa(client_address.sin_addr)
    );


    /*
     * State for this TCP connection.
     */
    LineReader reader = {0};

    char line[LINE_SIZE];

    int authenticated = 0;


    /*
     * Read complete protocol lines until
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
            printf(
                "Received complete line: %s\n",
                line
            );


            /*
             * AUTH
             */
            if (strncmp(line, "AUTH ", 5) == 0)
            {
                const char *token = line + 5;

                if (strcmp(token, AUTH_TOKEN) == 0)
                {
                    authenticated = 1;

                    if (send_response(
                            client_socket,
                            "OK AUTHENTICATED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "Authentication successful.\n"
                    );
                }
                else
                {
                    if (send_response(
                            client_socket,
                            "ERR 001 AUTH_FAILED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "Authentication failed.\n"
                    );
                }
            }


            /*
             * All non-AUTH commands are rejected
             * until authentication succeeds.
             */
            else if (!authenticated)
            {
                if (send_response(
                        client_socket,
                        "ERR 003 NOT_AUTHENTICATED SID:" SID "\n") < 0)
                {
                    perror("send");
                    break;
                }

                printf(
                    "Command rejected: client not authenticated.\n"
                );
            }


            /*
             * SYSINFO
             */
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
                        "OK SYSINFO %.2f %ld %ld SID:%s\n",
                        cpu_load,
                        mem_used_mb,
                        uptime_sec,
                        SID
                    );

                    if (send_response(
                            client_socket,
                            response) < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "SYSINFO sent successfully.\n"
                    );
                }
                else
                {
                    if (send_response(
                            client_socket,
                            "ERR 006 SYSINFO_FAILED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "Failed to read system information.\n"
                    );
                }
            }


            /*
             * LISTPROC
             */
            else if (strcmp(line, "LISTPROC") == 0)
            {
                char process_list[RESPONSE_SIZE];
                char response[RESPONSE_SIZE + 64];

                if (get_process_list(
                        process_list,
                        sizeof(process_list)) == 0)
                {
                    snprintf(
                        response,
                        sizeof(response),
                        "OK PROCS %s SID:%s\n",
                        process_list,
                        SID
                    );

                    if (send_response(
                            client_socket,
                            response) < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "LISTPROC sent successfully.\n"
                    );
                }
                else
                {
                    if (send_response(
                            client_socket,
                            "ERR 007 LISTPROC_FAILED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "Failed to read process list.\n"
                    );
                }
            }


            /*
             * EXEC
             */
            else if (strncmp(line, "EXEC ", 5) == 0)
            {
                const char *command_name =
                    line + 5;

                const char *system_command;


                /*
                 * First check the fixed whitelist.
                 */
                if (get_exec_command(
                        command_name,
                        &system_command) != 0)
                {
                    if (send_response(
                            client_socket,
                            "ERR 002 COMMAND_NOT_ALLOWED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        break;
                    }

                    printf(
                        "EXEC rejected: %s\n",
                        command_name
                    );
                }
                else
                {
                    char command_output[
                        RESPONSE_SIZE
                    ];

                    char response[
                        RESPONSE_SIZE + 64
                    ];


                    if (run_allowed_command(
                            system_command,
                            command_output,
                            sizeof(command_output)) == 0)
                    {
                        snprintf(
                            response,
                            sizeof(response),
                            "OK EXEC_RESULT %s SID:%s\n",
                            command_output,
                            SID
                        );

                        if (send_response(
                                client_socket,
                                response) < 0)
                        {
                            perror("send");
                            break;
                        }

                        printf(
                            "EXEC completed: %s\n",
                            command_name
                        );
                    }
                    else
                    {
                        if (send_response(
                                client_socket,
                                "ERR 008 EXEC_FAILED SID:" SID "\n") < 0)
                        {
                            perror("send");
                            break;
                        }

                        printf(
                            "EXEC failed: %s\n",
                            command_name
                        );
                    }
                }
            }


            /*
             * Commands not implemented yet.
             *
             * PUT, GET, MONITOR and QUIT will
             * be added in later steps.
             */
            else
            {
                if (send_response(
                        client_socket,
                        "ERR 003 COMMAND_NOT_IMPLEMENTED SID:" SID "\n") < 0)
                {
                    perror("send");
                    break;
                }

                printf(
                    "Authenticated command not implemented yet.\n"
                );
            }
        }


        /*
         * Controller closed the connection.
         */
        else if (result == 0)
        {
            printf(
                "Controller disconnected.\n"
            );

            break;
        }


        /*
         * Command line exceeded LINE_SIZE.
         */
        else if (result == -2)
        {
            printf(
                "Received line was too long.\n"
            );

            break;
        }


        /*
         * recv() error.
         */
        else
        {
            perror("recv");
            break;
        }
    }


    /*
     * Clean shutdown for this stage.
     */
    close(client_socket);
    close(server_socket);

    printf("Agent stopped.\n");

    return 0;
}
