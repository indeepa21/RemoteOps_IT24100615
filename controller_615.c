#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_IP "127.0.0.1"
#define AGENT_PORT 9410

#define BUFFER_SIZE 4096
#define LINE_SIZE 1024


typedef struct
{
    char buffer[BUFFER_SIZE];
    size_t start;
    size_t end;

} LineReader;


/* ---------------------------------------------------------
   Send all TCP bytes
   --------------------------------------------------------- */

int send_all(int socket_fd,
             const char *data,
             size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            socket_fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}


/* ---------------------------------------------------------
   Reliable newline-framed receive
   --------------------------------------------------------- */

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
            char c =
                reader->buffer[reader->start++];

            if (c == '\n')
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

            line[line_length++] = c;
        }


        reader->start = 0;
        reader->end = 0;


        ssize_t received = recv(
            socket_fd,
            reader->buffer,
            sizeof(reader->buffer),
            0
        );

        if (received == 0)
        {
            return 0;
        }

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        reader->end = (size_t)received;
    }
}


/* ---------------------------------------------------------
   PUT: send exact file bytes
   --------------------------------------------------------- */

int send_file(int socket_fd,
              FILE *file,
              long file_size)
{
    char buffer[4096];
    long total_sent = 0;

    while (total_sent < file_size)
    {
        long remaining =
            file_size - total_sent;

        size_t wanted = sizeof(buffer);

        if ((long)wanted > remaining)
        {
            wanted = (size_t)remaining;
        }

        size_t read_count =
            fread(buffer, 1, wanted, file);

        if (read_count == 0)
        {
            return -1;
        }

        if (send_all(
                socket_fd,
                buffer,
                read_count) < 0)
        {
            return -1;
        }

        total_sent += (long)read_count;
    }

    return 0;
}


/* ---------------------------------------------------------
   GET: receive exact raw bytes
   --------------------------------------------------------- */

int receive_file(int socket_fd,
                 LineReader *reader,
                 FILE *file,
                 long file_size)
{
    long total_received = 0;


    /*
     * First use raw bytes that may already
     * be inside the TCP line-reader buffer.
     */
    while (reader->start < reader->end &&
           total_received < file_size)
    {
        size_t available =
            reader->end - reader->start;

        long remaining =
            file_size - total_received;

        size_t amount = available;

        if ((long)amount > remaining)
        {
            amount = (size_t)remaining;
        }


        if (fwrite(
                reader->buffer + reader->start,
                1,
                amount,
                file) != amount)
        {
            return -1;
        }


        reader->start += amount;

        total_received += (long)amount;
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


        ssize_t received = recv(
            socket_fd,
            buffer,
            wanted,
            0
        );


        if (received == 0)
        {
            return -1;
        }


        if (received < 0)
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
                (size_t)received,
                file) !=
            (size_t)received)
        {
            return -1;
        }


        total_received += received;
    }


    return 0;
}


/* ---------------------------------------------------------
   UDP monitoring receiver
   --------------------------------------------------------- */

void receive_udp_monitor(int udp_socket)
{
    char buffer[1024];

    printf(
        "\nUDP monitoring receiver started.\n"
    );

    while (1)
    {
        ssize_t received = recvfrom(
            udp_socket,
            buffer,
            sizeof(buffer) - 1,
            0,
            NULL,
            NULL
        );


        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }


        buffer[received] = '\0';


        printf(
            "\n[UDP] %s\n",
            buffer
        );

        fflush(stdout);
    }


    close(udp_socket);

    exit(0);
}


/* ---------------------------------------------------------
   Main Controller
   --------------------------------------------------------- */

int main(void)
{
    int socket_fd;

    struct sockaddr_in agent_address;

    LineReader reader = {0};

    char input[LINE_SIZE];
    char response[BUFFER_SIZE];

    pid_t monitor_pid = -1;


    signal(SIGPIPE, SIG_IGN);


    printf(
        "RemoteOps Controller - IT24100615\n"
    );


    socket_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );


    if (socket_fd < 0)
    {
        perror("socket");

        return 1;
    }


    memset(
        &agent_address,
        0,
        sizeof(agent_address)
    );


    agent_address.sin_family =
        AF_INET;

    agent_address.sin_port =
        htons(AGENT_PORT);


    if (inet_pton(
            AF_INET,
            AGENT_IP,
            &agent_address.sin_addr) <= 0)
    {
        fprintf(
            stderr,
            "Invalid Agent IP.\n"
        );

        close(socket_fd);

        return 1;
    }


    printf(
        "Connecting to Agent at %s:%d...\n",
        AGENT_IP,
        AGENT_PORT
    );


    if (connect(
            socket_fd,
            (struct sockaddr *)&agent_address,
            sizeof(agent_address)) < 0)
    {
        perror("connect");

        close(socket_fd);

        return 1;
    }


    printf(
        "Connected successfully.\n"
    );


    printf(
        "\nCommands:\n"
        "AUTH OPS-0615\n"
        "SYSINFO\n"
        "LISTPROC\n"
        "EXEC DATE\n"
        "EXEC UPTIME\n"
        "EXEC DISKFREE\n"
        "EXEC HOSTNAME\n"
        "EXEC WHOAMI\n"
        "PUT <local_file>\n"
        "GET <filename>\n"
        "MONITOR START <udp_port>\n"
        "MONITOR STOP\n"
        "QUIT\n\n"
    );


    while (1)
    {
        printf("remoteops> ");

        fflush(stdout);


        if (fgets(
                input,
                sizeof(input),
                stdin) == NULL)
        {
            break;
        }


        input[
            strcspn(input, "\r\n")
        ] = '\0';


        if (input[0] == '\0')
        {
            continue;
        }


        /* =================================================
           PUT
           User interface:
           PUT <local_file>

           Wire protocol:
           PUT <filename> <filesize>\n
           followed by exact raw bytes.
           ================================================= */

        if (strncmp(
                input,
                "PUT ",
                4) == 0)
        {
            const char *local_path =
                input + 4;


            FILE *file =
                fopen(local_path, "rb");


            if (file == NULL)
            {
                perror("PUT fopen");

                continue;
            }


            if (fseek(
                    file,
                    0,
                    SEEK_END) != 0)
            {
                fclose(file);

                printf(
                    "Could not read file size.\n"
                );

                continue;
            }


            long file_size =
                ftell(file);


            if (file_size < 0)
            {
                fclose(file);

                continue;
            }


            rewind(file);


            /*
             * Send only the filename to Agent,
             * not the whole local path.
             */
            const char *filename =
                strrchr(local_path, '/');


            if (filename != NULL)
            {
                filename++;
            }

            else
            {
                filename = local_path;
            }


            char header[LINE_SIZE];


            snprintf(
                header,
                sizeof(header),
                "PUT %s %ld\n",
                filename,
                file_size
            );


            if (send_all(
                    socket_fd,
                    header,
                    strlen(header)) < 0)
            {
                perror("send");

                fclose(file);

                break;
            }


            if (send_file(
                    socket_fd,
                    file,
                    file_size) < 0)
            {
                printf(
                    "PUT file transfer failed.\n"
                );

                fclose(file);

                break;
            }


            fclose(file);


            int result =
                receive_line(
                    socket_fd,
                    &reader,
                    response,
                    sizeof(response)
                );


            if (result != 1)
            {
                printf(
                    "Failed to receive PUT response.\n"
                );

                break;
            }


            printf(
                "%s\n",
                response
            );


            continue;
        }


        /* =================================================
           GET
           ================================================= */

        if (strncmp(
                input,
                "GET ",
                4) == 0)
        {
            const char *filename =
                input + 4;


            char command[LINE_SIZE];


            snprintf(
                command,
                sizeof(command),
                "GET %s\n",
                filename
            );


            if (send_all(
                    socket_fd,
                    command,
                    strlen(command)) < 0)
            {
                perror("send");

                break;
            }


            int result =
                receive_line(
                    socket_fd,
                    &reader,
                    response,
                    sizeof(response)
                );


            if (result != 1)
            {
                printf(
                    "Failed to receive GET response.\n"
                );

                break;
            }


            printf(
                "%s\n",
                response
            );


            char received_filename[256];

            long file_size;


            if (sscanf(
                    response,
                    "OK FILE_SEND %255s %ld",
                    received_filename,
                    &file_size) == 2)
            {
                char output_name[320];


                snprintf(
                    output_name,
                    sizeof(output_name),
                    "downloaded_%s",
                    received_filename
                );


                FILE *file =
                    fopen(output_name, "wb");


                if (file == NULL)
                {
                    perror("GET fopen");

                    break;
                }


                if (receive_file(
                        socket_fd,
                        &reader,
                        file,
                        file_size) < 0)
                {
                    fclose(file);

                    remove(output_name);

                    printf(
                        "GET file transfer failed.\n"
                    );

                    break;
                }


                fclose(file);


                printf(
                    "Downloaded %ld bytes to %s\n",
                    file_size,
                    output_name
                );
            }


            continue;
        }


        /* =================================================
           MONITOR START
           ================================================= */

        if (strncmp(
                input,
                "MONITOR START ",
                14) == 0)
        {
            int udp_port;


            if (sscanf(
                    input,
                    "MONITOR START %d",
                    &udp_port) != 1 ||
                udp_port < 1 ||
                udp_port > 65535)
            {
                printf(
                    "Invalid UDP port.\n"
                );

                continue;
            }


            if (monitor_pid > 0)
            {
                printf(
                    "Monitoring is already running.\n"
                );

                continue;
            }


            int udp_socket = socket(
                AF_INET,
                SOCK_DGRAM,
                0
            );


            if (udp_socket < 0)
            {
                perror("UDP socket");

                continue;
            }


            struct sockaddr_in udp_address;


            memset(
                &udp_address,
                0,
                sizeof(udp_address)
            );


            udp_address.sin_family =
                AF_INET;

            udp_address.sin_addr.s_addr =
                htonl(INADDR_ANY);

            udp_address.sin_port =
                htons(
                    (unsigned short)udp_port
                );


            if (bind(
                    udp_socket,
                    (struct sockaddr *)
                        &udp_address,
                    sizeof(udp_address)) < 0)
            {
                perror("UDP bind");

                close(udp_socket);

                continue;
            }


            monitor_pid = fork();


            if (monitor_pid < 0)
            {
                perror("fork");

                close(udp_socket);

                monitor_pid = -1;

                continue;
            }


            if (monitor_pid == 0)
            {
                close(socket_fd);

                receive_udp_monitor(
                    udp_socket
                );
            }


            close(udp_socket);


            char command[LINE_SIZE];


            snprintf(
                command,
                sizeof(command),
                "%s\n",
                input
            );


            if (send_all(
                    socket_fd,
                    command,
                    strlen(command)) < 0)
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

                break;
            }


            int result =
                receive_line(
                    socket_fd,
                    &reader,
                    response,
                    sizeof(response)
                );


            if (result != 1)
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

                break;
            }


            printf(
                "%s\n",
                response
            );


            if (strncmp(
                    response,
                    "OK MONITOR_STARTED",
                    18) != 0)
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


            continue;
        }


        /* =================================================
           Normal text command
           ================================================= */

        char command[LINE_SIZE + 2];


        snprintf(
            command,
            sizeof(command),
            "%s\n",
            input
        );


        if (send_all(
                socket_fd,
                command,
                strlen(command)) < 0)
        {
            perror("send");

            break;
        }


        int result =
            receive_line(
                socket_fd,
                &reader,
                response,
                sizeof(response)
            );


        if (result == 0)
        {
            printf(
                "Agent closed the connection.\n"
            );

            break;
        }


        if (result != 1)
        {
            printf(
                "Failed to receive response.\n"
            );

            break;
        }


        printf(
            "%s\n",
            response
        );


        /* MONITOR STOP */
        if (strcmp(
                input,
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
                    "Local UDP receiver stopped.\n"
                );
            }
        }


        /* QUIT */
        if (strcmp(
                input,
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
            }


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


    close(socket_fd);


    printf(
        "Controller connection closed.\n"
    );


    return 0;
}
