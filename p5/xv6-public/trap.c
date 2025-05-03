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
  case T_PGFLT: // T_PGFLT = 14

    uint addr = rcr2();       // addr causing page fault
    struct proc *p = myproc();

    int map_index = -1;
    uint map_start = 0;
    uint map_end = 0;
    for (int i = 0; i < MAX_WMMAP_INFO; i++) {  // get mapping corresponding to address
      
      map_start = p->mappings[i].addr;
      if (map_start == 0) continue; // unused mapping

      map_end = map_start + p->mappings[i].length; // not inclusive

      if (addr >= map_start && addr < map_end) {
        map_index = i;
        break;
      }

    }


    if (map_index >= 0) {// lazy allocation

      int flags = p->mappings[map_index].flags;
      int fd = p->mappings[map_index].fd;

      struct file *file = 0;
      if (flags & MAP_ANONYMOUS || fd < 0) {
        fd = -1;
      } else {
      
        file = p->ofile[fd];
        int offset = addr - map_start;
        file->off = (uint) PGROUNDDOWN(offset);

      }

      char *mem = kalloc();
      memset(mem, 0, PGSIZE); // default all values to 0
      mappages(p->pgdir, (void *) PGROUNDDOWN(addr), PGSIZE, V2P(mem), PTE_U | PTE_W);

      if (fd >= 0) {
        int b = fileread(file, (void *) PGROUNDDOWN(addr), PGSIZE);
        if (b < 0) {  // failed to read file
          kfree(mem);
          exit();
        }
        file->off = 0;
      }

      p->mappings[map_index].pages_in_mem++;
      
      break;
      
    } else {

        if (addr > KERNBASE) {
          cprintf("Segmentation Fault: %x\n", addr);
          exit();
        }

        pte_t *pte = walkpgdir(p->pgdir, (void *) PGROUNDDOWN(addr), 0);

        if ( !(*pte & PTE_RW)) {   // check if it is a COW page
          cprintf("Segmentation Fault: %x\n", addr);
          cprintf("pte: %x\n", *pte);
          exit();
        }

        uint pa = PTE_ADDR(*pte);
        if (get_pg_ref(pa) == 1) {  // only process that uses page, so just make page writable

          *pte |= PTE_W;

        } else {      // otherwise if there are multiple references to this page, decrement the ref and allocate a new page

          *pte = 0;
          char *mem = kalloc();
          memmove(mem, (char*)P2V(pa), PGSIZE);
          mappages(p->pgdir, (void*)PGROUNDDOWN(addr), PGSIZE, V2P(mem), PTE_W | PTE_U);
          
          dec_page(pa);   // decrement the reference count

        }

        lcr3(V2P(p->pgdir));

        break;

    }
    
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
