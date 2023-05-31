#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern struct Queue Mlfq[5];
extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

int pagefaulthandler(void *va, pagetable_t pagetable);

void trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8)
  {
    // system call

    if (killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  }
  else if ((which_dev = devintr()) != 0)
  {
    // ok
  }
  else if ((r_scause() == 13 || r_scause() == 15))
  {

    if(r_stval() == 0)
    {
      // p->killed = 1;
      setkilled(p);
    }
    int res = pagefaulthandler((void *)r_stval(), p->pagetable);
    // 0 means all fine
    //-1 means mem is not alloated
    //-2 means address is invalid
    if (res == -1 || res == -2)
    {
      setkilled(p);
      //p->killed = 1;
    }
  }
  else
  {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if (killed(p))
    exit(-1);

  #if defined RR || LBS                 // These are preemptive
  // give up the CPU if this is a timer interrupt.
  // If the process needs to alarm, we must save current trapframe , call the handler and run function after returning to userspace
    if (which_dev == 2)
    {
      // alarm(n,fn) has been called, now we check if there is an alarm interval
      if (p->ticks > 0)
      {
        p->elapsed_ticks++;
        if (p->elapsed_ticks == p->ticks)
        {
          //*p->saved_tf = p->trapframe
          // Doesn't work, because all it will do is make it point to the trapframe again
          memmove(&(p->saved_tf), p->trapframe, sizeof(struct trapframe));
          // This is the way to saved data that is getting pointed to by a pointer into a struct
          p->trapframe->epc = p->handler; // updating program counter with the function address, big brain
        }
      }

    yield();
  }
  #endif

  #ifdef MLFQ
    if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING){

    struct proc* p = myproc();
    // If it has exhausted it's quanta
    // for(struct proc *pp=proc;pp<&proc[NPROC];pp++)
    // {
    //   if(pp->state==RUNNABLE || pp->state==RUNNING)
    //   {
    //     printf("%d %d\n",pp->pid,pp->priority);
    //   }
    // }
    // printf("\n");
    if (p->quanta <= 0){
    // Put it in a lower Priority
      if(p->priority + 1 != 5){
          
          remove(&Mlfq[p->priority], p->pid);
          p->priority++;
          push(&Mlfq[p->priority],p);
      }
      // Yeild to the CPU now for other processes
      yield();
    }

    // Yield for any other queues with more priority
    // than current one
    for (int i = 0; i < p->priority;i++){
      if(Mlfq[i].size)
        yield();
    }
  }
#endif

  usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }
  #if defined RR 
    // give up the CPU if this is a timer interrupt.
    if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
      yield();
  #endif

  #ifdef MLFQ
    if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING){

    
    struct proc* p = myproc();
    // If it has exhausted it's quanta
    if (p->quanta <= 0){
    // Put it in a lower Priority
      if(p->priority + 1 != 5){
          p->priority++;
      }
      // Yeild to the CPU now for other processes
      yield();
    }

    // Yield for any other queues with more priority
    // than current one
    for (int i = 0; i < p->priority;i++){
      if(Mlfq[i].size)
        yield();
    }
  }
#endif

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr()
{
  acquire(&tickslock);
  ticks++;
  update_time();
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) &&
      (scause & 0xff) == 9)
  {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000001L)
  {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0)
    {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  }
  else
  {
    return 0;
  }
}

int pagefaulthandler(void *va, pagetable_t pagetable)
{
  struct proc *p = myproc();
  if ((uint64)va >= MAXVA || ((uint64)va >= PGROUNDDOWN(p->trapframe->sp) - PGSIZE && PGSIZE && (uint64)va <= PGROUNDDOWN(p->trapframe->sp)))
  {
    return -2;
  }

  pte_t *pte;
  uint64 pa;
  uint flags;
  va = (void*)PGROUNDDOWN((uint64)va);
  pte = walk(pagetable,(uint64)va,0);
  if(pte == 0)
  {
    return -1;
  }
  pa = PTE2PA(*pte);
  if(pa == 0)
  {
    return -1;
  }
  flags = PTE_FLAGS(*pte);
  if(flags & PTE_C)
  {
    flags = (flags | PTE_W) & (~PTE_C);
    char *mem;
    mem = kalloc();
    if(mem == 0)
    {
      return -1;
    }
    memmove(mem,(void*)pa,PGSIZE);
    *pte = PA2PTE(mem) |flags;
    kfree((void*)pa);
    return 0;
  }

  return 0;
}