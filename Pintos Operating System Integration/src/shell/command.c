#include "command.h"
#include <stdio.h>
#include <stdlib.h>

// char **args, int size, char *input, char *output
command_t *createCommand(char **args, int size, char *input, char *output, char *error, bool fappend){
    command_t *cur = (command_t *) calloc(1, sizeof(command_t));

    char **final_args = malloc((size+1) * sizeof(char *));
    
    for(int i = 0; i < size; i++) {
        final_args[i] = malloc((strlen(args[i])+1) * sizeof(char));
        strcpy(final_args[i], args[i]);
    }
    final_args[size] = NULL;
    cur->args = final_args;

    char *input_final = malloc((strlen(input)+1) * sizeof(char));
    if (!input_final) {
        printf("Mallocing input failed\n");
    }
    char *output_final = malloc((strlen(output)+1) * sizeof(char));
    if (!output_final) {
        printf("Mallocing output failed\n");
    }
    char *error_final = malloc((strlen(error)+1) * sizeof(char));
    if (!error_final) {
        printf("Mallocing output failed\n");
    }

    strcpy(input_final, input);
    cur->input = input_final;
    strcpy(output_final, output);
    cur->output = output_final;
    strcpy(error_final, error);
    cur->error = error_final;

    cur->size = size;
    cur->fappend = fappend;
    return cur;
}

void deleteCommand(command_t *cur) {
    if (cur == NULL)
        return;

    // Freeing input and output strings
    if (cur->input != NULL)
        free(cur->input);
    if (cur->output != NULL)
        free(cur->output);
    if (cur->error != NULL)
        free(cur->error);

    // Freeing arguments
    if (cur->args != NULL) {
        for (int i = 0; i < cur->size; i++) {
            if (cur->args[i] != NULL) {
                free(cur->args[i]);
            }
        }
        free(cur->args);
    }

    // Freeing the command structure itself
    free(cur);
}


