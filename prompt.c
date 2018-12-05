#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define N_JOBS 64
#define USE_READLINE

struct info_process {
	pid_t pid;
	char status; // ’E’, ‘D’, ‘F’
	char command_line[COMMAND_LINE_SIZE]; // Comando
};

