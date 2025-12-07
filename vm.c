#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.


/**
 * allocuvm - Incrementa el tamaño del heap de un proceso usando lazy allocation
 * @pgdir: Directorio de páginas del proceso
 * @oldsz: Tamaño actual del proceso en bytes
 * @newsz: Nuevo tamaño deseado del proceso en bytes
 * 
 * CAMBIO PARA LAZY ALLOCATION:
 * En lugar de asignar físicamente todas las páginas entre oldsz y newsz,
 * esta versión modificada simplemente actualiza el tamaño del proceso.
 * La memoria física real se asignará más tarde, bajo demanda, cuando el
 * proceso intente acceder a esas direcciones (causando un page fault).
 * 
 * Returns: Nuevo tamaño del proceso, o 0 si hay error
 */
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  // Validación: El nuevo tamaño no puede invadir el espacio del kernel
  if(newsz >= KERNBASE)
    return 0;
  
  // Si el nuevo tamaño es menor, no hay nada que asignar
  if(newsz < oldsz)
    return oldsz;
  
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.


/**
 * deallocuvm - Reduce el tamaño del proceso liberando páginas
 * @pgdir: Directorio de páginas del proceso
 * @oldsz: Tamaño actual del proceso en bytes
 * @newsz: Nuevo tamaño (menor) del proceso en bytes
 * 
 * CAMBIO PARA LAZY ALLOCATION:
 * Debe manejar correctamente el caso donde algunas páginas en el rango
 * [newsz, oldsz) nunca fueron asignadas físicamente (lazy allocation).
 * Solo liberamos páginas que tienen el bit PTE_P activado.
 * 
 * Returns: Nuevo tamaño del proceso
 */
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    
    // Si no existe entrada en la tabla de páginas, saltar al siguiente bloque
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){

      // CAMBIO: Solo liberamos si PTE_P está activado

      // Con lazy allocation, algunas páginas pueden tener entradas en la
      // tabla de páginas pero sin el bit PTE_P, lo que significa que nunca
      // fueron asignadas físicamente. No intentamos liberar estas páginas.
      
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);       // Liberar la memoria física
      *pte = 0;       // Limpiar la entrada de la tabla de páginas
    }
    // Si PTE_P no está activado, la página nunca fue asignada
    // No hay memoria física que liberar, simplemente continuamos
  }
  return newsz;
}


// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.


/**
 * copyuvm - Crea una copia del espacio de direcciones de un proceso
 * @pgdir: Directorio de páginas del proceso padre
 * @sz: Tamaño del proceso
 * 
 * CAMBIO PARA LAZY ALLOCATION:
 * Durante fork(), solo copiamos las páginas que realmente fueron asignadas.
 * Las páginas lazy (sin PTE_P) se omiten; el hijo las asignará bajo demanda.
 * 
 * Returns: Puntero al nuevo directorio de páginas, o 0 si hay error
 */
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
    
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    

    // CAMBIO: Saltar páginas no presentes (lazy allocation)
  
    // Si la página no tiene el bit PTE_P, significa que nunca fue asignada
    // debido a lazy allocation. No la copiamos; el proceso hijo la asignará
    // bajo demanda cuando la acceda.
  
    if(!(*pte & PTE_P))
      continue;  // Saltar esta página y continuar con la siguiente
      
    // La página está presente, proceder con la copia normal
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

/**
 * handle_page_fault - Asigna memoria física cuando ocurre un page fault
 * @va: Dirección virtual que causó el page fault
 * 
 * Esta función implementa el núcleo de la lazy allocation. Es llamada desde
 * el manejador de traps (trap.c) cuando la CPU genera un page fault (T_PGFLT).
 * 
 * Funcionamiento:
 * 1. Verifica que la dirección esté en un rango válido del proceso
 * 2. Obtiene/crea la entrada correspondiente en la tabla de páginas
 * 3. Asigna una página física con kalloc()
 * 4. Mapea la página física a la dirección virtual
 * 5. Activa el bit PTE_P para marcar la página como presente
 * 
 * Validaciones:
 * - La dirección debe estar entre el stack y el límite del heap (curproc->sz)
 * - No debe estar en el guard page del stack
 * - Debe haber memoria física disponible
 * 
 * Returns: 0 si se manejó exitosamente, -1 si hay error
 */
int
handle_page_fault(uint va)
{
  struct proc *curproc = myproc();
  char *mem;
  pte_t *pte;
  uint a;

  // PASO 1: Validar la dirección virtual
 
  // Alinear la dirección a límite de página (4KB)
  a = PGROUNDDOWN(va);

  // Verificar que la dirección esté dentro del espacio válido del proceso
  // - No debe ser mayor o igual al tamaño del heap (curproc->sz)
  // - No debe estar por debajo del stack pointer (sería el guard page)
  if(a >= curproc->sz || a < PGROUNDUP(curproc->tf->esp)) {
    // Acceso inválido: fuera de los límites permitidos
    cprintf("handle_page_fault: invalid address 0x%x (sz=0x%x, esp=0x%x)\n",
            a, curproc->sz, curproc->tf->esp);
    return -1;
  }

  // PASO 2: Obtener o crear la entrada en la tabla de páginas
  // walkpgdir con alloc=1 crea las estructuras necesarias si no existen
  pte = walkpgdir(curproc->pgdir, (char*)a, 1);
  if(pte == 0) {
    cprintf("handle_page_fault: walkpgdir failed for address 0x%x\n", a);
    return -1;
  }

  // PASO 3: Verificar que la página no esté ya presente
  // Si PTE_P está activado, no es un caso de lazy allocation
  if(*pte & PTE_P) {
    cprintf("handle_page_fault: page already present at 0x%x\n", a);
    return -1;
  }

  // PASO 4: Asignar memoria física

  mem = kalloc();
  if(mem == 0) {
    cprintf("handle_page_fault: out of memory for address 0x%x\n", a);
    return -1;
  }


  // PASO 5: Inicializar la página con ceros
  // Importante: Las páginas nuevas deben estar en cero (especificación POSIX)
  memset(mem, 0, PGSIZE);


  // PASO 6: Mapear la página con permisos apropiados
  // PTE_P: Página presente en memoria física
  // PTE_W: Página escribible (read/write)
  // PTE_U: Accesible desde modo usuario
  *pte = V2P(mem) | PTE_P | PTE_W | PTE_U;

  return 0;  // Éxito
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.