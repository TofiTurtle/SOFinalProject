// prio_test.c (Ejemplo de Código)

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
}