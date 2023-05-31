#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define PHI 0x9e3779b9
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

struct Queue Mlfq[5];

// Returns the process in front of the queue
struct proc *front(struct Queue *q)
{
  // If a front exists then return
  if (q->front != q->back)
  {
    return q->procs[q->front];
  }
  else
  {
    return 0;
  }
}

int push(struct Queue *q, struct proc *p)
{
  // If there is space left in the queue
  if (q->size < NPROC)
  {
    q->procs[q->back] = p;

    // Checking if this is the last process
    if (q->back == NPROC)
      q->back = 0;
    else
      q->back++;

    q->size++;
    return 1;
  }
  return 0;
}

int pop(struct Queue *q)
{
  // If there exists atleast one element in that queue
  if (q->size > 0)
  {
    //        q->procs[q->front] = 0;

    // Checking if this is the first process
    if (q->front == NPROC)
      q->front = 0;
    else
      q->front++;

    q->size--;
    return 1;
  }
  return 0;
}

// Remove a process with a specific PID
int remove(struct Queue *q, int pid)
{
  struct proc *t;
  int new_idx;
  // Iterating through the queue to find the process
  for (int i = q->front; i != q->back; i = (i + 1) % 64)
  {
    if (q->procs[i]->pid == pid)
    {
      // Saving the current process with
      // a temp variable
      t = q->procs[i];

      // Swapping with the next index
      new_idx = (i + 1) % 65;
      q->procs[i] = q->procs[new_idx];
      q->procs[new_idx] = t;
    }
  }
  q->size--;
  if (q->back < 1)
    q->back = 64;
  else
    q->back--;
  return 0;
}

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

static uint rand_table[1000];
void rand_ret(int n)
{

  rand_table[0] = n * (PHI + 1);
  rand_table[1] = n * (PHI + 2);
  rand_table[2] = n * (PHI + 3);

  for (int i = 3; i < 1000; ++i)
  {
    rand_table[i] = rand_table[i - 1] ^ rand_table[i - 2] ^ i ^ PHI;
  }
}

void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void) // THIS IS ALSO THE PROC INITIALISE function
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->ticks = ticks;
  p->elapsed_ticks = 0;
  p->handler = 0;

  p->rtime = 0;
  p->etime = 0;
  p->stime = 0;
  p->trtime = 0;
  p->runs = 0;
#ifdef PBS
  p->priority = 60;
#endif

#ifdef MLFQ
  p->priority = 0;
#endif
  p->quanta = 1;
  p->queued = 0;
  p->qtime = ticks;
  // Setting the array values to 0
  memset(p->qtimes, 0, 5 * sizeof(p->qtimes[0]));
  p->ctime = ticks;

  p->tickets = 1;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->tickets = 1;
  p->state = UNUSED;

  p->mask = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // copying the tickets
  np->tickets = p->tickets;

  np->mask = p->mask; // Storing mask value into struct

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int waitx(uint64 addr, uint *wtime, uint *rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

void update_time()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNING)
    {
      p->rtime++;
      p->trtime++;
    }
    if (p->state == SLEEPING)
    {
      p->stime++;
    }
    release(&p->lock);
  }
  p->qtimes[p->priority]++;
  p->quanta--;
}

void RRschedule(struct cpu *c)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    {
      p->state = RUNNING;
      c->proc = p;
      swtch(&c->context, &p->context);
      c->proc = 0;
    }
    release(&p->lock);
  }
}

void proc_swtch(struct cpu *c, struct proc *change)
{
  if (!change)
  {
    return;
  }

  if (change->state == RUNNABLE)
  {
    // Switch to the new process, lock and reacquire before jumping back
    change->state = RUNNING;
    c->proc = change;
    swtch(&c->context, &change->context);

    // The process is done running for now
    c->proc = 0;
  }
  release(&change->lock);
}
void FCFSschedule(struct cpu *c)
{
  struct proc *p;
  struct proc *next = 0;
  uint lowestFound = 2100000000;

  for (p = proc; p < &proc[NPROC]; p++)
  {

    // Wait for the process to get into the running state
    acquire(&p->lock);
    if (p->state != RUNNABLE)
    {
      release(&p->lock);
      continue;
    }

    // Compare creation time, if the next process was created later assign it to p;
    if (p->ctime < lowestFound)
    {
      next = p;
      lowestFound = p->ctime;
      continue;
    }
    release(&p->lock);
  }

  proc_swtch(c, next);
  //    release(&p->lock);
}

int total_tickets(void)
{
  struct proc *p;
  int ticket_sum = 0;

  // Loop over processes add total tickets if a runnable process is found
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == RUNNABLE)
    {
      ticket_sum += p->tickets;
    }
  }
  return ticket_sum;
}
void LOTTERYschedule(struct cpu *c)
{
  struct proc *p;
  int win_index = 0;

  // Generating a random number
  rand_ret(237592526);

  // Calculating total tickets for running processses
  int total_tix = total_tickets();

  // Picking a ticket at random which won the lottery
  int winner_ticket = rand_table[667] % total_tix;
  // int winner_ticket = 1;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state != RUNNABLE)
    {
      release(&p->lock);
      continue;
    }

    // Select the process which won the lottery(holds the winning ticket)
    if (winner_ticket > (win_index + p->tickets))
    {
      win_index += p->tickets;
      release(&p->lock);
      continue;
    }
    //        proc_swtch(c,p);
    release(&p->lock);
    break;
  }
  acquire(&p->lock);
  proc_swtch(c, p);
  //    release(&p->lock);
}

int set_priority(int np, int pid)
{
  struct proc *p;
  int temp = -1;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->state == RUNNABLE)
    {
      // Finding the process
      if (pid == p->pid)
      {

        // Saving the current priority to return off the process end
        temp = p->priority;

        // Giving the process new priority
        p->priority = np;

        // Release the lock after;
        release(&p->lock);

        // Checking if the new priority of the process is less
        if (temp > np)
          yield();
        break;
      }
    }
    release(&p->lock);
  }
  return temp;
}
void priority_switch(struct cpu *c, struct proc *change)
{
  if (!change)
  {
    return;
  }

  // Switch to the new process, lock and reacquire before jumping back
  acquire(&change->lock);
  if (change->state == RUNNABLE)
  {
    change->state = RUNNING;
    change->stime = ticks;

    // Increment Number of run s
    change->runs++;

    // Set tje Sleep and Run Time for the highest priority process as 0
    change->rtime = 0;
    change->stime = 0;

    // Process is done runnning now
    c->proc = change;
    swtch(&c->context, &change->context);

    // Release Lock
    c->proc = 0;
  }
  release(&change->lock);
}
void PBSschedule(struct cpu *c)
{

  struct proc *p;
  struct proc *priority_proc = 0;
  int dp = 101;     //  Highest Priority
  int proc_dp;      //  Dynamic Priority for the process
  int niceness = 5; //  Default Nice value
  int flag = 0;     //  Initialising flag

  // int currentPriorityRuns = 0;
  // int currentPriorityTicks = 0;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    {

      // Check if the denum is positive
      // If yes then update the value of Niceness for this process
      if (p->stime + p->rtime > 0)
      {
        niceness = 10 * p->stime;
        niceness /= p->stime + p->rtime;
      }
      int temp = p->priority - niceness + 5;
      proc_dp = MAX(0, MIN(temp, 100));

      // If process isn't assinged yet
      if (priority_proc == 0)
        flag = 1;

      // If the dp is greater than the process's dp
      if (dp > proc_dp)
        flag = 1;

      if (flag == 1)
      {

        //   if (priority_proc)
        // {

        dp = proc_dp;
        priority_proc = p;
        // }
      }
    }
    release(&p->lock);
  }
  priority_switch(c, priority_proc);
}

void aging(){
    struct proc *p;

    // Initialising an age after which a process's
    // priority will increase
    int aged = 120;
    for (p = proc; p < &proc[NPROC]; p++) {
        if(p->state == RUNNABLE && p->priority > 0){
            int time_elapsed = ticks - p->qtime;

            // if a process with priority greater than 0
            // has elapsed more time than age then
            // increase it's priority
            if(time_elapsed > aged){
                p->qtime = ticks;

                remove(&Mlfq[p->priority], p->pid);
                p->priority--;
                p->queued = 0;
                // push(&Mlfq[p->priority],p->pid);

                }
            }
        }
    }
void queueProcs() {
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        // Check if the process is runnable
        // and not queued yet
        if (p->state == RUNNABLE && !p->queued) {
            // Push into the queue and changed to queued
            push(&Mlfq[p->priority], p);
            p->queued = 1;
        }
        release(&p->lock);
    }
}
void MLFQswitch(struct cpu *c,struct proc *change) {
    if(!change)
        return;

    // Setting the time quanta accoringly to the
    // priority queue it is present in
    change->quanta = 1;
    for (int i = 0; i < change->priority; ++i) {
        change->quanta *= 2;
    }

    // General Switch Routine
    change->state = RUNNING;
    c->proc = change;
    swtch(&c->context,&change->context);
    change->runs++;

    // Process is finished
    c->proc = 0;
    change->qtime = ticks;
    release(&change->lock);
}

void MLFQschedule(struct cpu *c){
    // Implement Aging and check if any
    // processes have aged or not
    aging();

    // Push processes into queues
    queueProcs();

    // Schedule Processes on the basis of
    // Queues

    struct proc *p = 0;
    struct proc *new_proc = 0;

    for (int i = 0; i < 5; i++){

        // If the subsequent queue is not empty
        // then iterate over the processes in it
        for(int j = Mlfq->size-1; j >= 0; j--){

            // Retrieve the front element and it's lock
            p = front(&Mlfq[i]);
            acquire(&p->lock);

            // Remove it from the MLFQ
            pop(&Mlfq[i]);
            p->queued = 0;

            // If the process is Runnable then switch
            if (p->state == RUNNABLE){
                p->qtime = ticks;
                new_proc = p;
                break;
            }

            release(&p->lock);
        }
        // If a runnable process is found then break out
        if (new_proc)
            break;
    }
    MLFQswitch(c,new_proc);

}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

#ifdef RR
    RRschedule(c);
#else

#ifdef FCFS
    FCFSschedule(c);
#else

#ifdef PBS
    PBSschedule(c);
#else

#ifdef LBS
    LOTTERYschedule(c);
#else

#ifdef MLFQ
    MLFQschedule(c);
#else

#endif
#endif
#endif
#endif
#endif

    //    processSwitch();
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";


    printf("%d %s %s %d", p->pid, state, p->name,p->priority);
    printf("\n");
    printf("Running time : %d\nWaiting time : %d\n", p->elapsed_ticks, p->ticks - p->elapsed_ticks);
    printf("Is the process queued : %d\n",p->queued);
    for(int i = 0; i < 5;i++)
    {
      printf("Time spent in Queue %d : %d\n",i,p->qtimes[i]);
    }
  }
 
}
