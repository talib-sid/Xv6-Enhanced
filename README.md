# xv6 Enhanced
## Team : 
                Siddharth Mangipudi (2021101060)
                Talib Siddiqui (2021101078)

# Specification 1 : 
## strace (5 marks)
strace command is of the form :

>                    strace mask command <arguments>

 - mask here is an integer, whose bits specify which system calls to trace
 - in order to trace `ith` system call, a program calls strace `1 << i`, where i is  the system call number

Output of strace is in the following form :

>                    "pid of process" : syscall "name_of_syscall" ([decimal value of arguments in registers])-> "return value"



1. Added `$U/_strace` to the `UPROGS` part of the makefile.

2. Added 
    ```cpp
    entry("trace");
    ```
    in `user/usys.pl`.
Doing this will make the makefile invoke the script `user/usys.pl` and produce `user/usys.S`.

3. Add a syscall number to `kernel/syscall.h` .
    ```cpp
    #define SYS_trace 22
    ```

4. Created a user program in `user/strace.c` to generate userspace stubs for the system call.
    ```cpp
    int main(int argc, char *argv[]) {
    char *args[32];

    if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){             // Case where mask has charecters other than numerical digits
        fprintf(2, "Usage: %s mask command\n", argv[0]);            // writing into the stderr buffer
        exit(1);
    }

    if (trace(atoi(argv[1])) < 0) {
        fprintf(2, "%s: negative mask value given, trace failed\n", argv[0]);
        exit(1);
    }

    for(int i = 2; i < argc && i < 32; i++){
    	args[i-2] = argv[i];               // The command is isolated
    }

    exec(args[0], args);

    exit(0);
    }
    ```

5. Implemented a syscall `sys_trace(void)` in `kernel/sysproc.c` . This implements new syscall to apply `mask` to a process.
```cpp
uint64 sys_trace(void)
{
  int mask;

  int f = argint(0,&mask);

  if(f < 0)
  {
    return -1;
  }

  struct proc *p = myproc();
  p->mask = mask;

  return 0; 
}
```
6. Modify the struct `proc` in `kernel/proc.h` to include the mask value for every process.

7. Modify the `syscall()` function in `kernel/syscall.c` to implement the actual strace printing part. We will also create a struct `syscall_num` which maps the syscall number to the number of registers it uses, this needs to be hardcoded.    
    - `p->trapframe->a0` - contains return value of syscall.
    - `p->trapframe->a7` - contains syscall number of syscall
    - registers `a0-a7` contain the arguments in decimal value.

```cpp
void
syscall(void)
{
  int num;
  struct proc *p = myproc();


  num = p->trapframe->a7;   // contains the integer than corresponds to sycall in syscall.h, check user/initcode.S

  int len_args = syscalls_num[num];     

  int arguments_decval[num];
  for(int i = 0; i < len_args;i++)
  {
    arguments_decval[i] = argraw(i); //argraw extracts value of registers in integer form
  }
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    if(p->mask & 1 << num)
    {
      printf("%d: syscall %s (",p->pid,syscall_names[num]);
      for(int i = 0; i < len_args;i++)
      {
        printf("%d ",arguments_decval[i]);              // interesting note : trace and shell output are both intermixed, since both use the write command
      }
      printf("\b) -> %d\n",p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

8. In `kernel/proc.c` , add the following lines
```cpp
fork:
+    np->trapframe->a0 = 0;

freeproc:
p->state = UNUSED;

+p->mask = 0;


```

9. Output is displayed on shell - Note that trace and shell output are both intermixed, since both use the write command, for instance, `strace 32 echo hi` would execute echo hi as well, since we are exec()-ing it.    


## Sigalarm and Sigreturn (10 marks)
In this specification, we added a feature to xv6 that periodically alerted a process as it uses CPU time. Each clock cycle of the Hardware clovk is taken as a `tick`.
We implemented a new `sigalarm(interval,handler)` system call, and also `sigreturn()` system call.
If an application calls `alarm(n,fn)`, then after every n ticks of CPU time that the program consumes, the kernel will cause application function `fn` to be called. When `fn` returns, the application picks up where it left off.
- `alarm(n,fn)` is a user defined function, in `alarmtest.c`

Sigreturn restores the trapframe of the process before `handler` was called.

1. Add `$U_alarmtest\` to `UPROGS` in Makefile.
2. Add two syscalls `sys_sigalarm(void)` and `sys_sigreturn(void)` to `kernel/sysproc.c` .
```cpp
uint64 sys_sigalarm(void)
{
  int ticks;
  uint64 handler;

 argint(0,&ticks);
 argaddr(1,&handler);
  
  struct proc* p = myproc();

  p->ticks = ticks;
  p->handler = handler;
  p->elapsed_ticks = 0;

  return 0;
}

uint64 sys_sigreturn(void)
{
  struct proc* p = myproc();

  // Recover saved trapframe.
  
  memmove(p->trapframe,&(p->saved_tf),sizeof(struct trapframe)); // currently passing tests 0,1,2

  p->elapsed_ticks = 0;

  return p->saved_tf.a0;    // This is the return value of sigreturn, the state of a0 reg in the saved trapframe
}
```

3. Add a corresponding syscall number to these two functions in `kernel/syscall.h`
```cpp   
#define SYS_sigalarm  23
#define SYS_sigreturn 24
```
4. Modify the struct `proc` in `kernel/proc.h` To add the following fields:
```cpp
  int ticks;                    // number of cpu ticks
  int elapsed_ticks;            // number of ticks since last call
  uint64 handler;  
  struct trapframe saved_tf;
```
- `ticks` - Interval
- `elapsed_ticks` - Number of  ticks since last interrupt.
- `handler` - self-explanotory.
- `saved_tf` - trapframe before alarm was called.

5. Initialise these variables in `kernel/proc.c`.
```cpp
allocproc():
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

+ p->ticks = 0;
+ p->elapsed_ticks = 0;
+ p->handler = 0;

return p;

```

6. In `kernel/trap.c` , in the function `usertrap` , we need to handle the case where we get a timer interrupt. We need to modify the following section:
```cpp
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
        p->trapframe->epc = p->handler; // updating program counter with the function address.
      }
    }
     yield();
  }     
```

7. Run `make qemu` on shell and type `alarmtest` on hell, and check output.

# Specification 2 :
## Scheduling (65 marks)

Each Scheduling Algorithm is given a Definitive TAG as follows:
```cpp
    FCFS - First Come First Serve
    RR - Round Robin
    PBS - Priority Based Scheduling
    LBS - Lottery Based Scheduling
```

The Scheduling Algorithm that is to be used must be mentioned in the tags as follows
```bash
    $ make qemu SCHEDULER=<TAG>
```

Note that if multiple Scheduling algorithms are tested in succession, the object files in the folders correspond to the previous Scheduling algorithm TAG, hence we wish to get rid of these tags. We do so by running
```bash
    $ make clean
```


## 1) FCFS(5 marks)

A scheduling policy that selects the process with the lowest creation time

1. Added Creation time to struct proc
```cpp
  uint ctime;                   // When was the process created
```
2. Edited allocproc()
```cpp
  p->ctime = ticks;
```
3. Edited scheduler()
```cpp
#ifdef RR
    RRschedule(c);
#else

void proc_swtch(struct cpu *c,struct proc *change){
    if(!change) {
        return;
    }

    // Switch to the new process
    // lock and reacquire before jumping back
    change->state = RUNNING;
    c->proc = change;
    swtch(&c->context,&change->context);

    // The process is done running for now
    c->proc = 0;
    release(&change->lock);
}
void FCFSschedule(struct cpu *c){
    struct proc *p;
    struct proc *next = 0;
    uint lowestFound = 2100000000;

    for(p = proc; p < &proc[NPROC]; p++) {

        // Wait for the process to get into the running state
        acquire(&p->lock);
        if(p->state != RUNNABLE) {
            release(&p->lock);
            continue;
        }

        // Compare creation time
        // if the next process was created later assign it to p;
        if(p->ctime < lowestFound) {
            next = p;
            lowestFound = p->ctime;
            continue;
        }
        release(&p->lock);
    }

    proc_swtch(c,next);
}
```

4.Edit kerneltrap()
```cpp
  // Enabling preemption for the default scheduling
  // Disabling preemption for FCFS
  #if defined RR
    // give up the CPU if this is a timer interrupt.
    if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
      yield();
  #endif

```

## 2) Lottery Based Scheduler(10 marks)

Implement a preemptive scheduler that assigns a time slice to the process randomly in
proportion to the number of tickets it owns.

1. Declaring a random function to get the lottery winnner

```cpp
#define PHI 0x9e3779b9

static uint rand_table[1000];
void
rand_ret(int n){

    rand_table[0] = n * (PHI + 1);
    rand_table[1] = n * (PHI + 2);
    rand_table[2] = n * (PHI + 3);

    for (int i = 3; i < 1000; ++i) {
        rand_table[i] = rand_table[i-1] ^ rand_table[i-2] ^ i ^ PHI;
    }
}
```

2. Edit allocproc()
```cpp
  p->tickets = 1;
```

3. Implementing set_tickets(int)
```cpp

// user.h
int set_tickets(int);


// sysproc.h
uint64
sys_set_tickets(void){
    int num;
    int f1 = argint(0,&num);

    if(f1 < 0)
    {
      return -1;
    }
    myproc()->tickets+=num;
    return 0;
}


// syscall.h
#define SYS_set_tickets  23

// syscall.c
extern uint64 sys_set_tickets(void);


```
4. Edited scheduler()
```cpp
  void LOTTERYschedule(struct cpu *c){
    struct proc *p;
    int win_index = 0;

    // Generating a random number
   rand_ret(237592526);

    // Calculating total tickets for running processses
   int total_tix = total_tickets();

    // Picking a ticket at random which won the lottery
   int winner_ticket = rand_table[667]%total_tix;
    int winner_ticket = 10;

    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state != RUNNABLE){
            release(&p->lock);
            continue;
        }

        // Select the process which won the lottery(holds the winning ticket)
        if(winner_ticket > (win_index + p->tickets)){
            win_index += p->tickets;
            release(&p->lock);
            continue;
        }
        release(&p->lock);
        break;
    }
    proc_swtch(c,p);

}

```

## 3) Priority Based Scheduler (15 Marks)

1. Added required variables to struct proc()
```cpp 
  uint ctime;                   // When was the process created
  uint srtime;                  // When did it start
  uint rtime;                   // How long the process ran for
  uint etime;                   // When did the process exited

  uint wtime;                   // Total time waited for
  uint trtime;                  // Total time ran for

  uint stime;                   // Time spent sleeping
  uint runs;                    // Number of times ran
  uint priority;                // Process's Priority
```

2. Initialise the variables in allocproc()
```cpp
  p->rtime = 0;
  p->etime = 0;
  p->stime = 0;
  p->trtime= 0;
  p->runs = 0;
  p->priority = 60;
  p->tickets = 1;
  p->ctime = ticks;
```
3. Edited scheduler() with a helper function with helps in switching
```cpp
void PBSschedule(struct cpu *c){
    struct proc *p;
    struct proc *priority_proc = 0;
    int dp = 101;         //  Highest Priority
    int proc_dp;         //  Dynamic Priority for the process
    int niceness = 5;   //  Default Nice value
    int flag = 0;      //  Initialising flag
    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);

        // Check if the denum is positive
        // If yes then update the value of Niceness for this process
        if(p->stime + p->rtime > 0){
            niceness = 10*p->stime;
            niceness /= p->stime+p->rtime;
        }
        int temp = p->priority - niceness + 5;
        proc_dp = MAX(0, MIN(temp, 100));

        if(p->state == RUNNABLE){
            // If process isn't assinged yet
            if(priority_proc == 0)
                flag = 1;

            // If the dp is greater than the process's dp
            if(dp > proc_dp)
                flag = 1;

            // If the dp is same then we 
            // compare how many times we have ran the process
            // Checking for the nullity of priority_proc
            if(dp == proc_dp && priority_proc) {
                if (priority_proc->runs > p->runs) {
                    flag = 1;
                }
                // If runs are equal too then compare creation time
                if(priority_proc->runs == p->runs && priority_proc->ctime > p->ctime){
                    flag = 1;
                }
            }

            if(flag == 1){
                if(priority_proc) {
                    release(&priority_proc->lock);
                }
                dp = proc_dp;
                priority_proc = p;
                continue;
            }
            release(&p->lock);
        }
        priority_switch(c,priority_proc);
    }
}

// To change the processes
void priority_switch(struct cpu *c,struct proc *change){
    if(!change){
        return;
    }

    // Switch to the new process
    // lock and reacquire before jumping back
    change->state = RUNNING;
    change->stime = ticks;

    // Increment Number of run s
    change->runs++;

    // Set tje Sleep and Run Time for the highest priority process as 0
    change->rtime = 0;
    change->stime = 0;

    // Process is done runnning now
    c->proc = change;
    swtch(&c->context,&change->context);

    // Release Lock
    c->proc = 0;
    release(&change->lock);
}

```
4. Edited clockintr() in trap.c

```cpp
// Updating time
  update_time();
```

5. Added a new system call set_priority(int,int)

```cpp
// user.h
int set_priority(int (pid),int (tickets));

// sysproc.h
uint64
sys_set_priority(){
    int np, pid ;
    int temp = 101;

    int f1 = argint(0,&np);
    int f2 = argint(1,&pid);

    // If the input format is wrong then exit
    if(f1 < 0 || f2 < 0){
        return -1;
    }
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
        // Locking the process
        acquire(&p->lock);

        // Finding the process and checking if new_priority is in the
        // correct range of values
        if(p->pid == pid && np >= 0 && np <= 100){

            // Setting the runtime and sleeptime to 0
            p->stime = 0;
            p->rtime = 0;

            // Saving the current priority to return later
            temp = p->priority;

            // Switching the priority
            p->priority = np;
        }
        // Unlock the process
        release(&p->lock);
    }
    // If the new priority is lesser than old then yield to cpu
    if(temp > np)
        yield();


    // Return the old priority
    return temp;
}

// syscall.h
#define SYS_priority 24

// syscall.c
extern uint64 sys_set_priority(void);

```



## 4) Multi-Level Feedback Queue Scheduling

1. Implementing Queue and Queue Routines
```cpp 
// Making the struct queue for MLFQ
struct Queue{
  // Index to denote the front and back in procs
  int front, back;

  // Allowing upto 64 processes in the queue
  struct proc *procs[65];
  int size;
} // 5 Queues for our MLFQ scheduler
Mlfq[5];


// Returns the process in front of the queue
struct proc *front(struct Queue *q){
    // If a front exists then return
    if(q->front != q->back){
        return q->procs[q->front];
    }else{
        return 0;
    }
}


int push(struct Queue *q, struct proc *p){
    // If there is space left in the queue
    if(q->size < NPROC){
        q->procs[q->back] = p;

        // Checking if this is the last process
        if(q->back == NPROC) q->back = 0;
        else q->back++;

        q->size++;
        return 1;
    }
    return 0;
}


int pop(struct Queue *q){
    // If there exists atleast one element in that queue
    if(q->size > 0){
//        q->procs[q->front] = 0;

        // Checking if this is the first process
        if(q->front == NPROC) q->front = 0;
        else q->front++;

        q->size--;
        return 1;
    }
    return 0;
}

// Remove a process with a specific PID
int remove(struct Queue *q, int pid){
    struct proc *t;
    int new_idx;
    // Iterating through the queue to find the process
    for (int i = q->front; i != q->back; i = (i+1)%64) {
        if(q->procs[i]->pid == pid){
            // Saving the current process with
            // a temp variable
            t = q->procs[i];
            
            // Swapping with the next index 
            new_idx = (i+1)%65;
            q->procs[i] = q->procs[new_idx];
            q->procs[new_idx] = temp;
        }
    }
    q->size--;
    if(q->back < 1) q->back = 64;
    else q->back--;
}
```

2. Edited the struct proc in proc.h
```cpp
// MLFQ Additions

  uint qtime;                   // Time spent in the current queue
  uint qtimes[5];               // Time spent in all the queues
  uint queued;                  // If the process is in a queue or not
  uint quanta;                  // Describe the Processes quanta
```

3. Intialising above variables in allocproc()
```cpp

#ifdef MLFQ
    p->priority = 0;
#endif

  p->quanta = 1;
  p->queued = 0;
  p->qtime = ticks;

  // Setting the array values to 0
  memset(p->qtimes,0,5*sizeof(p->qtimes[0]));
```

4. Making changes to procinit() and update_time()
```cpp
// Process Initialisation
  for (int i = 0; i < NMLFQ; i++)
  {
    mlfq[i].size = 0;
    mlfq[i].head = 0;
    mlfq[i].tail = 0;
  }


// Update Time and quanta
  p->qtimes[p->priority]++;
  p->quanta--;
```

5. Made changes to kerneltrap and usertrap to yield() the process when it has exhausted the time slice for it's queue

```cpp
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
      if(mlfq[i].size)
        yield();
    }
  }
#endif
```

6. Implement Aging

```cpp
void aging(){
    struct proc *p;

    // Initialising an age after which a process's
    // priority will increase
    int aged = 30;
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

                }
            }
        }
    }
```

7. Implementing Queueing Processes 

```cpp
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
```

8. Implementing the scheduler in proc.c

```cpp
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
    // Switch processes
    MLFQswitch(c,new_proc);
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
```


## Question.  
### If a process voluntarily relinquishes control of the CPU(eg. For doing I/O), it leaves the queuing network, and when the process becomes ready again after the I/O, it is inserted at the tail of the same queue, from which it is relinquished earlier (Q: Explain in the README how could this be exploited by a process(see specification 4 below)).

## Answer. 
 A malicious process can possibly exploit condition by yielding the CPU before finishing its allocated time, retaining its priority and blocking lower priority processes from running,  it can cause starvation to other processes, if aging is not implemented.



# Specification 3 :
### Copy-on-Write Fork (15 marks)

Copy-on-Write Fork is a Virtual memory management modification which can be applied in kernels.

The goal of copy-on-write (COW) fork() is to defer allocating and copying physical memory pages for the child until the copies are actually needed, if ever. 
COW fork() creates just a pagetable for the child, with PTEs for user memory pointing to the parent's physical pages. 

COW fork() marks all the user PTEs in both parent and child as not writable(remove write prmissions). 

When either process tries to write one of these COW pages, the CPU will force a page fault. The kernel `pagefaulthandler` in `kernel/trap.c` detects this case, allocates a page of physical memory for the faulting process, copies the original page into the new page, and modifies the relevant PTE in the faulting process to refer to the new page, this time with the PTE marked writeable. 

When the page fault handler returns, the user process will be able to write its copy of the page.

Note that a given physical page may be referred to by multiple processes' page tables, and should be freed only when the last reference disappears, so in order to handle this, we can create a struct `ref_count` which has a `spinlock` and an array of number of pages in each page to record this.(acts as a Semaphore)

1. Modify `kernel/vm.c` in the following places:
```cpp
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    .....
    flags = PTE_FLAGS(*pte);
-    if((mem = kalloc()) == 0)
-      goto err;
-    memmove(mem, (char*)pa, PGSIZE);
-    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
-      kfree(mem);
-      goto err;
-    }
+    if(flags & PTE_W)
+    {
+      flags = (flags&(~PTE_W)) | PTE_C;
+      *pte = PA2PTE(pa) | flags ;
+    }

+    if(mappages(new,i,PGSIZE,pa,flags) != 0)
    {
      goto err;
    }

+    safe_increment_references((void*)pa);
    
  ......
```

2. We need to add the following functions to `kernel/kalloc.c` to bookkeep the number of references each page has :
```cpp

struct {
  struct spinlock lock;
  int no_of_references[PGROUNDUP(PHYSTOP) >> 12];        
  // PHYSTOP is the upper bound for RAM usage for both user and kernel space
  // KERNBASE is the address from where the kernel starts
  // Total size = 128*1024*1024 = 2^27 bytes --> 2^27 PTEs
  // Each physical page table has 512 = 2^9 pages in it --> 2^9 * 8 bytes = 2^12 bytes
  // PGROUNDUP rounds it up to nearest 406-multiple
  
}ref_count;

void refcountinit()
{
  initlock(&ref_count.lock,"ref_count");
  acquire(&kmem.lock);            // reason we need to memset is to ensure no other concurrent child process can modify this at the same time, very important
  // memset(ref_count.no_of_references,0,sizeof(int));
  for(int i = 0; i < (PGROUNDUP(PHYSTOP) >> 12);i++)
  {
    ref_count.no_of_references[i] = 0;
  }      
  release(&kmem.lock);
}

void safe_increment_references(void* pa)
{
  acquire(&ref_count.lock);
  if(ref_count.no_of_references[(uint64)pa >> 12] < 0)
  {
    panic("safe_increment_references");
  }
  ref_count.no_of_references[((uint64)(pa) >> 12)] += 1;       // basically increments the number of references for that page
  release(&ref_count.lock);
}

void safe_decrement_references(void* pa)
{
  acquire(&ref_count.lock);
  if(ref_count.no_of_references[(uint64)pa >> 12] <= 0)
  {
    panic("safe_decrement_references");
  }
  ref_count.no_of_references[((uint64)(pa) >> 12)]  -= 1;
  release(&ref_count.lock);
}

int get_refcount(void *pa)
{
  acquire(&ref_count.lock);
  int result = ref_count.no_of_references[((uint64)(pa) >> 12)];
  if(ref_count.no_of_references[(uint64)pa >> 12] < 0)
  {
    panic("get_page_ref");
  }
  release(&ref_count.lock);
  return result;
}

void reset_refcount(void* pa)
{
  ref_count.no_of_references[((uint64)(pa) >> 12)] = 0;
}

```

We also need to modify `kinit` function here to initialise this struct.
```cpp
kinit:
+     refcountint();
```
Also, for the `kalloc` function, we increment the reference count for each run struct
```cpp
kalloc:
......
if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
+    safe_increment_references((void*)r);
  }
.....
```

3. We need to modify `kernel/trap.c` in order to handle page-fault exceptions, we define a seperate interrupt routine `pagefaulthandler` .
```cpp
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

usertrap:
....
else if ((r_scause() == 13 || r_scause() == 15))      // eroor code in event of page fault exception
  {

    if(r_stval() == 0)      // if the virtual adress where it is faulting is 0
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
....  
```
4. We need to only free a page if the number of references to it become zero, in order to do so, we need to modify the `kree` function in `kernel/kalloc.c`
```cpp
kfree():
....
if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
  {
    panic("kfree");
  }

  acquire(&ref_count.lock);
  if(ref_count.no_of_references[(uint64)pa >> 12] <= 0)
  {
    panic("safe_decrement_references");
  }
  ref_count.no_of_references[(uint64)pa >> 12] -= 1;   // decrementing reference
  
  if(ref_count.no_of_references[(uint64)pa >> 12] > 0)
  {
    release(&ref_count.lock);
    return; // can't free the page address yet
  }
  release(&ref_count.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
....  
```
We need to also increment the number of references in `freerange()` since it will call `kfree()` and decrease the count, so in order to cancel this out we first increase it and then decrease it.
```cpp
for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    safe_increment_references(p);    // increment page references
    kfree(p);
  }
```
5. Modify `copyout()` in `kernel/vm.c` to use the same scheme as page faults when it encounters a COW page.
```cpp
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0,flags;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
    {
      return -1;  
    }
    pte = walk(pagetable,va0,0);
    flags = PTE_FLAGS(*pte);
    if(flags & PTE_C)
    {
      pagefaulthandler((void*)va0,pagetable);
      pa0 = walkaddr(pagetable,va0);
    }  
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```
6. In order to test if the above implementation is working, add the program `cowtest.c` too the users folder, and consequently add it to the makefile.

Run `make qemu` and run `cowtest` to see the COW functionality works, similarly, run `usertests` in the shell.

# Specification 4 - Report

Using the `scheduler.c` I created 100 processes which forked, of which , N were I/O Bound processes.
Every I/O bound Process sleeps for 200 ms, whereas every CPU bound process executes the following.
```cpp
  for (int i = 0; i < 1000000000; i++) {}; // CPU bound process
```
I also edited the `scheduler.c` file to set tickets for `LBS`.
The number of tickets that are added to every process is `i%3+1` tickets where `i` is the inherent Process number.

I then calculated the Average Running times,Waiting times of the processes, and tabulated them into graphs.

## Comparing Average Running times of processes in different Scheduling Algorithms

![Image not found](running.png)

As Expected, We see that in all scheduling algorithms, the average running time of processes are decreasing, since the processes are waiting in the I/O queue for most of the time.

We also see that the Smarter, Priority based Scheduling performs better than it's Counterparts. This is because it priotitises processes over others, hence giving other processes time to also run.
>Note that these values are rounded down to the nearest integer 

## Comparing Average Waiting times of processes in different Scheduling Algorithms

![Image not found](waiting.png)

As Expected, We see that in all scheduling algorithms, the average waiting time of processes are increasing, since the processes are waiting in the I/O queue for most of the time.

We also see that in this case, FCFS and PBS performed similarly, and had lower waiting times than RR Scheduling.
We can infer from this data that FCFS is better with shorter CPU burst periods, that is to say, more I/O bound processes. This corresponds with the theory that FCFS scheduling tends to penalise short processes.

In both cases we see that LBS scheduling performed poorly, and RR scheduling performed average on deafult xv6 settings for the time quanta.

For 100 process, if number of I/O bound processes are 20, then the following are the avg_runtime and avg_waittime for each process

RR : 
  - Average Running time : 2
  - Average Waiting time : 74

LBS :
  - Average Running time : 1
  - Average Waiting time : 70

FCFS : 
  - Average Running time : 3
  - Average Waiting time : 59 

PBS :
  - Average Running time : 3
  - Average Waiting time : 59 

  
