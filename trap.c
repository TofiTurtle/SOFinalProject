#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// FUNCION NUEVA: Manejador de lazy allocation
// Esta funcion se llama cuando hay un page fault y asigna la memoria fisica que faltaba
int
handle_lazy_allocation(struct proc *p, uint faultaddr)
{
  char *mem;
  uint a;
  pde_t *pgdir = p->pgdir;
  
  // Alinear dirección al inicio de la página
  a = PGROUNDDOWN(faultaddr);
  
  // Verificar que la dirección esté dentro del rango válido del proceso
  if(a >= p->sz || a < PGROUNDUP(p->tf->esp)) {
    return -1; // Dirección inválida
  }
  
  // Asignar memoria física
  mem = kalloc();
  if(mem == 0){
    cprintf("[LAZY] handle_lazy_allocation: sin memoria disponible\n");
    return -1;
  }
  
  // Limpiar la página
  memset(mem, 0, PGSIZE);
  
  // Mapear la página en la tabla de páginas
  if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
    cprintf("handle_lazy_allocation: mappages fallo\n");
    kfree(mem);
    return -1;
  }
  
  cprintf("Pagina asignada en faultaddr=0x%x para pid=%d\n", faultaddr, p->pid);
  return 0;
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  
  // MODIFICACIÓN: Manejar page faults (T_PGFLT)
  case T_PGFLT:
    {
      uint faultaddr = rcr2(); // Obtener dirección que causó el fault
      struct proc *p = myproc();
      
      if(p == 0) {
        // Page fault en kernel - esto es un error
        cprintf("unexpected page fault in kernel at 0x%x\n", faultaddr);
        panic("trap");
      }
      
      // Intentar manejar con lazy allocation
      if(handle_lazy_allocation(p, faultaddr) == 0) {
        // Éxito: la página fue asignada, continuar ejecución
        break;
      }
      
      // Si llegamos aquí, el page fault no pudo ser manejado
      // (acceso inválido a memoria)
      cprintf("pid %d: page fault at 0x%x - invalid memory access\n", 
              p->pid, faultaddr);
      p->killed = 1;
    }
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}