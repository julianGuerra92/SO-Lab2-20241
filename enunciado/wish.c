#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

typedef struct
{
  char *name;
  int (*handler)(char **args);
} command_t;

int cmd_ls(char **args);
int cmd_exit(char **args);
int cmd_cd(char **args);

command_t builtin_commands[] = {
    {"ls", cmd_ls},
    {"exit", cmd_exit},
    {"cd", cmd_cd},
    {NULL, NULL}};

int cmd_ls(char **args)
{
  DIR *dir;
  struct dirent *entry;
  char *path = ".";

  if (args[1] != NULL)
  {
    path = args[1];
  }

  dir = opendir(path);
  if (dir == NULL)
  {
    perror("ls");
    return 1;
  }

  while ((entry = readdir(dir)) != NULL)
  {
    if (entry->d_name[0] != '.')
    {
      printf("%s\n", entry->d_name);
    }
  }

  closedir(dir);
  return 1;
}

int cmd_exit(char **args)
{
  return 0;
}

int cmd_cd(char **args)
{
  if (args[1] == NULL || args[2] != NULL)
  {
    fprintf(stderr, "An error has occurred\n");
    return 1;
  }
  
  if (chdir(args[1]) != 0)
  {
    fprintf(stderr, "An error has occurred\n");
    return 1;
  }
  
  return 1;
}

char **parse_line(char *line)
{
  char **tokens = malloc(MAX_ARGS * sizeof(char *));
  char *token;
  int position = 0;

  if (tokens == NULL)
  {
    fprintf(stderr, "wish: allocation error\n");
    exit(EXIT_FAILURE);
  }

  // Eliminar salto de línea al final
  char *newline = strchr(line, '\n');
  if (newline) *newline = '\0';
  
  token = strtok(line, " \t\r\a");
  while (token != NULL && position < MAX_ARGS - 1)
  {
    tokens[position] = token;
    position++;
    token = strtok(NULL, " \t\r\a");
  }
  tokens[position] = NULL;

  return tokens;
}

int execute_builtin(char **args)
{
  if (args[0] == NULL)
  {
    return 1; // Línea vacía, continuar
  }

  for (int i = 0; builtin_commands[i].name != NULL; i++)
  {
    if (strcmp(args[0], builtin_commands[i].name) == 0)
    {
      return builtin_commands[i].handler(args);
    }
  }

  return -1;
}

int execute_external(char **args)
{
  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0)
  {
    if (execvp(args[0], args) == -1)
    {
      perror("wish");
    }
    exit(EXIT_FAILURE);
  }
  else if (pid < 0)
  {
    perror("wish");
  }
  else
  {
    do
    {
      waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  return 1;
}

int execute(char **args)
{
  int result = execute_builtin(args);

  if (result == -1)
  {
    return execute_external(args);
  }

  return result;
}

void shell_loop(FILE *input)
{
  char *line = NULL;
  size_t bufsize = 0;
  char **args;
  int status = 1;

  do
  {
    if (input == stdin)
    {
      printf("wish> ");
    }
    
    if (getline(&line, &bufsize, input) == -1)
    {
      break; // EOF
    }
    
    args = parse_line(line);
    status = execute(args);

    free(args);
  } while (status);

  free(line);
}

int main(int argc, char **argv)
{
  FILE *input = stdin;
  
  if (argc == 2)
  {
    input = fopen(argv[1], "r");
    if (input == NULL)
    {
      fprintf(stderr, "An error has occurred\n");
      exit(1);
    }
  }
  else if (argc > 2)
  {
    fprintf(stderr, "An error has occurred\n");
    exit(1);
  }
  
  shell_loop(input);
  
  if (input != stdin)
  {
    fclose(input);
  }
  
  return EXIT_SUCCESS;
}
