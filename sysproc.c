#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"


extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// Implementación de la nueva syscall
int
sys_setpriority(void)
{
  int pid;
  int new_priority;
  struct proc *p;

  // 1. Obtener los argumentos de usuario
  if(argint(0, &pid) < 0 || argint(1, &new_priority) < 0)
    return -1;

  // 2. Validar el rango de prioridad (asumimos 1 a 5)
  if (new_priority < 1 || new_priority > 5)
    return -1;

  // 3. Buscar y modificar el proceso
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      // NUEVO SCHEDULER: Actualizar la prioridad y reiniciar el aging
      p->priority = new_priority;
      p->ticks_waiting = 0; 
      release(&ptable.lock);
      return 0; // Éxito
    }
  }
  release(&ptable.lock);
  return -1; // Proceso no encontrado
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
