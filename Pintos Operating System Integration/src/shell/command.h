#ifndef COMMAND_HEAD
#define COMMAND_HEAD
typedef struct shell_command {
    char **args;
    int size;
    char *input;
    char *output;
    bool fappend;
    char *error;
    // command_t *pipe_dest;
} command_t;


command_t *createCommand(char **args, int size, char *input, char *output, char *error, bool fappend);

// void setPipe(command_t *c1, command_t *c2);

void deleteCommand(command_t *cur);

#endif
// command *pipeCommand(command *c1, command*c2);