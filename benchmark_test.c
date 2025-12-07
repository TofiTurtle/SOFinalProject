#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_PROCESSES 10
#define HIGH_PRIO 5
#define LOW_PRIO 1
#define ITERATIONS 100000000

void cpu_bound_task(int pid, int prio, int start_time) {
    long i;
    volatile long sum = 0;
    
    for (i = 0; i < ITERATIONS; i++) {
        sum += i;
    }
    
    int end_time = uptime();
    int elapsed = end_time - start_time;
    
    printf(1, "[T=%d] PID %d (Prio %d): TERMINADO - Tiempo: %d ticks\n", 
           end_time, pid, prio, elapsed);
    exit();
}

int main(void) {
    int i, pid, priority_to_set;
    int global_start = uptime();
    
    printf(1, "\n========================================\n");
    printf(1, "  BENCHMARK DE SCHEDULER\n");
    printf(1, "========================================\n");
    printf(1, "Tiempo inicio: T=%d\n\n", global_start);

    for (i = 0; i < NUM_PROCESSES; i++) {
        pid = fork();
        if (pid == 0) {
            int my_pid = getpid();
            int my_start = uptime();
            
            priority_to_set = (i < 2) ? HIGH_PRIO : LOW_PRIO;
            setpriority(my_pid, priority_to_set);
            
            printf(1, "[T=%d] PID %d: Iniciando (Prioridad %d)\n", 
                   my_start, my_pid, priority_to_set);
            
            cpu_bound_task(my_pid, priority_to_set, my_start);
        }
    }

    for (i = 0; i < NUM_PROCESSES; i++) {
        wait();
    }
    
    int global_end = uptime();
    printf(1, "\n========================================\n");
    printf(1, "TIEMPO TOTAL: %d ticks\n", global_end - global_start);
    printf(1, "========================================\n\n");
    exit();
}