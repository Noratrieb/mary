#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#ifdef _GNU_SOURCE
#error yeet
#endif

typedef enum status
{
    OK,
    EXIT,
} status;

typedef struct options
{
    bool print_exec;
} options;

typedef struct context
{
    options opts;
} context;

typedef struct strlist
{
    char *part;
    struct strlist *next;
} strlist;

typedef strlist cmdline;

status alloc_failure()
{
    fprintf(stderr, "failed to allocate");
    return EXIT;
}

void free_list(strlist *list)
{
    for (strlist *node = list; node;)
    {
        free(node->part);
        strlist *next_node = node->next;
        free(node);
        node = next_node;
    }
}

status parse(char *line, cmdline **head)
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

                cmdline *new = malloc(sizeof(*new));
                new->part = part;
                new->next = NULL;
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
        perror("fork()");
    }
    if (fork_result == 0)
    {
        // child
        size_t argc = 0;
        for (cmdline *node = cmd; node; node = node->next)
        {
            argc++;
        }

        size_t argc_size = sizeof(char *) * (argc + 1 /*NULL terminator*/);
        char **argv = malloc(argc_size);
        if (!argv)
        {
            fprintf(stderr, "failed to allocate memory for spawn\n");
            return;
        }
        char **argv_cur = argv;
        for (cmdline *node = cmd; node; node = node->next)
        {
            *argv_cur = node->part;
            argv_cur++;
        }
        *argv_cur = NULL;

        execvp(cmd->part, argv);
        // we must have failed if we're still here
        perror(cmd->part);
        free(argv);
        free_list(cmd);
        exit(1);
    }
    else
    {
        // parent
        wait(&fork_result);
    }
}

status builtin_set(cmdline *args)
{
    cmdline *name_arg = args;
    if (!name_arg)
    {
        fprintf(stderr, "set: missing variable name\n");
        return OK;
    }

    char *name = name_arg->part;

    cmdline *value_arg = name_arg->next;
    if (!value_arg)
    {
        fprintf(stderr, "set: missing variable value\n");
        return OK;
    }

    char *value = value_arg->part;

    (void)name;
    (void)value;

    return OK;
}

status execute(context *ctx, cmdline *cmd)
{
    char *prog = cmd->part;

    if (strcmp(prog, "exit") == 0)
    {
        return EXIT;
    }
    else if (strcmp(prog, "set") == 0)
    {
        return builtin_set(cmd->next);
    }
    else
    {
        // spawn the program
        spawn(cmd);
        return OK;
    }
}

status read_line(context *ctx)
{
    char line[1024];

    size_t count = read(0, line, sizeof(line));

    if (count == 0)
    {
        return EXIT;
    }
    else if (count < 0)
    {
        perror("reading line");
        return OK;
    }
    else if (count == sizeof(line))
    {
        fprintf(stderr, "line too long");
        return OK;
    }

    line[count] = '\0';

    cmdline *head = NULL;
    status result = parse(line, &head);
    if (result == EXIT)
    {
        return EXIT;
    }

    if (!head)
    {
        return OK;
    }

    if (ctx->opts.print_exec)
    {
        printf("+");
        for (cmdline *cur = head; cur; cur = cur->next)
        {
            printf(" %s", cur->part);
        }
        printf("\n");
    }

    result = execute(ctx, head);

    free_list(head);

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

    context ctx = {.opts = opts};

    while (true)
    {
        char *prompt = "\r$ ";

        int written = write(1, prompt, strlen(prompt));
        (void)written; // don't care if the prompt was written.

        status result = read_line(&ctx);

        if (result == EXIT)
        {
            return 0;
        }
    }
}