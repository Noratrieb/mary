#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <ctype.h>

typedef enum status
{
    OK,
    ERR,
    EXIT,
} status;

typedef struct options
{
    bool print_exec;
} options;

typedef struct varlist
{
    char *name;
    char *value;
    struct varlist *next;
} varlist;

typedef struct strlist
{
    char *part;
    struct strlist *next;
} strlist;

typedef struct context
{
    options opts;
    varlist *vars;
} context;

typedef strlist cmdline;

status alloc_failure()
{
    fprintf(stderr, "failed to allocate");
    return EXIT;
}

void free_strlist(strlist *list)
{
    for (strlist *node = list; node;)
    {
        free(node->part);
        strlist *next_node = node->next;
        free(node);
        node = next_node;
    }
}

void free_varlist(varlist *list)
{
    for (varlist *node = list; node;)
    {
        free(node->name);
        free(node->value);
        varlist *next_node = node->next;
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

status expand_word(context *ctx, cmdline *word)
{
    char *output = NULL;

    char *str = word->part;

    ssize_t dollar_start = -1;
    for (size_t offset = 0;;)
    {
        char c = str[offset];

        bool need_finish_copy = !c && output;
        bool variable_is_over = dollar_start >= 0 && !isalpha(c) && c != '_';

        char *inserting_value = "";

        if (variable_is_over)
        {
            // var end
            str[offset] = '\0';
            char *referenced_varname = str + dollar_start + 1;

            if (strlen(referenced_varname) == 0)
            {
                fprintf(stderr, "must have variable name after $\n");
                return ERR;
            }

            varlist *node = ctx->vars;
            for (; node; node = node->next)
            {
                if (strcmp(node->name, referenced_varname) == 0)
                {
                    break;
                }
            }

            if (!node)
            {
                fprintf(stderr, "variable %s was not found\n", referenced_varname);
                return ERR;
            }

            inserting_value = node->value;
        }

        if (need_finish_copy || variable_is_over)
        {

            size_t existing_len = output ? strlen(output) : 0;

            str[dollar_start] = '\0';
            size_t before_len = strlen(str);
            size_t inserting_value_len = strlen(inserting_value);

            char *new_buf = malloc(existing_len + before_len + inserting_value_len + 1);
            if (!new_buf)
            {
                fprintf(stderr, "failed to allocate buffer\n");
                return ERR;
            }
            memcpy(new_buf, output, existing_len);
            memcpy(new_buf + existing_len, str, before_len);
            memcpy(new_buf + existing_len + before_len, inserting_value, inserting_value_len);
            new_buf[existing_len + before_len + inserting_value_len] = '\0';
            free(output);
            output = new_buf;

            str = str + offset;
            offset = 0;
            dollar_start = -1;
        }
        else if (c == '$')
        {
            dollar_start = offset;
        }

        str[offset] = c; // undo
        offset++;

        if (!c)
        {
            break;
        }
    }

    if (output)
    {
        free(word->part);
        word->part = output;
    }

    return OK;
}

status expand(context *ctx, cmdline *cmd)
{
    for (cmdline *word = cmd; word; word = word->next)
    {
        status result = expand_word(ctx, word);
        if (result != OK)
        {
            return result;
        }
    }

    return OK;
}

status spawn(cmdline *cmd)
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
            return EXIT;
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
        return EXIT;
    }
    else
    {
        // parent
        wait(&fork_result);
        return OK;
    }
}

status builtin_set(context *ctx, cmdline *args)
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

    varlist *existing = NULL;
    for (varlist *node = ctx->vars; node; node = node->next)
    {
        if (strcmp(name, node->name) == 0)
        {
            existing = node;
            break;
        }
    }

    if (existing)
    {
        existing->value = strdup(value);
    }
    else
    {
        varlist *node = malloc(sizeof(*node));
        node->name = strdup(name);
        node->value = strdup(value);
        node->next = ctx->vars;
        ctx->vars = node;
    }

    (void)name;
    (void)value;

    return OK;
}

status builtin_vars(context *ctx, cmdline *args)
{
    if (args)
    {
        fprintf(stderr, "vars: must be called without arguments\n");
        return OK;
    }
    for (varlist *node = ctx->vars; node; node = node->next)
    {
        printf("%s=%s\n", node->name, node->value);
    }

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
        return builtin_set(ctx, cmd->next);
    }
    else if (strcmp(prog, "vars") == 0)
    {
        return builtin_vars(ctx, cmd->next);
    }
    else
    {
        // spawn the program
        return spawn(cmd);
    }
}

status process_next_line(context *ctx)
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
    if (result != OK)
    {
        return result;
    }

    if (!head)
    {
        return OK;
    }

    result = expand(ctx, head);
    if (result != OK)
    {
        free_strlist(head);
        return result;
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

    free_strlist(head);

    if (result == EXIT)
    {
        return EXIT;
    }

    return OK;
}

int process(context *ctx)
{
    while (true)
    {
        char *prompt = "\r$ ";

        int written = write(1, prompt, strlen(prompt));
        (void)written; // don't care if the prompt was written.

        status result = process_next_line(ctx);

        if (result == EXIT)
        {
            return 0;
        }
    }
}

int main()
{
    options opts = {};

    char *mary_x = getenv("MARY_X");
    if (mary_x && strcmp(mary_x, "1") == 0)
    {
        opts.print_exec = true;
    }

    context ctx = {.opts = opts, .vars = NULL};

    int status = process(&ctx);

    free_varlist(ctx.vars);

    return status;
}