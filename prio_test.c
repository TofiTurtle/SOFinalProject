/*// prio_test.c (Ejemplo de Código)

#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_PROCESSES 10
#define HIGH_PRIO 5
#define LOW_PRIO 1
#define ITERATIONS 200000000 // Número de iteraciones para que la tarea sea larga

void cpu_bound_task(int pid, int prio) {
    long i;
    // Tarea que consume CPU
    for (i = 0; i < ITERATIONS; i++) {
        // La condición if(i % X == 0) es opcional para imprimir progreso, 
        // pero hace el bucle ligeramente menos puro en su uso de CPU.
    }
    printf(1, "PID %d (Prio %d): Tarea FINALIZADA.\n", pid, prio);
    exit();
}

int main(void) {
    int i;
    int pid;
    int priority_to_set;
    
    printf(1, "--- INICIANDO PRUEBA DE PLANIFICADOR DE PRIORIDAD ---\n");

    for (i = 0; i < NUM_PROCESSES; i++) {
        pid = fork();

        if (pid == 0) {
            // Código del hijo

            // Asignar prioridades: 2 Alta, 8 Baja (para ver el impacto del Aging)
            if (i < 2) {
                priority_to_set = HIGH_PRIO;
            } else {
                priority_to_set = LOW_PRIO;
            }
            
            // Llamada a la syscall que implementaste
            setpriority(getpid(), priority_to_set); 
            printf(1, "PID %d: Asignado Prioridad %d (Proceso %d/%d)\n", getpid(), priority_to_set, i + 1, NUM_PROCESSES);
            
            cpu_bound_task(getpid(), priority_to_set);
        }
    }

    // El proceso padre espera a todos los hijos
    for (i = 0; i < NUM_PROCESSES; i++) {
        wait();
    }
    
    printf(1, "--- PRUEBA COMPLETADA ---\n");
    exit();
}*/

// prio_test.c - Versión con sincronización de salida para múltiples CPUs

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NUM_PROCESSES 10
#define HIGH_PRIO 5
#define LOW_PRIO 1
#define ITERATIONS 200000000

// Función para obtener un lock simple usando un archivo
void acquire_print_lock(void) {
    int fd;
    // Intenta crear el archivo lock. Si ya existe, espera.
    while ((fd = open("print.lock", O_CREATE | O_RDWR)) < 0) {
        sleep(1);
    }
    close(fd);
}

void release_print_lock(void) {
    unlink("print.lock");
}

void cpu_bound_task(int pid, int prio) {
    long i;
    // Tarea que consume CPU
    for (i = 0; i < ITERATIONS; i++) {
        // Bucle puro de CPU
    }
    
    // Sincronizar la salida
    acquire_print_lock();
    printf(1, "PID %d (Prio %d): Tarea FINALIZADA.\n", pid, prio);
    release_print_lock();
    
    exit();
}

int main(void) {
    int i;
    int pid;
    int priority_to_set;
    
    // Limpiar cualquier lock previo
    unlink("print.lock");
    
    printf(1, "--- INICIANDO PRUEBA DE PLANIFICADOR DE PRIORIDAD ---\n");
    printf(1, "Nota: Ejecutando con multiples CPUs - las tareas pueden ejecutarse en paralelo\n");

    for (i = 0; i < NUM_PROCESSES; i++) {
        pid = fork();

        if (pid == 0) {
            // Código del hijo

            // Asignar prioridades: 2 Alta, 8 Baja
            if (i < 2) {
                priority_to_set = HIGH_PRIO;
            } else {
                priority_to_set = LOW_PRIO;
            }
            
            // Llamada a la syscall
            setpriority(getpid(), priority_to_set);
            
            // Sincronizar la salida
            acquire_print_lock();
            printf(1, "PID %d: Asignado Prioridad %d (Proceso %d/%d)\n", 
                   getpid(), priority_to_set, i + 1, NUM_PROCESSES);
            release_print_lock();
            
            cpu_bound_task(getpid(), priority_to_set);
        }
    }

    // El proceso padre espera a todos los hijos
    for (i = 0; i < NUM_PROCESSES; i++) {
        wait();
    }
    
    printf(1, "--- PRUEBA COMPLETADA ---\n");
    
    // Limpiar
    unlink("print.lock");
    
    exit();
}