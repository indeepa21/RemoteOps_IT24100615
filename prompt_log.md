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
