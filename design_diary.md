# RemoteOps Design Diary

## Project Setup

Registration Number: IT24100615

I reviewed the assignment requirements and calculated my personalised values:

- TCP Port: 9410
- SID: 5160
- Authentication Token: OPS-0615
- Agent file: agent_615.c
- Controller file: controller_615.c
- Makefile: Makefile_615

I am using CentOS 10 for development.

I checked that GCC, Make, Git and the ss networking tool are installed and working.

I created the RemoteOps project folder and the personalised storage folder:

./agentfiles/IT24100615/

I initialized Git and created my first commit:

chore: initialize RemoteOps project

So far, I have only completed the project setup. I have not started implementing the network code yet.

## Build Setup

I created simple starter versions of agent_615.c and controller_615.c.

I created Makefile_615 using GCC with the warning options -Wall, -Wextra and -Wpedantic.

I tested the build, clean and rebuild process successfully.

At this stage, the Agent and Controller only print personalised information. TCP socket communication has not been implemented yet.

## Basic TCP Agent Server

I changed agent_615.c from a starter program into a basic TCP server.

The Agent creates an IPv4 TCP socket, binds it to my personalised port 9410, starts listening, and accepts one client connection.

I used the ss command to confirm that the Agent was listening on port 9410.

At this stage, the Agent accepts only one connection and then exits. Authentication, commands and concurrency have not been implemented yet.
