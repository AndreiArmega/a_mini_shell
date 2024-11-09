// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"


#define READ	0
#define WRITE	1


static bool shell_cd(word_t *dir)
{
if (dir && dir->string)
	return chdir(dir->string);
return false;
}

static int shell_exit(void)
{
return SHELL_EXIT;
}

static int parse_simple(simple_command_t *s, int level, command_t *father)
{
pid_t child_pid;
pid_t wait_return;
int status;
if (!s)
	return -1;
if (strcmp(s->verb->string, "exit") == 0 || strcmp(s->verb->string, "quit") == 0)
	return shell_exit();
if (strcmp(s->verb->string, "cd") == 0) {
	if (s->out) {
		int fd;
fd = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);
close(fd);
}
if (s->err) {
	int fd;
fd = open(s->err->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);
close(fd);
}
return shell_cd(s->params);
}
if (s->verb->next_part != NULL) {
	const char *variable = s->verb->string;
const char *value;
if (strcmp(s->verb->next_part->string, "=") == 0) {
	if (s->verb->next_part->next_part == NULL) {
		fprintf(stderr, "Invalid command.");
return -1;
} else {
	value = s->verb->next_part->next_part->string;
}
}
return setenv(variable, value, 1);
}
child_pid = fork();
int fd, fd2;
switch (child_pid) {
case -1:
DIE(1, "fork");
break;
case 0:
if (s->in != NULL) {
	fd = open(get_word(s->in), O_RDONLY);
fd2 = dup2(fd, STDIN_FILENO);
DIE(fd2 < 0, "dup2");
close(fd);
}
if (s->out != NULL && s->err != NULL) {
	fd = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);
dup2(fd, STDOUT_FILENO);
dup2(fd, STDERR_FILENO);
close(fd);
} else {
	if (s->out) {
		if (s->io_flags & IO_OUT_APPEND) {
			fd = open(get_word(s->out), O_WRONLY | O_CREAT | O_APPEND, 0644);
DIE(fd < 0, "open");
fd2 = dup2(fd, STDOUT_FILENO);
DIE(fd2 < 0, "dup2");
close(fd);
} else {
	fd = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0644);
DIE(fd < 0, "open");
fd2 = dup2(fd, STDOUT_FILENO);
DIE(fd2 < 0, "dup2");
close(fd);
}
} else if (s->err != NULL) {
	if (s->io_flags & IO_ERR_APPEND) {
		fd = open(get_word(s->err), O_WRONLY | O_CREAT | O_APPEND, 0644);
DIE(fd < 0, "open");
fd2 = dup2(fd, STDERR_FILENO);
DIE(fd2 < 0, "dup2");
close(fd);
} else {
	fd = open(get_word(s->err), O_WRONLY | O_CREAT | O_TRUNC, 0644);
DIE(fd < 0, "open");
fd2 = dup2(fd, STDERR_FILENO);
DIE(fd2 < 0, "dup2");
close(fd);
}
}
}
char *cmd = get_word(s->verb);
int argv_size;
char **argv = get_argv(s, &argv_size);
if (execvp(cmd, argv) == -1)
	fprintf(stderr, "Execution failed for '%s'\n", cmd);
free(cmd);
int i;
for (i = 0; i < argv_size; i++)
	free(argv[i]);
free(argv);
exit(EXIT_FAILURE);
break;
default:
wait_return = waitpid(child_pid, &status, 0);
DIE(wait_return < 0, "waitpid");
if (WIFEXITED(status))
	return WEXITSTATUS(status);
break;
}
return 0;
}

static int run_in_parallel(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
pid_t child_pid;
pid_t wait_return;
int status;
child_pid = fork();
switch (child_pid) {
case -1:
DIE(1, "fork");
break;
case 0:
exit(parse_command(cmd1, level + 1, father));
break;
default:
return parse_command(cmd2, level + 1, father);
wait_return = waitpid(child_pid, &status, 0);
DIE(wait_return < 0, "waitpid");
if (WIFEXITED(status))
	return WEXITSTATUS(status);
break;
}
return 0;
}
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
int pipe_vect[2], rc;
rc = pipe(pipe_vect);
if (rc < 0)
	return false;
pid_t pid, pid2;
int status;
pid = fork();
switch (pid) {
case -1:
return -1;
case 0:
close(pipe_vect[0]);
rc = dup2(pipe_vect[1], STDOUT_FILENO);
if (rc < 0)
	return -1;
close(pipe_vect[1]);
parse_command(cmd1, level + 1, father);
exit(-1);
default:
pid2 = fork();
switch (pid2) {
case -1:
return -1;
case 0:
close(pipe_vect[1]);
rc = dup2(pipe_vect[0], STDIN_FILENO);
if (rc < 0)
	return -1;
close(pipe_vect[0]);
parse_command(cmd2, level + 1, father);
exit(-1);
default:
break;
}
break;
}
close(pipe_vect[1]);
close(pipe_vect[0]);
waitpid(pid2, &status, 0);
return true;
}

int parse_command(command_t *c, int level, command_t *father)
{
int rc;
if (c->op == OP_NONE) {
	rc = parse_simple(c->scmd, level, father);
return rc;
}
switch (c->op) {
case OP_SEQUENTIAL:
parse_command(c->cmd1, level + 1, c);
return parse_command(c->cmd2, level + 1, c);
break;
case OP_PARALLEL:
return run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
break;
case OP_CONDITIONAL_NZERO:
rc = parse_command(c->cmd1, level + 1, c);
if (rc != 0)
	return parse_command(c->cmd2, level + 1, c);
break;
case OP_CONDITIONAL_ZERO:
rc = parse_command(c->cmd1, level + 1, c);
if (rc == 0)
	return parse_command(c->cmd2, level + 1, c);
break;
case OP_PIPE:
return run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
break;
default:
return SHELL_EXIT;
}
return 0;
}


