#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>

#include <netinet/in.h>
#include <arpa/inet.h>

/* Personalised values */
#define AGENT_PORT 9410
#define AUTH_TOKEN "OPS-0615"
#define SID "5160"
#define LOG_FILE "remoteops_IT24100615.log"
#define STORAGE_DIR "./agentfiles/IT24100615"

/* General settings */
#define BACKLOG 5
#define RECEIVE_BUFFER_SIZE 4096
#define LINE_SIZE 1024
#define RESPONSE_SIZE 4096
#define MAX_FILE_SIZE (10 * 1024 * 1024)
#define MONITOR_INTERVAL 2


typedef struct
{
    char buffer[RECEIVE_BUFFER_SIZE];
    size_t start;
    size_t end;
} LineReader;


/* ---------------------------------------------------------
   Logging
   --------------------------------------------------------- */

void log_event(const char *format, ...)
{
    FILE *log_file;
    time_t current_time;
    struct tm time_info;
    char timestamp[64];

    log_file = fopen(LOG_FILE, "a");

    if (log_file == NULL)
    {
        perror("log file");
        return;
    }

    flock(fileno(log_file), LOCK_EX);

    current_time = time(NULL);

    if (localtime_r(&current_time, &time_info) == NULL)
    {
        flock(fileno(log_file), LOCK_UN);
        fclose(log_file);
        return;
    }

    strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%d %H:%M:%S",
        &time_info
    );

    fprintf(log_file, "[%s] ", timestamp);

    va_list arguments;

    va_start(arguments, format);
    vfprintf(log_file, format, arguments);
    va_end(arguments);

    fprintf(log_file, "\n");
    fflush(log_file);

    flock(fileno(log_file), LOCK_UN);

    fclose(log_file);
}


/* ---------------------------------------------------------
   TCP helpers
   --------------------------------------------------------- */

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
    return send_all(
        socket_fd,
        response,
        strlen(response)
    );
}


int receive_line(int socket_fd,
                 LineReader *reader,
                 char *line,
                 size_t line_size)
{
    size_t line_length = 0;

    while (1)
    {
        while (reader->start < reader->end)
        {
            char current_char =
                reader->buffer[reader->start++];

            if (current_char == '\n')
            {
                if (line_length > 0 &&
                    line[line_length - 1] == '\r')
                {
                    line_length--;
                }

                line[line_length] = '\0';

                if (reader->start == reader->end)
                {
                    reader->start = 0;
                    reader->end = 0;
                }

                return 1;
            }

            if (line_length + 1 >= line_size)
            {
                return -2;
            }

            line[line_length++] = current_char;
        }

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


/* ---------------------------------------------------------
   SYSINFO
   --------------------------------------------------------- */

int get_sysinfo(double *cpu_load,
                long *mem_used_mb,
                long *uptime_sec)
{
    FILE *file;

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
        else if (sscanf(
                     mem_line,
                     "MemAvailable: %ld kB",
                     &value) == 1)
        {
            mem_available_kb = value;
        }
    }

    fclose(file);

    if (mem_total_kb == 0 ||
        mem_available_kb == 0)
    {
        return -1;
    }

    *mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

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


/* ---------------------------------------------------------
   LISTPROC
   --------------------------------------------------------- */

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


/* ---------------------------------------------------------
   EXEC
   --------------------------------------------------------- */

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


/* ---------------------------------------------------------
   PUT
   --------------------------------------------------------- */

int valid_filename(const char *filename)
{
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }

    if (strchr(filename, '/') != NULL)
    {
        return 0;
    }

    if (strstr(filename, "..") != NULL)
    {
        return 0;
    }

    return 1;
}


int receive_file_bytes(int socket_fd,
                       LineReader *reader,
                       FILE *file,
                       long file_size)
{
    long total_received = 0;

    while (reader->start < reader->end &&
           total_received < file_size)
    {
        long remaining =
            file_size - total_received;

        size_t buffered =
            reader->end - reader->start;

        size_t to_write = buffered;

        if ((long)to_write > remaining)
        {
            to_write = (size_t)remaining;
        }

        if (fwrite(
                reader->buffer + reader->start,
                1,
                to_write,
                file) != to_write)
        {
            return -1;
        }

        reader->start += to_write;
        total_received += (long)to_write;
    }

    if (reader->start == reader->end)
    {
        reader->start = 0;
        reader->end = 0;
    }

    char buffer[4096];

    while (total_received < file_size)
    {
        long remaining =
            file_size - total_received;

        size_t wanted = sizeof(buffer);

        if ((long)wanted > remaining)
        {
            wanted = (size_t)remaining;
        }

        ssize_t bytes_received = recv(
            socket_fd,
            buffer,
            wanted,
            0
        );

        if (bytes_received == 0)
        {
            return -1;
        }

        if (bytes_received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (fwrite(
                buffer,
                1,
                (size_t)bytes_received,
                file) != (size_t)bytes_received)
        {
            return -1;
        }

        total_received += bytes_received;
    }

    return 0;
}


/* ---------------------------------------------------------
   GET
   --------------------------------------------------------- */

int send_file_bytes(int socket_fd,
                    FILE *file,
                    long file_size)
{
    char buffer[4096];
    long total_sent = 0;

    while (total_sent < file_size)
    {
        long remaining =
            file_size - total_sent;

        size_t to_read = sizeof(buffer);

        if ((long)to_read > remaining)
        {
            to_read = (size_t)remaining;
        }

        size_t bytes_read = fread(
            buffer,
            1,
            to_read,
            file
        );

        if (bytes_read == 0)
        {
            return -1;
        }

        if (send_all(
                socket_fd,
                buffer,
                bytes_read) < 0)
        {
            return -1;
        }

        total_sent += (long)bytes_read;
    }

    return 0;
}


/* ---------------------------------------------------------
   UDP monitoring
   --------------------------------------------------------- */

void run_udp_monitor(const char *controller_ip,
                     int udp_port)
{
    int udp_socket;
    struct sockaddr_in udp_address;

    udp_socket = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (udp_socket < 0)
    {
        exit(1);
    }

    memset(
        &udp_address,
        0,
        sizeof(udp_address)
    );

    udp_address.sin_family = AF_INET;
    udp_address.sin_port =
        htons((unsigned short)udp_port);

    if (inet_pton(
            AF_INET,
            controller_ip,
            &udp_address.sin_addr) <= 0)
    {
        close(udp_socket);
        exit(1);
    }

    while (1)
    {
        double cpu_load;
        long mem_used_mb;
        long uptime_sec;

        if (get_sysinfo(
                &cpu_load,
                &mem_used_mb,
                &uptime_sec) == 0)
        {
            char message[256];

            snprintf(
                message,
                sizeof(message),
                "SYSINFO %.2f %ld %ld SID:%s",
                cpu_load,
                mem_used_mb,
                uptime_sec,
                SID
            );

            sendto(
                udp_socket,
                message,
                strlen(message),
                0,
                (struct sockaddr *)&udp_address,
                sizeof(udp_address)
            );
        }

        sleep(MONITOR_INTERVAL);
    }
}


/* ---------------------------------------------------------
   Main
   --------------------------------------------------------- */

int main(void)
{
    int server_socket;
    int client_socket;

    struct sockaddr_in server_address;
    struct sockaddr_in client_address;

    socklen_t client_address_length;

    signal(SIGPIPE, SIG_IGN);

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

    if (bind(
            server_socket,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) < 0)
    {
        perror("bind");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, BACKLOG) < 0)
    {
        perror("listen");
        close(server_socket);
        return 1;
    }

    printf(
        "RemoteOps Agent - IT24100615\n"
    );

    printf(
        "Listening on TCP port %d...\n",
        AGENT_PORT
    );

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

    const char *client_ip =
        inet_ntoa(client_address.sin_addr);

    printf(
        "Controller connected from %s\n",
        client_ip
    );

    log_event(
        "Controller connected from %s",
        client_ip
    );

    LineReader reader = {0};

    char line[LINE_SIZE];

    int authenticated = 0;

    pid_t monitor_pid = -1;

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

            if (strncmp(line, "AUTH ", 5) == 0)
            {
                log_event(
                    "Command received: AUTH <redacted>"
                );
            }
            else
            {
                log_event(
                    "Command received: %s",
                    line
                );
            }


            /* AUTH */
            if (strncmp(line, "AUTH ", 5) == 0)
            {
                const char *token =
                    line + 5;

                if (strcmp(
                        token,
                        AUTH_TOKEN) == 0)
                {
                    authenticated = 1;

                    if (send_response(
                            client_socket,
                            "OK AUTHENTICATED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        log_event(
                            "Send failed after successful authentication"
                        );
                        break;
                    }

                    printf(
                        "Authentication successful.\n"
                    );

                    log_event(
                        "Authentication successful"
                    );
                }
                else
                {
                    if (send_response(
                            client_socket,
                            "ERR 001 AUTH_FAILED SID:" SID "\n") < 0)
                    {
                        perror("send");
                        log_event(
                            "Send failed after failed authentication"
                        );
                        break;
                    }

                    printf(
                        "Authentication failed.\n"
                    );

                    log_event(
                        "Authentication failed"
                    );
                }
            }


            /* Require AUTH first */
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

                log_event(
                    "Command rejected: client not authenticated"
                );
            }


            /* SYSINFO */
            else if (strcmp(
                         line,
                         "SYSINFO") == 0)
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
                    send_response(
                        client_socket,
                        "ERR 006 SYSINFO_FAILED SID:" SID "\n"
                    );
                }
            }


            /* LISTPROC */
            else if (strcmp(
                         line,
                         "LISTPROC") == 0)
            {
                char process_list[
                    RESPONSE_SIZE
                ];

                char response[
                    RESPONSE_SIZE + 64
                ];

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
                    send_response(
                        client_socket,
                        "ERR 007 LISTPROC_FAILED SID:" SID "\n"
                    );
                }
            }


            /* EXEC */
            else if (strncmp(
                         line,
                         "EXEC ",
                         5) == 0)
            {
                const char *command_name =
                    line + 5;

                const char *system_command;

                if (get_exec_command(
                        command_name,
                        &system_command) != 0)
                {
                    send_response(
                        client_socket,
                        "ERR 002 COMMAND_NOT_ALLOWED SID:" SID "\n"
                    );

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
                        send_response(
                            client_socket,
                            "ERR 008 EXEC_FAILED SID:" SID "\n"
                        );
                    }
                }
            }


            /* PUT */
            else if (strncmp(
                         line,
                         "PUT ",
                         4) == 0)
            {
                char filename[256];
                long file_size;

                if (sscanf(
                        line,
                        "PUT %255s %ld",
                        filename,
                        &file_size) != 2)
                {
                    send_response(
                        client_socket,
                        "ERR 009 INVALID_PUT_FORMAT SID:" SID "\n"
                    );

                    continue;
                }

                if (!valid_filename(filename))
                {
                    send_response(
                        client_socket,
                        "ERR 009 INVALID_FILENAME SID:" SID "\n"
                    );

                    continue;
                }

                if (file_size < 0 ||
                    file_size > MAX_FILE_SIZE)
                {
                    send_response(
                        client_socket,
                        "ERR 004 FILE_TOO_LARGE SID:" SID "\n"
                    );

                    log_event(
                        "PUT rejected: %s (%ld bytes)",
                        filename,
                        file_size
                    );

                    break;
                }

                mkdir("agentfiles", 0755);
                mkdir(STORAGE_DIR, 0755);

                char file_path[512];

                snprintf(
                    file_path,
                    sizeof(file_path),
                    "%s/%s",
                    STORAGE_DIR,
                    filename
                );

                FILE *file =
                    fopen(file_path, "wb");

                if (file == NULL)
                {
                    send_response(
                        client_socket,
                        "ERR 010 FILE_OPEN_FAILED SID:" SID "\n"
                    );

                    continue;
                }

                if (receive_file_bytes(
                        client_socket,
                        &reader,
                        file,
                        file_size) != 0)
                {
                    fclose(file);
                    remove(file_path);

                    log_event(
                        "PUT failed while receiving %s",
                        filename
                    );

                    break;
                }

                fclose(file);

                char response[512];

                snprintf(
                    response,
                    sizeof(response),
                    "OK FILE_RECEIVED %s SID:%s\n",
                    filename,
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
                    "PUT completed: %s (%ld bytes)\n",
                    filename,
                    file_size
                );

                log_event(
                    "PUT completed: %s (%ld bytes)",
                    filename,
                    file_size
                );
            }


            /* GET */
            else if (strncmp(
                         line,
                         "GET ",
                         4) == 0)
            {
                const char *filename =
                    line + 4;

                if (!valid_filename(filename))
                {
                    send_response(
                        client_socket,
                        "ERR 005 FILE_NOT_FOUND SID:" SID "\n"
                    );

                    continue;
                }

                char file_path[512];

                snprintf(
                    file_path,
                    sizeof(file_path),
                    "%s/%s",
                    STORAGE_DIR,
                    filename
                );

                FILE *file =
                    fopen(file_path, "rb");

                if (file == NULL)
                {
                    send_response(
                        client_socket,
                        "ERR 005 FILE_NOT_FOUND SID:" SID "\n"
                    );

                    printf(
                        "GET failed: file not found: %s\n",
                        filename
                    );

                    log_event(
                        "GET failed: file not found: %s",
                        filename
                    );

                    continue;
                }

                if (fseek(
                        file,
                        0,
                        SEEK_END) != 0)
                {
                    fclose(file);
                    break;
                }

                long file_size =
                    ftell(file);

                if (file_size < 0)
                {
                    fclose(file);
                    break;
                }

                rewind(file);

                char response[512];

                snprintf(
                    response,
                    sizeof(response),
                    "OK FILE_SEND %s %ld SID:%s\n",
                    filename,
                    file_size,
                    SID
                );

                if (send_response(
                        client_socket,
                        response) < 0)
                {
                    fclose(file);
                    perror("send");
                    break;
                }

                if (send_file_bytes(
                        client_socket,
                        file,
                        file_size) < 0)
                {
                    fclose(file);

                    log_event(
                        "GET failed while sending %s",
                        filename
                    );

                    break;
                }

                fclose(file);

                printf(
                    "GET completed: %s (%ld bytes)\n",
                    filename,
                    file_size
                );

                log_event(
                    "GET completed: %s (%ld bytes)",
                    filename,
                    file_size
                );
            }


            /* MONITOR START */
            else if (strncmp(
                         line,
                         "MONITOR START ",
                         14) == 0)
            {
                int udp_port;

                if (sscanf(
                        line,
                        "MONITOR START %d",
                        &udp_port) != 1 ||
                    udp_port < 1 ||
                    udp_port > 65535)
                {
                    send_response(
                        client_socket,
                        "ERR 011 INVALID_UDP_PORT SID:" SID "\n"
                    );

                    continue;
                }

                if (monitor_pid > 0)
                {
                    kill(
                        monitor_pid,
                        SIGTERM
                    );

                    waitpid(
                        monitor_pid,
                        NULL,
                        0
                    );

                    monitor_pid = -1;
                }

                char controller_ip[
                    INET_ADDRSTRLEN
                ];

                if (inet_ntop(
                        AF_INET,
                        &client_address.sin_addr,
                        controller_ip,
                        sizeof(controller_ip)) == NULL)
                {
                    send_response(
                        client_socket,
                        "ERR 012 MONITOR_FAILED SID:" SID "\n"
                    );

                    continue;
                }

                monitor_pid = fork();

                if (monitor_pid < 0)
                {
                    send_response(
                        client_socket,
                        "ERR 012 MONITOR_FAILED SID:" SID "\n"
                    );

                    monitor_pid = -1;
                    continue;
                }

                if (monitor_pid == 0)
                {
                    close(client_socket);
                    close(server_socket);

                    run_udp_monitor(
                        controller_ip,
                        udp_port
                    );

                    exit(0);
                }

                if (send_response(
                        client_socket,
                        "OK MONITOR_STARTED SID:" SID "\n") < 0)
                {
                    perror("send");
                    break;
                }

                printf(
                    "UDP monitoring started for %s:%d\n",
                    controller_ip,
                    udp_port
                );

                log_event(
                    "UDP monitoring started for %s:%d",
                    controller_ip,
                    udp_port
                );
            }


            /* MONITOR STOP */
            else if (strcmp(
                         line,
                         "MONITOR STOP") == 0)
            {
                if (monitor_pid > 0)
                {
                    kill(
                        monitor_pid,
                        SIGTERM
                    );

                    waitpid(
                        monitor_pid,
                        NULL,
                        0
                    );

                    monitor_pid = -1;

                    printf(
                        "UDP monitoring stopped.\n"
                    );

                    log_event(
                        "UDP monitoring stopped"
                    );
                }

                if (send_response(
                        client_socket,
                        "OK MONITOR_STOPPED SID:" SID "\n") < 0)
                {
                    perror("send");
                    break;
                }
            }


            /* QUIT */
            else if (strcmp(
                         line,
                         "QUIT") == 0)
            {
                if (monitor_pid > 0)
                {
                    kill(
                        monitor_pid,
                        SIGTERM
                    );

                    waitpid(
                        monitor_pid,
                        NULL,
                        0
                    );

                    monitor_pid = -1;

                    printf(
                        "UDP monitoring stopped before QUIT.\n"
                    );

                    log_event(
                        "UDP monitoring stopped during QUIT"
                    );
                }

                if (send_response(
                        client_socket,
                        "OK BYE SID:" SID "\n") < 0)
                {
                    perror("send");
                }

                printf(
                    "QUIT received. Closing connection.\n"
                );

                log_event(
                    "QUIT received; connection closing cleanly"
                );

                break;
            }


            /* Other commands */
            else
            {
                if (send_response(
                        client_socket,
                        "ERR 003 COMMAND_NOT_IMPLEMENTED SID:" SID "\n") < 0)
                {
                    perror("send");
                    break;
                }

                log_event(
                    "Unknown authenticated command: %s",
                    line
                );
            }
        }


        /* Clean client disconnect */
        else if (result == 0)
        {
            if (monitor_pid > 0)
            {
                kill(
                    monitor_pid,
                    SIGTERM
                );

                waitpid(
                    monitor_pid,
                    NULL,
                    0
                );

                monitor_pid = -1;

                log_event(
                    "UDP monitoring stopped because client disconnected"
                );
            }

            printf(
                "Controller disconnected.\n"
            );

            log_event(
                "Controller disconnected"
            );

            break;
        }


        /* Line too long */
        else if (result == -2)
        {
            printf(
                "Received line was too long.\n"
            );

            log_event(
                "Connection closed because command line was too long"
            );

            break;
        }


        /* recv error */
        else
        {
            perror("recv");

            log_event(
                "Controller connection ended with recv error"
            );

            break;
        }
    }


    if (monitor_pid > 0)
    {
        kill(
            monitor_pid,
            SIGTERM
        );

        waitpid(
            monitor_pid,
            NULL,
            0
        );
    }

    close(client_socket);
    close(server_socket);

    log_event(
        "Session cleanup completed"
    );

    printf(
        "Agent stopped.\n"
    );

    return 0;
}
