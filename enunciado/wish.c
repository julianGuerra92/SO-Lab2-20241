#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_PATHS 32
#define MAX_COMMANDS 32

// Variables globales para el path
char *search_paths[MAX_PATHS];
int num_paths = 0;

// Mensaje de error único
char error_message[30] = "An error has occurred\n";

// Estructura para comando con redirección
typedef struct {
    char **args;
    char *redirect_file;
} command_info_t;

// Prototipos de funciones
void print_error();
void init_path();
char *find_executable(char *cmd);
void parse_line(char *line, command_info_t **commands, int *num_commands);
char **parse_command(char *cmd_str, char **redirect_file);
int execute_builtin(char **args, char *redirect_file);
void execute_external(char **args, char *redirect_file);
void execute_parallel(command_info_t **commands, int num_commands);
void shell_loop(FILE *input, int interactive);

// Función para imprimir el error estándar
void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

// Inicializar el path por defecto
void init_path() {
    search_paths[0] = strdup("/bin");
    num_paths = 1;
}

// Buscar ejecutable en el path
char *find_executable(char *cmd) {
    static char full_path[MAX_LINE];
    
    // Si el comando contiene '/', intentar usarlo directamente
    if (strchr(cmd, '/') != NULL) {
        if (access(cmd, X_OK) == 0) {
            return cmd;
        }
        return NULL;
    }
    
    // Buscar en cada directorio del path
    for (int i = 0; i < num_paths; i++) {
        snprintf(full_path, MAX_LINE, "%s/%s", search_paths[i], cmd);
        if (access(full_path, X_OK) == 0) {
            return full_path;
        }
    }
    
    return NULL;
}

// Parsear una línea completa (maneja & y > por comando)
void parse_line(char *line, command_info_t **commands, int *num_commands) {
    *num_commands = 0;
    
    // Eliminar newline
    line[strcspn(line, "\n")] = 0;
    
    // Dividir por &
    char *saveptr;
    char *cmd = strtok_r(line, "&", &saveptr);
    
    while (cmd != NULL && *num_commands < MAX_COMMANDS) {
        // Limpiar espacios al inicio y final
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        
        int len = strlen(cmd);
        while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t')) {
            cmd[len-1] = '\0';
            len--;
        }
        
        if (strlen(cmd) > 0) {
            command_info_t *cmd_info = malloc(sizeof(command_info_t));
            cmd_info->redirect_file = NULL;
            cmd_info->args = parse_command(cmd, &cmd_info->redirect_file);
            
            if (cmd_info->args != NULL && cmd_info->args[0] != NULL) {
                commands[*num_commands] = cmd_info;
                (*num_commands)++;
            } else {
                free(cmd_info);
            }
        }
        
        cmd = strtok_r(NULL, "&", &saveptr);
    }
}

// Parsear un comando individual con su posible redirección
char **parse_command(char *cmd_str, char **redirect_file) {
    char **args = malloc(MAX_ARGS * sizeof(char *));
    int position = 0;
    *redirect_file = NULL;
    
    // Verificar si hay redirección
    char *redir_pos = strchr(cmd_str, '>');
    if (redir_pos != NULL) {
        *redir_pos = '\0';
        redir_pos++;
        
        // Verificar múltiples >
        if (strchr(cmd_str, '>') != NULL || strchr(redir_pos, '>') != NULL) {
            print_error();
            args[0] = NULL;
            return args;
        }
        
        // Parsear el archivo de redirección
        char *token = strtok(redir_pos, " \t\n");
        if (token == NULL) {
            print_error();
            args[0] = NULL;
            return args;
        }
        *redirect_file = strdup(token);
        
        // Verificar que no hay más tokens (múltiples archivos)
        if (strtok(NULL, " \t\n") != NULL) {
            print_error();
            free(*redirect_file);
            *redirect_file = NULL;
            args[0] = NULL;
            return args;
        }
    }
    
    // Parsear los argumentos del comando
    char *saveptr;
    char *token = strtok_r(cmd_str, " \t\n", &saveptr);
    
    while (token != NULL && position < MAX_ARGS - 1) {
        args[position] = strdup(token);
        position++;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
    args[position] = NULL;
    
    // Verificar que hay al menos un comando si hay redirección
    if (*redirect_file != NULL && position == 0) {
        print_error();
        free(*redirect_file);
        *redirect_file = NULL;
        args[0] = NULL;
    }
    
    return args;
}

// Ejecutar comando built-in
int execute_builtin(char **args, char *redirect_file) {
    if (args[0] == NULL) {
        return 1; // Línea vacía
    }
    
    // Comando: exit
    if (strcmp(args[0], "exit") == 0) {
        if (args[1] != NULL) {
            print_error();
            return 1;
        }
        exit(0);
    }
    
    // Comando: cd
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL || args[2] != NULL) {
            print_error();
            return 1;
        }
        if (chdir(args[1]) != 0) {
            print_error();
        }
        return 1;
    }
    
    // Comando: path
    if (strcmp(args[0], "path") == 0) {
        // Limpiar paths anteriores
        for (int i = 0; i < num_paths; i++) {
            free(search_paths[i]);
        }
        num_paths = 0;
        
        // Agregar nuevos paths
        for (int i = 1; args[i] != NULL && num_paths < MAX_PATHS; i++) {
            search_paths[num_paths] = strdup(args[i]);
            num_paths++;
        }
        return 1;
    }
    
    return 0; // No es built-in
}

// Ejecutar comando externo
void execute_external(char **args, char *redirect_file) {
    if (args[0] == NULL) {
        return;
    }
    
    char *executable = find_executable(args[0]);
    if (executable == NULL) {
        print_error();
        return;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        // Proceso hijo
        
        // Manejar redirección
        if (redirect_file != NULL) {
            int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                print_error();
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        
        if (execv(executable, args) == -1) {
            print_error();
            exit(1);
        }
    } else if (pid < 0) {
        print_error();
    } else {
        // Proceso padre - esperar al hijo
        int status;
        waitpid(pid, &status, 0);
    }
}

// Ejecutar comandos en paralelo
void execute_parallel(command_info_t **commands, int num_commands) {
    if (num_commands == 0) {
        return;
    }
    
    pid_t pids[MAX_COMMANDS];
    int num_processes = 0;
    
    // Iniciar todos los procesos
    for (int i = 0; i < num_commands; i++) {
        if (commands[i] == NULL || commands[i]->args == NULL || commands[i]->args[0] == NULL) {
            continue;
        }
        
        // Verificar si es built-in
        int result = execute_builtin(commands[i]->args, commands[i]->redirect_file);
        if (result != 0) {
            continue; // Era built-in o línea vacía
        }
        
        // Es comando externo
        char *executable = find_executable(commands[i]->args[0]);
        if (executable == NULL) {
            print_error();
            continue;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            // Proceso hijo
            if (commands[i]->redirect_file != NULL) {
                int fd = open(commands[i]->redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    print_error();
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            
            if (execv(executable, commands[i]->args) == -1) {
                print_error();
                exit(1);
            }
        } else if (pid > 0) {
            pids[num_processes++] = pid;
        } else {
            print_error();
        }
    }
    
    // Esperar a todos los procesos
    for (int i = 0; i < num_processes; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

// Loop principal del shell
void shell_loop(FILE *input, int interactive) {
    char *line = NULL;
    size_t bufsize = 0;
    ssize_t len;
    
    while (1) {
        if (interactive) {
            printf("wish> ");
            fflush(stdout);
        }
        
        len = getline(&line, &bufsize, input);
        if (len == -1) {
            break; // EOF
        }
        
        // Parsear la línea
        command_info_t *commands[MAX_COMMANDS];
        int num_commands;
        
        parse_line(line, commands, &num_commands);
        
        if (num_commands == 0) {
            // Línea vacía
            continue;
        }
        
        if (num_commands == 1) {
            // Un solo comando (puede ser built-in o externo)
            int result = execute_builtin(commands[0]->args, commands[0]->redirect_file);
            if (result == 0) {
                // No es built-in, ejecutar como externo
                execute_external(commands[0]->args, commands[0]->redirect_file);
            }
        } else {
            // Múltiples comandos (paralelos)
            execute_parallel(commands, num_commands);
        }
        
        // Liberar memoria
        for (int i = 0; i < num_commands; i++) {
            if (commands[i] != NULL) {
                if (commands[i]->args != NULL) {
                    for (int j = 0; commands[i]->args[j] != NULL; j++) {
                        free(commands[i]->args[j]);
                    }
                    free(commands[i]->args);
                }
                if (commands[i]->redirect_file != NULL) {
                    free(commands[i]->redirect_file);
                }
                free(commands[i]);
            }
        }
    }
    
    if (line != NULL) {
        free(line);
    }
}

int main(int argc, char **argv) {
    // Inicializar el path por defecto
    init_path();
    
    // Verificar argumentos
    if (argc == 1) {
        // Modo interactivo
        shell_loop(stdin, 1);
    } else if (argc == 2) {
        // Modo batch
        FILE *file = fopen(argv[1], "r");
        if (file == NULL) {
            print_error();
            exit(1);
        }
        shell_loop(file, 0);
        fclose(file);
    } else {
        // Demasiados argumentos
        print_error();
        exit(1);
    }
    
    // Liberar paths
    for (int i = 0; i < num_paths; i++) {
        free(search_paths[i]);
    }
    
    return 0;
}