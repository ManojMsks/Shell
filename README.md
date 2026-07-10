lsh — A Unix Shell in C
A simple Unix shell written from scratch in C. I built this to understand how shells actually work under the hood — how commands get executed, how pipes connect programs together, and how the OS manages processes.
What it can do
 Run any command (ls, grep, cat, etc.)
 Change directories with cd
 Pipe output between commands — including multiple pipes
 Redirect output to a file, append to a file, or read input from a file
 Handle Ctrl+C gracefully — kills the running command, not the shell itself
How to build and run
 gcc -o lsh lsh.c
 ./lsh
Examples
Basic command:
 > ls -la
Single pipe:
 > ls | grep .c
Multiple pipes:
 > ls | grep .c | wc -l
Redirect output to a file:
 > ls > files.txt
Append to a file:
 > echo "hello" >> files.txt
Read input from a file:
 > cat < files.txt
Exit the shell:
 > exit
How it works
When you type a command, the shell:
 Reads the line and splits it into tokens
 Checks if it's a builtin (cd, exit) — these run inside the shell process itself
 If there's a pipe (|), forks a child process for each command and connects them with kernel pipes
 If there's redirection (>, >>, <), opens the file and rewires stdin/stdout using dup2 before running the command
 For plain commands, forks a child and uses execvp to run it, then waits for it to finish
The shell ignores Ctrl+C (SIGINT) so it stays alive — only the child process running your command gets killed.
Builtins
   Command What it does     cd <dir> Change current directory   exit Exit the shell   These have to be builtins because they need to affect the shell process itself — a child process changing its own directory has no effect on the shell.
What I learned
 How fork() and execvp() work together to run programs
 How Unix pipes work at the file descriptor level
 How dup2 rewires stdin/stdout to connect processes or files
 How signals work and why the shell needs to ignore them while children respond to them
 The difference between process memory and kernel state
> 
