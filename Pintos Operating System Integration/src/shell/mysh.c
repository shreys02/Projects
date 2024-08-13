#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include "command.c"
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

static void read_line (char line[], size_t);
static bool backspace (char **pos, char line[]);
command_t **parse_user_command(char line[], int count);

// These are arbitrary maximums chosen on reasonable limits of commands/lengths
const int DIRECTORY_MAX = 300;
const int COMMAND_MAX = 1000;

// This is what Microsoft Windows roughly uses as a max length for file names
const int BUFFER_MAX = 256;
int
main (void)
{
	struct passwd *pwd;
	pwd = getpwuid(getuid());
	char direc[DIRECTORY_MAX];
	getcwd(direc, sizeof(direc));
	for (;;) 
	{
		// Prints out username and current working directory 
		// followed by 2 spaces for readability
		printf("%s:%s>  ", pwd->pw_name, direc);
		fflush(stdout);
		char command[COMMAND_MAX];
		read_line(command, sizeof command);

		// Handles exit commands
		if (!memcmp (command, "exit ", 5) || !memcmp (command, "exit", 4)) {
			break;
		}
		// Handles cd commands
		else if (!memcmp (command, "cd", 2)) 
		{
			if (chdir (command + 3)) {
           		printf("%s\n", strerror(errno));
			}
			getcwd(direc, sizeof(direc)); 
		}
		else if (command[0] == '\0') 
		{
			/* Empty command. */
			// Displays directory again and waits for new command
		}
		else
		{
			// The number of commands will naturally depend on the number of 
			// valid pipe characters (those not inside ""), so we used this 
			// variable + 1 to represent the number of commands. 
			int pipe_count = 0;
			bool quote = false;
			for (int i = 0; command[i]; i++)
				if (command[i] == '"') {
					quote = !quote;
				}
				else if (!quote){
					pipe_count += (command[i] == '|');
				}
			
			// Get a list of command_ts
			command_t **commands = parse_user_command(command, pipe_count);

			// Stores fds (input and output) for all pipes 
			int pipe_fds[pipe_count * 2 + 1];

			for (int i=0; i < pipe_count; i++) {
				if (pipe(pipe_fds + i*2) < 0) {
					perror("Pipe creation failed"); 
					exit(0);
				}
			}

			pid_t pids[pipe_count + 1];
			int fd_count = 0;
			// Forks off children based on pipe_count
			for (int j = 0; j < pipe_count + 1; j++) {
				command_t *cur_command = commands[j];
                pids[j] = fork();
				int in_file, out_file, err_file;
				if (pids[j] < 0) {
					printf("Forking has failed\n");
					break;
				}
				else if (pids[j] == 0) {
					// Handles case of input redirection
					if (*(cur_command->input) != '\0') {
						in_file = open(cur_command->input, O_RDONLY);
						if (in_file != -1) {
							dup2(in_file, STDIN_FILENO);
							close(in_file);
						}
						else {
							fprintf(stderr, "Error executing command '%s'\n", strerror(errno));
							exit(1);
						}
					}
					else {
						if (pipe_count != 0) {
							if (j != 0) {
								dup2(pipe_fds[fd_count-2], STDIN_FILENO);
							}
                        }
					}
					// Handles case of output redirection
					if (*(cur_command->output) != '\0') {
						// BONUS: Handles appending to a file instead of just truncating
						if (cur_command->fappend) {
							out_file = open(cur_command->output, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
						}
						else {
							out_file = open(cur_command->output, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
						}
						if (out_file != -1) {
							dup2(out_file, STDOUT_FILENO);
							close(out_file);
						}
						else {
							fprintf(stderr, "Error executing command '%s'\n", strerror(errno));
							exit(1);
						}
					}
					else {
                        if (pipe_count != 0) {
							if (j < pipe_count) {
                            	dup2(pipe_fds[fd_count+1], STDOUT_FILENO);
							}
                        }
                    }
					// BONUS: Handles case of error output redirection
					if (*(cur_command->error) != '\0') {
						err_file = open(cur_command->error, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
						if (err_file != -1) {
							dup2(err_file, STDERR_FILENO);
							close(err_file);
						}
						else {
							fprintf(stderr, "Error executing command '%s'\n", strerror(errno));
							exit(1);
						}
					}

					// Closes of all ends of all pipes as required
					for(int a = 0; a < pipe_count * 2; a++) {
                		close(pipe_fds[a]);
					}

					if (execvp(cur_command->args[0], cur_command->args) < 0) {
						fprintf(stderr, "Error executing command '%s': %s\n", cur_command->args[0], strerror(errno));
						exit(1);
					}
					
				}
				// Frees memory associated with that command
				deleteCommand(cur_command);
				fd_count += 2;
			}
			int n = 0;
            int status;
            pid_t pid;
			// Closes of all ends of all pipes as required in the parent scope
			for(int a = 0; a < pipe_count * 2; a++) {
                close(pipe_fds[a]);
			}
			// Wait for all the children to terminate
            while (n < pipe_count + 1) {
				pid = wait(&status);
                n++;
            }
       }
   }

	return 0;
}

// Parses a "command" to setup arguments for exec as well as components for redirection
command_t *parse_subcommand(char subcommand[]) {
	char *args[COMMAND_MAX];
	int idx = 0;
	bool quote = false;
	bool input = false;
	bool output = false;
	bool fappend = false;
	bool errout = false;

	// Setting up pointers to store redirection input/output/errors
	char inputbuf[BUFFER_MAX];
	char outputbuf[BUFFER_MAX];
	char errorbuf[BUFFER_MAX];
	*inputbuf = '\0';
	*outputbuf = '\0';
	*errorbuf = '\0';
	char *inch = inputbuf;
	char *outch = outputbuf;
	char *errch = errorbuf;

	int length = strlen(subcommand);
	char *start = subcommand;
	char *tmp = start;

	for (size_t i = 0; i < length; i++) {
		char c = subcommand[i];
		// Handles quotes via a boolean that is checked for redirection
		if (c == '"') {
			quote = !quote;
		}
		else if (c == '<' && !quote) {
			input = true;
		}
		// BONUS: Part of Error Redirection to stdout
		else if (c == '2' && subcommand[i+1] == '>') {
            output = false;
            errout = true;
            i += 1;
        }
		else if (c == '>' && !quote) {
			input = false;
			*inch = '\0';
			output = true;
			if (subcommand[i+1] == '>') {
				i += 1;
				fappend = true;
			}
		}
		else if (input && (c == ' ' || c == '>')) {
			*inch = '\0';
			input = false;
		}
		else if (output && (c == ' ' || c == '2')) {
			*outch = '\0';
			output = false;
		}
		else if (errout && (c == ' ')) {
			*errch = '\0';
			errout = false;
		}
		else if (input) {
			*inch++ = subcommand[i];
		}
		else if (output) {
			*outch++ = subcommand[i];
		}
		else if (errout) {
			*errch++ = subcommand[i];
		}
		else if (c == ' ' && !quote) {
			*tmp = '\0';
			args[idx++] = start;
			start = tmp + 1;
			tmp++;
		}
		else {
			*tmp++ = subcommand[i];
		}
	}
	// Ensures valid termination of all required strings
	*tmp = '\0';
	args[idx++] = start;
	args[idx] = NULL;

	*inch = '\0';
	*outch = '\0';
	*errch = '\0';

	char **args_ptr = args;
	// Creates command using command struct
	command_t *curr_command = createCommand(args_ptr, idx, inputbuf, outputbuf, errorbuf, fappend);
	return curr_command;
}

// Parses the overall user input (reffered to here as "user command") into subcommands
// and calls parse_subcommand. 
command_t **parse_user_command(char line[], int p_count) {
	size_t length = strlen(line); 
 
	char strip_space[length + 1];
	char *ptr = strip_space;
	char *ptr_l = line;

	bool q = false;
	// Gets rid of preceding spaces
	while (*ptr_l == ' ') {
		ptr_l++;
	}

	while (*ptr_l != '\0') {
		if (*ptr_l == '"') {
			q = !q;
		}
		// Parses strings and gets rid of insignificant spaces 
		// (spaces that could have not existed and resulted in the same command)
		if (*ptr_l == ' ' && q == false) {
			if (ptr_l == line || *(ptr_l + 1) == ' ') {
				ptr_l++;
			}
			else if (*(ptr_l - 1) == '<' || *(ptr_l + 1) == '<' || *(ptr_l - 1) == '>' || *(ptr_l + 1) == '>') {
				ptr_l++;
			}
			else if (*(ptr_l - 1) == '|' || *(ptr_l + 1) == '|') {
				ptr_l++;
			}
			else if ((ptr != strip_space) && (*(ptr - 1) == '<' || *(ptr - 1) == '>' || *(ptr - 1) == '|')) {
				ptr_l++;
			}
			else if (*(ptr_l + 1) == '\0') {
                *ptr = '\0';
                ptr_l++;
            }
			else {
				*ptr = *ptr_l;
				ptr_l++;
				ptr++;
			}
		}
		else {
			*ptr = *ptr_l;
			ptr_l++;
			ptr++;
		}
		}
	*ptr = '\0';
	// Here length2 refers to the result stripped command's length
	size_t length2 = strlen(strip_space); 

	char subline[length2 + 1];
	char *tmp = subline;

	bool quote = false;

	command_t *commands[p_count + 2];
	command_t **cmd_ptr = commands;

	// Goes through results and calls parse_subcommands when necessary 
	// based on locations of valid pipes (not inside quotations)
	for (size_t i = 0; i < length2; i++) {
		char c = strip_space[i];
		if (c == '"') {
			quote = !quote;
			*tmp++ = strip_space[i];
		}
		else if (c == '|' && !quote) {
			if (strip_space[i - 1] == ' ') {
			*(tmp - 1) = '\0';
			}
			else {
			*tmp = '\0';
			}
			command_t *command = parse_subcommand(subline);
			*cmd_ptr++ = command;
			tmp = subline;
		}
		else {
			*tmp++ = strip_space[i];
		}
	}
	*tmp = '\0';
	command_t *command = parse_subcommand(subline);
	*cmd_ptr++ = command;
	*cmd_ptr = NULL;
	return (cmd_ptr - p_count - 1);
}

// NOTE: The following commands were referenced from examples/shell.c
// and then built upon to handle our new logic. 
static void
read_line (char line[], size_t size) 
{
 char *pos = line;
 for (;;)
    {
    	char c;
    	read (STDIN_FILENO, &c, 1);
    	switch (c) 
		{
		case '\n':
			if (pos == line) {
				*pos = '\0';
				return;
			}
			// BONUS: Allows for multi-line commands
			else if (*(pos - 1) != '\\') {
				*pos = '\0';
				return;
			}
		case '\b':
			backspace (&pos, line);
			break;

		case ('U' - 'A') + 1:
			while (backspace (&pos, line))
			continue;
			break;

		default:
			if (pos < line + size - 1) 
			{
				*pos++ = c;
			}
			break;
		}
   }
}

static bool
backspace (char **pos, char line[]) 
{
	if (*pos > line)
	{
		/* Back up cursor, overwrite character, back up
		again. */
		(*pos)--;
		return true;
	}
	else
		return false;
}