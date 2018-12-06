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

char const PROMPT = '$';
char *read_line(char *line); 
int execute_line(char *line);
int parse_args(char **args, char *line);
int check_internal(char **args); 
int internal_cd(char **args); 
int internal_export(char **args); 
int internal_source(char **args); 
int internal_jobs(char **args);
void reaper(int signum);
void ctrlc(int signum);
int is_output_redirection(char **args);
static struct info_process jobs_list[N_JOBS]; 
static pid_t shell_pid;
static char *line_read = (char *)NULL;


int main(int argc, char *argv[]) {
	char line[COMMAND_LINE_SIZE];		
	//Bucle principal del programa donde leemos los comandos de consola
	//para despues ejecutarlos según la opción introducida
	while (read_line(line)) {
		execute_line(line);
	}
	return 0;
}

//Imprime el prompt con su respectivo directorio y usuario
void imprimir_prompt(){
	char cwd[PATH_MAX];
	getcwd(cwd, sizeof(cwd));
	printf("%s:~%s%c ", getenv("USER"), cwd, PROMPT);
}

//Función que imprime el prompt y lee la linea introducida por teclado
char *read_line(char *line){
	
	//En el caso de utilizar el read_line() se ensamblará
	//esta parte del if, en el caso contrario se ensamblará el else
	#ifdef USE_READLINE
		//Si hay algo en line_read lo vaciamos y ponemos todo a NULL
		if (line_read) {
			free (line_read);
			line_read = (char *)NULL;
		}
		
		//Creamos una nueva cadena de 100 carácteres y la formateamos con el 
		//formato del prompt, después se lo pasamos a readline() como argumento
		char str[100];
		char cwd[PATH_MAX];
		getcwd(cwd, sizeof(cwd));
		sprintf(str, "%s:~%s%c ", getenv("USER"), cwd, PROMPT);
		
		//Leemos la linea introducida por el usuario
		line_read = readline(str);

		//Si se ha escrito algo se guarda en el historial 
		//y lo copiamos en line para pasarselo a execute_line()
		if (line_read && *line_read){
			add_history(line_read);
			strcpy(line,line_read);
		} else {
			line = NULL;
		}
		
		return line;
	#else
		//Imprimimos el PROMPT en consola
		imprimir_prompt();
		
		//Leemos la linea por consola
		char *ptr = fgets(line, COMMAND_LINE_SIZE, stdin);

		if (!ptr && !feof(stdin)) { //Si no se incluye la 2ª condición no se consigue salir del shell con Ctrl+D	
			ptr = line; // Si se omite, al pulsar Ctrl+C produce "Violación de segmento (`core' generado)"
			ptr[0] = 0; // Si se omite esta línea aparece error ejecución ": no se encontró la orden"
		}
		
		//Limpiamos el buffer
		fflush(stdin);
		
		//Limpiamos los carácteres delimitadores de line
		strtok(line, "\t\n\r");
		
		return ptr;
	#endif
}

//Método que primeramente separa el comando en tokens mediante la función
//parse_args() y después verifica si se trata de un comando interno llamando
//a check_internal(), si es así lo ejecuta.
//Si el comando no es interno, se va a execute_line creando un proceso hijo con fork()
int execute_line(char *line){
	
	signal(SIGINT,ctrlc);	// Cuando llega un CTRL + C, llamamos la funcion apropiada
	signal(SIGCHLD,reaper);	// Cuando llega la señal SIGCHLD, llamamos al reaper

	char *args[ARGS_SIZE];
	memset(args,'\0',sizeof(args)); // Limpiamos args para no tener errores
	
	strcpy(jobs_list[0].command_line, line);  // Copiamos los argumentos no troceados en el primer el. de jobs_list 
	parse_args(args, line);
	
	if (!check_internal(args)) {
		//printf("Detectado comando externo!\n");
		pid_t pid = fork();
		jobs_list[0].pid = pid;

		int error;	// Eventual codigo de error del proceso hijo

		if (pid == 0) {	// Si el pid es 0, estamos en el proceso hijo
			is_output_redirection(args);
			error = execvp(args[0], args);
			//printf("ERROR en la ejecuciòn del proceso hijo - Codigo %d\n",error);
			exit(1);
		}
		else if (pid > 0) {	// Proceso padre
							// Empleamos pause() para escuchar señales del hijo
			while (jobs_list[0].pid != 0) {
				pause();
			}
		}
	}
	return 1;
}

void reaper(int signum) {
	pid_t pid;
	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
		if(pid == jobs_list[0].pid) {
			jobs_list[0].pid = 0;
			jobs_list[0].status = 'F';
			strcpy(jobs_list[0].command_line,"");
		}
	}
}

void ctrlc(int signum) {
	signal(SIGINT, ctrlc);
	printf("\nDetectado Control + C\n");
	if (jobs_list[0].pid > 0) {  // Si hay un proceso en foreground
		if (jobs_list[0].pid != shell_pid) {	// Si el proceso en foreground no es el minishell
			kill(jobs_list[0].pid,SIGTERM);
		} else if (jobs_list[0].pid == shell_pid) {
			printf("Señal SIGTERM NO ENVIADA al proceso n.%d porque es el mini shell.\n",jobs_list[0].pid);
		}
	} else {
		printf("Señal SIGTERM NO ENVIADA porque no hay proceso en foreground.\n");
	}
}

//Método que separa line en tokens y los guarda en args.
int parse_args(char **args, char *line) {
	
	char *token;
	int contador = 0;
	//El carácter delimitador de strtok() será un espacio
	const char s[2] = " ";
	
	//Limpiamos los comentarios de line
	strtok(line, "#");
	
	//Leemos el primer token
	token = strtok(line, s);
	//Bucle while que lee y guarda los tokens en args
	while(token != NULL) {
		args[contador] = token;
		contador++;
		token = strtok(NULL, s);
	}
	//Si contador es 1 limpiamos el siguiente args para
	//evitar errores posteriores.
	if (contador == 1) {
		args[1] = NULL;
	}	
	return contador;
}

//Método que verifica si se trata de un comando interno, 
//en ese caso lo ejecuta y devuelve 1.
int check_internal(char **args){
	if (strcmp(args[0], "cd") == 0) {
		internal_cd(args);
		return 1;
	} else if (strcmp(args[0], "export") == 0) {
		internal_export(args);
		return 1;
	} else if (strcmp(args[0], "source") == 0) {
		internal_source(args);
		return 1;
	} else if (strcmp(args[0], "jobs") == 0) {
		internal_jobs(args);
		return 1;
	} else if (strcmp(args[0], "exit") == 0) {
		exit(0);
	}
	return 0;
}

//Método que cambia al directorio indicado por args[1],
//en el caso de que args[1] sea NULL envia al HOME.
int internal_cd(char **args){
	if (args[1] == NULL || strlen(args[1]) == 0) {
		chdir(getenv("HOME"));
		return 1;
	} else if (chdir(args[1]) == -1) {
		printf("Directory %s not found\n", args[1]);
	}
	return 0;
}

//Método que cambia el tipo de env que se le pasa por parámetro
//después de un igual (=), para ello descomponemos el argumento
//con la función strtok().
int internal_export(char **args){

	if (args[1] == NULL) {
		printf("Syntax Error. Use: export Name=Value\n");
		return 1;
	}
	const char igual = '=';
	char *pos = strchr(args[1], igual); // Lo que hay después del primer =
	if (pos == NULL) {
		printf("Syntax Error. Use: export Name=Value\n");
		return 1;
	}
	char *token = strtok(args[1], "=");
	char *nombre = token;
	char *valor = NULL;
	(*pos)++;
	strcpy(valor,pos);

	printf("Name: %s\n", nombre);
	printf("Value: %s\n", valor);
	if (valor != NULL) {
		if (getenv(nombre) != NULL) {
			printf("Antiguo valor para %s: %s\n", nombre, getenv(nombre));
			printf("Nuevo valor para %s: %s\n", nombre, valor);
			setenv(nombre, valor, 1);
		} else {
			printf("Syntax Error. Introduce a valid name\n");
		}
	}
	return 0;
}

int internal_source(char **args){
	// Abre un fichero y ejecuta el comando presente en cada linea
	// enviandolo al executeline

	// Se usa fgets()
	// char *fgets(char *str, int n, FILE *stream)
	// con: str: donde se almacena la string leida
	//      n: numero maximo de caracteres a leer
	//      stream: puntero al stream de datos

	// stream = fopen("nombrefile", "r")
	if (args[1] == NULL) {
		printf("Syntax Error. Use: source <filename>\n");
		return 1;
	} else {
	FILE *file = fopen(args[1],"r"); // abrimos el fichero pasado como argumento en modo solo lectura
		if (file) {
			char readcommand[COMMAND_LINE_SIZE];

			while (fgets(readcommand, COMMAND_LINE_SIZE, file)) {
				fflush(file);
				execute_line(readcommand);
			}

		fclose(file); // cerramos el fichero
		return 0;

		} else {
			return 1;
		}
	}
}

int internal_jobs(char **args){
	printf("Hacemos JOBS\n");
	return 0;
}

int is_output_redirection (char **args){
	int i = 0;
	while (args[i] != NULL) {
		if (strcmp(args[i],">") == 0) {
			if (args[i+1] != NULL) {
				int descriptor = open(args[i+1], O_WRONLY | O_CREAT, S_IRWXU);
				dup2(descriptor,STDOUT_FILENO);
				close(descriptor);
				args[i] = NULL;
				args[i+1] = NULL;
				return 1;
			} else {
				printf("Syntax Error. Use: command > file\n");
			}
		}	
		i++;
	}
	return 0;
}