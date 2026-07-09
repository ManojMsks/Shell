#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ─────────────────────────────────────────────
   Forward declarations
   ───────────────────────────────────────────── */
int lsh_execute_pipe(char **cmd1, char **cmd2);
int lsh_cd(char **args);
int lsh_exit(char **args);
int lsh_execute_multipipe(char **args);

/* ─────────────────────────────────────────────
   Builtins table
   Two parallel arrays: name → function.
   lsh_num_builtins() uses sizeof so it updates
   automatically when you add entries.
   ───────────────────────────────────────────── */
char *builtin_str[] = {
    "cd",
    "exit"
};

int (*builtin_func[])(char **) = {
    &lsh_cd,
    &lsh_exit
};

int lsh_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

/* ─────────────────────────────────────────────
   Builtin: cd
   Must be a builtin — a child process changing
   its own directory has no effect on the shell.
   ───────────────────────────────────────────── */
int lsh_cd(char **args)
{
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: expected argument to \"cd\"\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("lsh");
        }
    }
    return 1;
}

/* ─────────────────────────────────────────────
   Builtin: exit
   Returning 0 breaks the main loop in lsh_loop.
   ───────────────────────────────────────────── */
int lsh_exit(char **args)
{
    return 0;
}

/* ─────────────────────────────────────────────
   lsh_launch
   Forks a child, sets up redirection if needed,
   then execvp's the command.
   
   outfile   — filename for stdout redirection (or NULL)
   out_flags — open() flags: O_TRUNC for >, O_APPEND for >>
   infile    — filename for stdin redirection (or NULL)
   ───────────────────────────────────────────── */
int lsh_launch(char **args, char *outfile, int out_flags, char *infile)
{
    pid_t pid;
    int status;

    pid = fork();

    if (pid == 0) {
        /* ── Child process ── */

        /* Redirect stdout to file if > or >> was used */
        if (outfile != NULL) {
            int fd = open(outfile, out_flags, 0644);
            if (fd < 0) {
                perror("lsh: open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd); /* fd is duplicated onto stdout, original not needed */
        }

        /* Redirect stdin from file if < was used */
        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror("lsh: open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd); /* fd is duplicated onto stdin, original not needed */
        }
				signal(SIGINT,  SIG_DFL);
				signal(SIGQUIT, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);
        /* Replace child process image with the command.
           If execvp returns, it failed — exit so we don't
           run a second copy of the shell. */
        if (execvp(args[0], args) == -1) {
            perror("lsh");
        }
        exit(EXIT_FAILURE);

    } else if (pid < 0) {
        /* ── Fork failed ── */
        perror("lsh");

    } else {
        /* ── Parent process ──
           Block until child exits normally or is killed by a signal.
           Loop handles WUNTRACED: waitpid can return early if child
           is stopped (Ctrl+Z) — we keep waiting until it's truly done. */
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

/* ─────────────────────────────────────────────
   lsh_execute
   Decides what to do with a parsed command:
   1. Check for pipe → lsh_execute_pipe
   2. Check for builtins → call directly
   3. Check for redirection → lsh_launch with flags
   4. Plain command → lsh_launch with no redirection
   ───────────────────────────────────────────── */
int lsh_execute(char **args)
{
    int i;

    /* Empty input — do nothing */
    if (args[0] == NULL) {
        return 1;
    }

    /* Scan for pipe operator
       Split args into two at the | position.
       args becomes cmd1, &args[i+1] becomes cmd2. */
    for (i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "|") == 0) {
        return lsh_execute_multipipe(args);
    }
    }

    /* Check builtins — cd and exit must run in the
       shell process itself, not a child */
    for (i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    /* Scan for redirection operators.
       Save filename and open() flags, then null-terminate
       args at the operator so execvp doesn't see > or filename. */
    char *outfile  = NULL;
    char *infile   = NULL;
    int   out_flags = 0;

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            outfile   = args[i + 1];
            out_flags = O_WRONLY | O_CREAT | O_TRUNC;  /* overwrite */
            args[i]   = NULL;
            break;
        } else if (strcmp(args[i], ">>") == 0) {
            outfile   = args[i + 1];
            out_flags = O_WRONLY | O_CREAT | O_APPEND; /* append */
            args[i]   = NULL;
            break;
        } else if (strcmp(args[i], "<") == 0) {
            infile  = args[i + 1];
            args[i] = NULL;
            break;
        }
    }

    return lsh_launch(args, outfile, out_flags, infile);
}

/* ─────────────────────────────────────────────
   lsh_read_line
   Uses getline so it handles arbitrarily long
   lines. getline mallocs the buffer — caller
   must free() the returned pointer.
   ───────────────────────────────────────────── */
char *lsh_read_line(void)
{
    char *line    = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS); /* Ctrl+D — clean exit */
        } else {
            perror("lsh: readline");
            exit(EXIT_FAILURE);
        }
    }

    return line;
}

/* ─────────────────────────────────────────────
   lsh_split_line
   Tokenises the line by whitespace into a NULL-
   terminated array of char pointers.
   strtok modifies the line in-place by replacing
   delimiters with \0 — tokens point into line,
   they are not copies.
   ───────────────────────────────────────────── */
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM   " \t\r\n\a"

char **lsh_split_line(char *line)
{
    int    bufsize = LSH_TOK_BUFSIZE;
    int    position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char  *token, **tokens_backup;

    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position++] = token;
        /* Grow the array if we're out of slots */
        if (position >= bufsize) {
            bufsize       += LSH_TOK_BUFSIZE;
            tokens_backup  = tokens;
            tokens         = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                free(tokens_backup);
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }

    tokens[position] = NULL; /* NULL-terminate for execvp compatibility */
    return tokens;
}

/* ─────────────────────────────────────────────
   lsh_loop
   Main REPL: print prompt → read → split →
   execute → repeat until status == 0.
   ───────────────────────────────────────────── */
void lsh_loop(void)
{
    char  *line;
    char **args;
    int    status;

    do {
        printf("> ");
        fflush(stdout); /* ensure prompt appears before blocking on input */

        line   = lsh_read_line();
        args   = lsh_split_line(line);
        status = lsh_execute(args);

        free(line);
        free(args);
    } while (status);
}

/* ─────────────────────────────────────────────
   main
   ───────────────────────────────────────────── */
int main(int argc, char **argv)
{
		signal(SIGINT,  SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);
    lsh_loop();
    return EXIT_SUCCESS;
}

/* ─────────────────────────────────────────────
   lsh_execute_multipipe 
   Handles cmd1 | cmd2.
   handles multiple no of pipes too.
   Creates a pipe, forks two children:
    child1 writes cmd1 stdout into pipe write end
    child2 reads  cmd2 stdin  from pipe read end
   Parent closes both ends BEFORE waiting to
   avoid deadlock when pipe buffer fills up.
   ───────────────────────────────────────────── */
int lsh_execute_multipipe(char **args) {
    char **cmds[64];
    int    num_cmds = 0;
    int    pipes[64][2];
    int    i;

    // split args into commands array
    cmds[num_cmds++] = args;
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            cmds[num_cmds++] = &args[i + 1];
        }
    }

    // create num_cmds-1 pipes
    for (i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("lsh: pipe");
            return 1;
        }
    }

    // fork a child for each command
    for (i = 0; i < num_cmds; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("lsh: fork");
            return 1;
        }
        if (pid == 0) {
            // reset signals — child should respond to Ctrl+C
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // read from previous pipe if not first command
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            // write to current pipe if not last command
            if (i < num_cmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // close all pipe fds — child only needs what it dup2'd
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(cmds[i][0], cmds[i]);
            perror("lsh");
            exit(EXIT_FAILURE);
        }
    }

    // parent closes all pipe ends
    for (i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // wait for all children, if we didnt wait then child processes would become zombie processes and we would have to kill them manually
    for (i = 0; i < num_cmds; i++) {
        wait(NULL);
    }

    return 1;
}