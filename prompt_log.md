# AI Prompt Log

## Entry 1
Tool: ChatGPT

Prompt:
I am ready to start the IE3090 RemoteOps assignment. I am a beginner. Guide me step by step. My registration number is IT24100615.

How I used it:
I used the response to understand the assignment and calculate my personalised values such as port 9410, SID 5160, and authentication token OPS-0615.

## Entry 2
Tool: ChatGPT

Prompt:
Can I use CentOS 10 for this assignment?

How I used it:
I used the response to confirm that CentOS 10 is suitable and then checked GCC, Make, Git and networking tools.

## Entry 3
Tool: ChatGPT

Prompt:
Guide me to set up Git for my RemoteOps project.

How I used it:
I followed the steps to initialize Git, create .gitignore, add files, and create my first commit.

## Entry 4
Tool: ChatGPT

Prompt:
Why did I get the error "fatal: not a git repository"?

How I used it:
I learned that I was running the Git command outside the project folder. I changed to the RemoteOps_IT24100615 folder and ran the command again

## Entry 5

Tool: ChatGPT

Prompt:
Asked for Step 6 to create the Makefile and first simple C programs.

How I used it:
I created Makefile_615, simple starter Agent and Controller programs, compiled them with GCC, tested make clean, rebuilt them, and ran both programs.

## Entry 6

Tool: ChatGPT

Prompt:
Asked for Step 7 to implement the basic RemoteOps Agent TCP server.

How I used it:
I added socket(), bind(), listen() and accept() to agent_615.c, compiled the program, verified that it listened on port 9410 using ss, and tested one TCP connection.

## Entry 7

Tool: ChatGPT

Prompt:
Asked for help completing the basic Controller TCP connection.

How I used it:
I added socket(), inet_pton() and connect() to controller_615.c, compiled the program, and tested a successful TCP connection to the Agent on port 9410.

## Entry 8

Tool: ChatGPT

Prompt:
Asked for Step 9 to implement reliable TCP line framing.

How I used it:
I implemented a buffered receive_line() function so the Agent can handle partial TCP lines and multiple newline-terminated lines. I tested both cases using Netcat and shell printf commands.

## Entry 9

Tool: ChatGPT

Prompt:
Asked for the next step after reliable TCP line framing.

How I used it:
I implemented AUTH using the personalised token OPS-0615, added SID:5160 to Agent responses, tested successful and failed authentication, and tested rejection of a command before authentication.

## Entry 10

Tool: ChatGPT

Prompt:
Asked for the next step after implementing authentication.

How I used it:
I implemented SYSINFO using Linux /proc files and tested the command after successful authentication.

## Entry 11

Tool: ChatGPT

Prompt:
Asked for Step 12 to implement LISTPROC.

How I used it:
I implemented LISTPROC using popen() and the Linux ps command, formatted the running processes into the required response, and tested it after authentication.

## Entry 12

Tool: ChatGPT

Prompt:
Asked for Step 13 to implement the restricted EXEC command.

How I used it:
I implemented the fixed EXEC whitelist, mapped each allowed name to a predefined Linux command, and tested both an allowed command and a rejected command.

## Entry 13

Tool: ChatGPT

Prompt:
Asked for Step 14 to implement PUT file upload.

How I used it:
I implemented exact-byte file reception for PUT, stored uploaded files under my personalised storage directory, and tested the upload and file integrity.

## Entry 14

Tool: ChatGPT

Prompt:
Asked for Step 15 to implement GET file download.

How I used it:
I implemented GET so the Agent sends the response header followed by the exact raw file bytes. I tested a successful download, file integrity, and the FILE_NOT_FOUND error.

## Entry 15

Tool: ChatGPT

Prompt:
Asked for Step 16 to implement UDP MONITOR START.

How I used it:
I implemented a UDP monitoring process that sends periodic SYSINFO-style datagrams to the Controller and includes SID:5160.

## Entry 16

Tool: ChatGPT

Prompt:
Asked for Step 17 to implement MONITOR STOP.

How I used it:
I added handling for MONITOR STOP so the Agent terminates the active UDP monitoring process and returns the required SID-tagged response.

## Entry 17

Tool: ChatGPT

Prompt:
Asked for Step 18 to implement QUIT.

How I used it:
I implemented QUIT so the Agent sends the required BYE response, stops active monitoring if necessary, and closes the TCP connection cleanly.

## Entry 18

Tool: ChatGPT

Prompt:
Asked for Step 19 to implement logging and graceful disconnect handling.

How I used it:
I added timestamped logging to remoteops_IT24100615.log and added handling for clean and unexpected TCP disconnects.
