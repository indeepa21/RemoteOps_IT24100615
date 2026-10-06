#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define AGENT_IP "127.0.0.1"
#define AGENT_PORT 9410

int main(void)
{
    int controller_socket;
    struct sockaddr_in agent_address;

    controller_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (controller_socket < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&agent_address, 0, sizeof(agent_address));

    agent_address.sin_family = AF_INET;
    agent_address.sin_port = htons(AGENT_PORT);

    if (inet_pton(AF_INET, AGENT_IP, &agent_address.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(controller_socket);
        return 1;
    }

    printf("RemoteOps Controller - IT24100615\n");
    printf("Connecting to Agent at %s:%d...\n",
           AGENT_IP,
           AGENT_PORT);

    if (connect(controller_socket,
                (struct sockaddr *)&agent_address,
                sizeof(agent_address)) < 0)
    {
        perror("connect");
        close(controller_socket);
        return 1;
    }

    printf("Connected to RemoteOps Agent successfully.\n");

    close(controller_socket);

    printf("Controller connection closed.\n");

    return 0;
}
