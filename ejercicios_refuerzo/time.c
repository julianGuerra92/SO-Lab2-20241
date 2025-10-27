#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    struct timeval start, end;
    pid_t pid;
    int status;
    
    // Verificar que se haya proporcionado un comando
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <command> [args...]\n", argv[0]);
        return 1;
    }
    
    // Obtener el tiempo inicial
    gettimeofday(&start, NULL);
    
    // Crear proceso hijo
    pid = fork();
    
    if (pid < 0) {
        // Error al crear el proceso
        perror("Error en fork");
        return 1;
    }
    else if (pid == 0) {
        // Proceso hijo: ejecutar el comando
        execvp(argv[1], &argv[1]);
        
        // Si execvp retorna, hubo un error
        perror("Error al ejecutar el comando");
        exit(1);
    }
    else {
        // Proceso padre: esperar a que el hijo termine
        waitpid(pid, &status, 0);
        
        // Obtener el tiempo final
        gettimeofday(&end, NULL);
        
        // Calcular el tiempo transcurrido
        double elapsed = (end.tv_sec - start.tv_sec) + 
                        (end.tv_usec - start.tv_usec) / 1000000.0;
        
        // Mostrar el tiempo transcurrido
        printf("\nElapsed time: %.5f\n", elapsed);
    }
    
    return 0;
}