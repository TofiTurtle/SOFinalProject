// memtest.c - Suite de pruebas para sistema de memoria xv6
// Compilar: agregar "memtest" a UPROGS en Makefile

#include "types.h"
#include "stat.h"
#include "user.h"

// Colores para output (si tu terminal lo soporta)
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define YELLOW "\033[0;33m"
#define BLUE "\033[0;34m"
#define RESET "\033[0m"

// Macros de utilidad
#define PGSIZE 4096
#define MB (1024 * 1024)
#define KB (1024)

// Estadísticas globales
int tests_passed = 0;
int tests_failed = 0;

// ==============================================================================
// FUNCIONES DE UTILIDAD
// ==============================================================================

void print_header(char *title) {
    printf(1, "\n%s========================================%s\n", BLUE, RESET);
    printf(1, "%s  %s%s\n", BLUE, title, RESET);
    printf(1, "%s========================================%s\n\n", BLUE, RESET);
}

void print_test(char *name) {
    printf(1, "[TEST] %s... ", name);
}

void print_pass(void) {
    printf(1, "%s[PASS]%s\n", GREEN, RESET);
    tests_passed++;
}

void print_fail(char *reason) {
    printf(1, "%s[FAIL]%s %s\n", RED, RESET, reason);
    tests_failed++;
}

void print_info(char *msg) {
    printf(1, "  %s[INFO]%s %s\n", YELLOW, RESET, msg);
}

// ==============================================================================
// TEST 1: ASIGNACIÓN BÁSICA DE MEMORIA
// ==============================================================================

void test_basic_allocation(void) {
    print_test("Asignación básica con sbrk");
    
    char *p = sbrk(PGSIZE);
    if (p == (char*)-1) {
        print_fail("sbrk falló al asignar una página");
        return;
    }
    
    // Escribir en la página para verificar que está mapeada
    p[0] = 'A';
    p[PGSIZE-1] = 'Z';
    
    if (p[0] == 'A' && p[PGSIZE-1] == 'Z') {
        print_pass();
    } else {
        print_fail("No se puede escribir en la memoria asignada");
    }
}

// ==============================================================================
// TEST 2: ASIGNACIÓN MÚLTIPLE
// ==============================================================================

void test_multiple_allocation(void) {
    print_test("Asignación múltiple de páginas");
    
    int pages = 10;
    char *start = sbrk(0);
    char *p = sbrk(pages * PGSIZE);
    
    if (p == (char*)-1) {
        print_fail("sbrk falló al asignar múltiples páginas");
        return;
    }
    
    // Escribir en cada página
    int success = 1;
    for (int i = 0; i < pages; i++) {
        char *page = start + (i * PGSIZE);
        page[0] = 'A' + i;
        page[PGSIZE-1] = 'Z' - i;
        
        if (page[0] != 'A' + i || page[PGSIZE-1] != 'Z' - i) {
            success = 0;
            break;
        }
    }
    
    if (success) {
        print_pass();
        printf(1, "       Asignadas y verificadas %d páginas (%d KB)\n", 
               pages, (pages * PGSIZE) / KB);
    } else {
        print_fail("Error al escribir en páginas asignadas");
    }
}

// ==============================================================================
// TEST 3: LAZY ALLOCATION (si está implementado)
// ==============================================================================

void test_lazy_allocation(void) {
    print_test("Lazy allocation con page fault");
    
    int old_size = sbrk(0);
    
    // Pedir mucha memoria sin usarla
    char *p = sbrk(100 * PGSIZE);
    if (p == (char*)-1) {
        print_fail("sbrk falló");
        return;
    }
    
    int new_size = sbrk(0);
    printf(1, "\n       Heap expandido de %d a %d bytes\n", old_size, new_size);
    
    // Ahora acceder a una página en el medio
    // Si es lazy, esto debería causar un page fault y asignar la página
    p[50 * PGSIZE] = 'X';
    
    if (p[50 * PGSIZE] == 'X') {
        print_pass();
        print_info("Lazy allocation funcionando (o eager)");
    } else {
        print_fail("No se puede acceder a memoria solicitada");
    }
}

// ==============================================================================
// TEST 4: FORK Y COPY-ON-WRITE
// ==============================================================================

void test_fork_memory(void) {
    print_test("Fork y copia de memoria");
    
    // Asignar memoria y escribir datos
    char *p = sbrk(PGSIZE);
    if (p == (char*)-1) {
        print_fail("No se pudo asignar memoria");
        return;
    }
    
    for (int i = 0; i < PGSIZE; i++) {
        p[i] = (i % 256);
    }
    
    int pid = fork();
    
    if (pid < 0) {
        print_fail("Fork falló");
        return;
    }
    
    if (pid == 0) {
        // Proceso hijo: verificar que tiene copia de los datos
        int child_ok = 1;
        for (int i = 0; i < PGSIZE && child_ok; i++) {
            if (p[i] != (i % 256)) {
                child_ok = 0;
            }
        }
        
        // Modificar datos en hijo
        p[0] = 'C';
        p[100] = 'H';
        
        exit();
    } else {
        // Proceso padre: esperar al hijo
        wait();
        
        // Verificar que los datos del padre no fueron modificados
        // (esto verifica COW o copia profunda)
        if (p[0] == (0 % 256) && p[100] == (100 % 256)) {
            print_pass();
            print_info("Memoria del padre protegida del hijo");
        } else {
            print_fail("Hijo modificó memoria del padre (posible bug en COW)");
        }
    }
}

// ==============================================================================
// TEST 5: LÍMITES DE MEMORIA
// ==============================================================================

void test_memory_limits(void) {
    print_test("Límites de memoria del sistema");
    
    int pages_allocated = 0;
    char *p;
    
    // Intentar asignar memoria hasta fallar
    while (1) {
        p = sbrk(PGSIZE);
        if (p == (char*)-1) {
            break;
        }
        pages_allocated++;
        
        // Escribir en la página para forzar asignación física
        p[0] = 'X';
        
        // Límite de seguridad para no colgar el sistema
        if (pages_allocated > 1000) {
            break;
        }
    }
    
    if (pages_allocated > 0) {
        print_pass();
        printf(1, "       Asignadas %d páginas (%d KB) antes de fallar\n",
               pages_allocated, (pages_allocated * PGSIZE) / KB);
    } else {
        print_fail("No se pudo asignar ninguna página");
    }
    
    // Liberar memoria
    sbrk(-pages_allocated * PGSIZE);
}

// ==============================================================================
// TEST 6: FRAGMENTACIÓN Y ACCESO ALEATORIO
// ==============================================================================

void test_random_access(void) {
    print_test("Acceso aleatorio a memoria");
    
    int pages = 50;
    char *p = sbrk(pages * PGSIZE);
    
    if (p == (char*)-1) {
        print_fail("No se pudo asignar memoria");
        return;
    }
    
    // Escribir en posiciones aleatorias
    int seed = 42;
    for (int i = 0; i < 1000; i++) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        int offset = seed % (pages * PGSIZE);
        p[offset] = (char)(seed % 256);
    }
    
    // Verificar algunos valores
    seed = 42;
    int errors = 0;
    for (int i = 0; i < 1000; i++) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        int offset = seed % (pages * PGSIZE);
        char expected = (char)(seed % 256);
        
        if (p[offset] != expected) {
            errors++;
        }
    }
    
    if (errors == 0) {
        print_pass();
        printf(1, "       Verificados 1000 accesos aleatorios\n");
    } else {
        print_fail("Errores en lectura de memoria");
        printf(1, "       %d errores de 1000 accesos\n", errors);
    }
}

// ==============================================================================
// TEST 7: STRESS TEST - FORK MÚLTIPLE
// ==============================================================================

void test_fork_stress(void) {
    print_test("Stress test con múltiples forks");
    
    int num_children = 5;
    int child_alloc = 10; // páginas por hijo
    
    for (int i = 0; i < num_children; i++) {
        int pid = fork();
        
        if (pid < 0) {
            print_fail("Fork falló en stress test");
            return;
        }
        
        if (pid == 0) {
            // Proceso hijo: asignar memoria y trabajar
            char *p = sbrk(child_alloc * PGSIZE);
            if (p == (char*)-1) {
                exit();
            }
            
            // Escribir datos
            for (int j = 0; j < child_alloc * PGSIZE; j++) {
                p[j] = (char)((i + j) % 256);
            }
            
            // Verificar datos
            for (int j = 0; j < child_alloc * PGSIZE; j++) {
                if (p[j] != (char)((i + j) % 256)) {
                    exit();
                }
            }
            
            exit();
        }
    }
    
    // Padre espera a todos los hijos
    int children_ok = 0;
    for (int i = 0; i < num_children; i++) {
        int status = wait();
        if (status >= 0) {
            children_ok++;
        }
    }
    
    if (children_ok == num_children) {
        print_pass();
        printf(1, "       %d procesos hijo completados exitosamente\n", num_children);
    } else {
        print_fail("Algunos procesos hijo fallaron");
        printf(1, "       %d de %d exitosos\n", children_ok, num_children);
    }
}

// ==============================================================================
// TEST 8: MEDICIÓN DE RENDIMIENTO
// ==============================================================================

void test_performance(void) {
    print_test("Medición de rendimiento de asignación");
    
    int iterations = 100;
    int pages_per_iter = 10;
    
    int start_tick = uptime();
    
    for (int i = 0; i < iterations; i++) {
        char *p = sbrk(pages_per_iter * PGSIZE);
        if (p == (char*)-1) {
            break;
        }
        
        // Forzar acceso a cada página
        for (int j = 0; j < pages_per_iter; j++) {
            p[j * PGSIZE] = 'X';
        }
    }
    
    int end_tick = uptime();
    int elapsed = end_tick - start_tick;
    
    // Liberar
    sbrk(-iterations * pages_per_iter * PGSIZE);
    
    print_pass();
    printf(1, "       %d iteraciones en %d ticks\n", iterations, elapsed);
    printf(1, "       ~%d páginas/segundo\n", 
           (iterations * pages_per_iter * 100) / (elapsed > 0 ? elapsed : 1));
}

// ==============================================================================
// TEST 9: DEALLOCACIÓN
// ==============================================================================

void test_deallocation(void) {
    print_test("Deallocación con sbrk negativo");
    
    int initial_size = sbrk(0);
    
    // Asignar
    char *p = sbrk(20 * PGSIZE);
    if (p == (char*)-1) {
        print_fail("No se pudo asignar memoria");
        return;
    }
    
    int after_alloc = sbrk(0);
    
    // Escribir datos
    for (int i = 0; i < 20 * PGSIZE; i += PGSIZE) {
        p[i] = 'X';
    }
    
    // Deallocar la mitad
    sbrk(-10 * PGSIZE);
    
    int after_dealloc = sbrk(0);
    
    if (after_dealloc < after_alloc && after_dealloc > initial_size) {
        print_pass();
        printf(1, "       Inicial: %d, Después alloc: %d, Después dealloc: %d\n",
               initial_size, after_alloc, after_dealloc);
    } else {
        print_fail("Deallocación no funcionó correctamente");
    }
}

// ==============================================================================
// TEST 10: ZERO-FILL VERIFICATION
// ==============================================================================

void test_zero_fill(void) {
    print_test("Verificación de páginas limpiadas (zero-fill)");
    
    char *p = sbrk(5 * PGSIZE);
    if (p == (char*)-1) {
        print_fail("No se pudo asignar memoria");
        return;
    }
    
    int non_zero = 0;
    for (int i = 0; i < 5 * PGSIZE; i++) {
        if (p[i] != 0) {
            non_zero++;
        }
    }
    
    if (non_zero == 0) {
        print_pass();
        print_info("Páginas nuevas correctamente inicializadas a cero");
    } else {
        print_fail("Páginas contienen datos basura");
        printf(1, "       %d bytes no-cero encontrados\n", non_zero);
    }
}

// ==============================================================================
// MAIN - EJECUTAR TODAS LAS PRUEBAS
// ==============================================================================

int main(int argc, char *argv[]) {
    print_header("SUITE DE PRUEBAS DE MEMORIA XV6");
    
    printf(1, "Tamaño de página: %d bytes (%d KB)\n", PGSIZE, PGSIZE/KB);
    printf(1, "Heap inicial: %d bytes\n\n", sbrk(0));
    
    // Ejecutar tests básicos
    print_header("TESTS BÁSICOS");
    test_basic_allocation();
    test_multiple_allocation();
    test_zero_fill();
    test_deallocation();
    
    // Tests de funcionalidad avanzada
    print_header("TESTS DE FUNCIONALIDAD");
    test_lazy_allocation();
    test_fork_memory();
    test_random_access();
    
    // Tests de límites y stress
    print_header("TESTS DE LÍMITES Y STRESS");
    test_memory_limits();
    test_fork_stress();
    
    // Tests de rendimiento
    print_header("TESTS DE RENDIMIENTO");
    test_performance();
    
    // Resumen final
    print_header("RESUMEN");
    printf(1, "%sPruebas exitosas: %d%s\n", GREEN, tests_passed, RESET);
    printf(1, "%sPruebas fallidas: %d%s\n", RED, tests_failed, RESET);
    printf(1, "Total: %d pruebas\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf(1, "\n%s¡Todas las pruebas pasaron!%s 🎉\n\n", GREEN, RESET);
    } else {
        printf(1, "\n%sAlgunas pruebas fallaron%s ⚠️\n\n", RED, RESET);
    }
    
    exit();
}