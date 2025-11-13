#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

typedef enum code
{
    OK,
    EXIT,
} code;

typedef struct options
{
    bool print_exec;
} options;

typedef struct cmdline
{
    char *part;
    struct cmdline *next;
} cmdline;

code alloc_failure()
{
    fprintf(stderr, "failed to allocate");
    return EXIT;
}

void print_errno(char *context)
{
    char err[64];
    int result = strerror_r(errno, err, sizeof(err));
    if (result < 0)
    {
        strcpy(err, "unknown error");
    }
    fprintf(stderr, "error: %s: %s\n", context, err);
}

code parse(char *line, cmdline **head)
{
    cmdline *cur = NULL;

    size_t start = 0;
    size_t len = 0;
    for (size_t i = 0;; i++)
    {
        char c = line[i];
        if (c == ' ' || c == '\n' || !c)
        {
            if (len > 0)
            {
                // split
                char *part = malloc(len + 1);
                if (!part)
                {
                    return alloc_failure();
                }
                memcpy(part, line + start, len);
                part[len] = '\0';

                cmdline *new = malloc(sizeof(new));
                new->part = part;
                if (cur)
                {
                    cur->next = new;
                }
                else
                {
                    *head = new;
                }
                cur = new;

                len = 0;
            }

            start = i + 1;
        }
        else
        {
            len++;
        }

        if (!c)
        {
            break;
        }
    }

    return OK;
}

void spawn(cmdline *cmd)
{
    int fork_result = fork();
    if (fork_result == -1)
    {
        print_errno("fork()");
    }
    if (fork_result == 0)
    {
        size_t argc = 0;
        for (cmdline *node = cmd; node; node = node->next)
        {
            argc++;
        }

        char **argv = malloc(sizeof(char *) * argc + 1);
        char **argv_cur = argv;
        for (cmdline *node = cmd; node; node = node->next)
        {
            *argv_cur = node->part;
            argv_cur++;
        }
        *argv_cur = NULL;

        // child
        int fail = execvp(cmd->part, argv);
        print_errno(cmd->part);
        exit(1);
    }
    else
    {
        // parent
        wait(&fork_result);
    }
}

code execute(cmdline *cmd)
{
    char *prog = cmd->part;

    if (strcmp(prog, "exit") == 0)
    {
        return EXIT;
    }
    else
    {
        // spawn the program
        spawn(cmd);
    }
}

code read_line(options *opts)
{
    char line[1024];

    pid_t p = getpid();

    size_t count = read(0, line, sizeof(line));

    if (count == 0)
    {
        return EXIT;
    }
    else if (count < 0)
    {
        print_errno("reading line");
        return OK;
    }
    else if (count == sizeof(line))
    {
        fprintf(stderr, "line too long");
        return OK;
    }

    line[count] = '\0';

    cmdline *head = NULL;
    code result = parse(line, &head);
    if (result == EXIT)
    {
        return EXIT;
    }

    if (!head)
    {
        return OK;
    }

    if (opts->print_exec)
    {
        printf("+");
        for (cmdline *cur = head; cur; cur = cur->next)
        {
            printf(" %s", cur->part);
        }
        printf("\n");
    }

    result = execute(head);
    if (result == EXIT)
    {
        return EXIT;
    }

    return OK;
}

int main()
{
    options opts = {};

    char *mary_x = getenv("MARY_X");
    if (mary_x && strcmp(mary_x, "1") == 0)
    {
        opts.print_exec = true;
    }

    while (true)
    {
        char *prompt = "\r> ";

        write(1, prompt, strlen(prompt));
        code result = read_line(&opts);
        if (result == EXIT)
        {
            return 0;
        }
    }
}