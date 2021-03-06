#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

//SN-variables
int lastproc = -1, sncount = 0, WTIME = 500;
int LEN[5] = {0, 0, 0, 0, 0}; //length of each queue

int slice[5] = {1, 2, 4, 8, 16}; //quantum for each queue

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

// initialize the proc table at boot time.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
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
//SNXX ADDED
static struct proc *
allocproc(void)
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

  p->c_time = ticks; // When was the process created
  p->e_time = 0;
  p->s_time = 0;    // When was the process started
  p->iow_time = 0;  // time for which process is SLEEPING.
  p->tot_wtime = 0; // total waittime for cpu for a process.
  p->r_time = 0;    // how long the process is running
  p->e_time = 0;    // When was the process exited
  p->n_run = 0;     // number of times a process is picked by a  cpu
  p->w_time = 0;

  for (int i = 0; i < 5; i++)
  {
    p->ticks[i] = 0;
    p->total_ticks[i] = 0;
  }

#ifdef FCFS
  p->SP = -1;
  p->DP = -1;
  p->niceness = -1;
#endif

#ifdef RR
  p->SP = -1;
  p->DP = -1;
  p->niceness = -1;
#endif

#ifdef PBS
  p->SP = 60;
  p->niceness = 5;
  p->DP = 60;
  if (p->pid <= 2)
    p->DP = 1;
#endif

#ifdef MLFQ
  p->q_num = 0;
#endif

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
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
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

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
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
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process. // SNXX: has to update the stime here
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  p->s_time = ticks;
  p->w_time = 0;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
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
//SNXX:again has to update stime here
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

  np->mask = p->mask;

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
  np->s_time = ticks;
  np->w_time = 0;

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
//SNXX: has to update etime here
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
  p->e_time = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
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
          np->n_run = 0;
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
    sleep(p, &wait_lock); //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// SNXX: has to do a lot of shit here
void scheduler(void)
{

  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

#ifdef RR
    if (sncount == 0)
    {
      //printf("entred in to RR bro\n");
      sncount++;
    }

    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        if (lastproc != p->pid)
        {
          p->n_run++;
          lastproc = p->pid;
        }

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
#endif

#ifdef FCFS
    if (sncount == 0)
    {
      //printf("entred in to FCFS bro");
      sncount++;
    }
    struct proc *min, *pi, *pj;

    for (pi = proc; pi < &proc[NPROC]; pi++)
    {
      acquire(&pi->lock);
      if (pi->state == RUNNABLE)
      {
        min = pi;
        for (pj = proc; pj < &proc[NPROC]; pj++)
        {

          if (pj->state == RUNNABLE && pj->pid > 2)
          {
            if (min->c_time > pj->c_time)
              min = pj;
          }
        }
        if (lastproc != min->pid)
        {
          min->n_run++;
          lastproc = min->pid;
        }

        min->state = RUNNING;
        c->proc = min;
        swtch(&c->context, &min->context);
        c->proc = 0;
      }
      release(&pi->lock);
    }
#endif

#ifdef PBS

    struct proc *min, *pi, *pj;

    for (pi = proc; pi < &proc[NPROC]; pi++)
    {
      acquire(&pi->lock);
      if (pi->state == RUNNABLE)
      {
        min = pi;
        for (pj = proc; pj < &proc[NPROC]; pj++)
        {

          if (pj->state == RUNNABLE)
          {
            if (min->DP > pj->DP)
              min = pj;

            else if (min->DP == pj->DP)
            {
              if (min->n_run > pj->n_run)
                min = pj;
              else if (min->n_run == pj->n_run)
              {
                if (min->c_time > pj->c_time)
                  min = pj;
              }
            }
          }
        }

        if (lastproc != min->pid)
        {
          min->n_run++;
          lastproc = min->pid;
        }

        min->state = RUNNING;
        c->proc = min;
        swtch(&c->context, &min->context);
        c->proc = 0;
      }
      release(&pi->lock);
    }

#endif

#ifdef MLFQ
    
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) //demoter
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        if (p->ticks[p->q_num] >= slice[p->q_num]) // I will demote you Bitch
        {
          //updating properties of process.......
          p->w_time = 0;
          p->ticks[p->q_num] = 0; // ticks spent by the process in the demoted queue
          if (p->q_num + 1 <= 4)
            p->q_num++;
          p->s_time = ticks; //s_time or the time at which the process becomes runnable in demoted queue
          //updated properties of process.........
        }
      }
      release(&p->lock);
    }

    for (p = proc; p < &proc[NPROC]; p++) //promoter
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        if ((ticks - p->s_time) > WTIME) // promote me you bitchy scheduler
        {
          //updating properties of process.......
          p->w_time = 0;
          p->ticks[p->q_num] = 0; // ticks spent by the process in the demoted queue
          if (p->q_num - 1 >= 0)
            p->q_num--;
          p->s_time = ticks; //s_time or the time at which the process becomes runnable in demoted queue
          //updated properties of process.........
        }
      }
      release(&p->lock);
    }

    struct proc *min;
    struct proc *debug = proc;
    acquire(&debug->lock);
    int flag = min->w_time;
    flag=0;
    for (int q = 0; q < 5; q++) //moving through the queues starting from 0;
    {
      for (p = proc; p < &proc[NPROC]; p++) //searcher loop
      {
        //acquire(&p->lock);
        if (p->state == RUNNABLE)
        {
          if (p->q_num == q) // this is the first encounter
          {
            flag = 1;
            min = p;
          }
        }
        //release(&p->lock);
        if (flag == 1)
          break;
      }
      if (flag == 1)
        break;
    }
    

    /*if (flag == 1)
    {
      //acquire(&min->lock);
      flag = 0;
      if (min != 0)
      {
        if (lastproc != min->pid)
        {
          min->n_run++;
          lastproc = min->pid;
        }
        min->w_time=0;

        min->state = RUNNING;
        c->proc = min;
        swtch(&c->context, &min->context);
        c->proc = 0;
      }
      //release(&min->lock);
    }*/
    release(&debug->lock);

    // RR DEBUGGING
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        if (lastproc != p->pid)
        {
          p->n_run++;
          lastproc = p->pid;
        }
        c->proc = 0;
      }
      release(&p->lock);
    }

#endif
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
//SNXX: has to update stime here
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  p->s_time = ticks;
  p->w_time = 0;
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
//SNXX: has to deal with some priority stuff here
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
#ifdef MLFQ
  p->ticks[p->q_num] = 0;
#endif

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
//SNXX: has to update stime here
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
        p->s_time = ticks;
        p->w_time = 0;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
//snxx here too stime and wtime
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
        p->s_time = ticks;
        p->w_time = 0;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
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
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state1;
  int headcount = 1;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state1 = states[p->state];
    else
      state1 = "???";

#ifdef PBS
    if (headcount == 1)
    {
      printf("PID        Priority        State        rtime        wtime        nrun\n");
      headcount++;
    }

    printf("%d             %d           %s           %d            %d          %d\n", p->pid, p->DP, state1, p->r_time, p->tot_wtime, p->n_run);
    printf("\n");
#endif

#ifdef MLFQ
    if (headcount == 1)
    {
      printf("PID        Priority        State        rtime        wtime        nrun\n");
      headcount++;
    }

    printf("%d             %d           %s           %d            %d          %d\n", p->pid, p->q_num, state1, p->r_time, p->w_time, p->n_run);
    printf("\n");
#endif

#ifdef FCFS
    if (headcount == 1)
    {
      printf("PID        State        rtime        wtime        nrun\n");
      headcount++;
    }

    printf("%d             %s          %d            %d          %d\n", p->pid, state1, p->r_time, p->tot_wtime, p->n_run);
    printf("\n");
#endif

#ifdef RR
    if (headcount == 1)
    {
      printf("PID        State        rtime        wtime        nrun\n");
      headcount++;
    }

    printf("%d             %s          %d            %d          %d\n", p->pid, state1, p->r_time, p->tot_wtime, p->n_run);
    printf("\n");
#endif
  }
}
int maxi(int a, int b)
{
  if (a > b)
    return a;
  else
    return b;
}
int mini(int a, int b)
{
  if (a < b)
    return a;
  else
    return b;
}

void updatetime()
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNING)
    {
      p->r_time += 1;
#ifdef MLFQ
      if (p->q_num >= 0)
      {
        p->ticks[p->q_num] += 1;
        p->total_ticks[p->q_num] += 1;
      }
#endif
    }

    if (p->state == SLEEPING)
      p->iow_time += 1;

    if (p->state == RUNNABLE)
    {
      p->tot_wtime += 1;
      p->w_time += 1;
    }

    int niceness = 10 * p->iow_time;
    if (p->iow_time + p->r_time != 0)
      niceness = niceness / (p->iow_time + p->r_time);
    else
      niceness = 5;
    p->DP = maxi(0, mini(p->SP - niceness + 5, 100));

    release(&p->lock);
  }
}

int procsetpriority(int pid, int priority)
{
  struct proc *p;
  int flag = 0;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      flag = 1;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }
  if (flag == 0)
  {
    return -1;
  }
  else
  {
    acquire(&p->lock);
    int oldpriority = p->SP;
    p->niceness = 5;
    p->SP = priority;
    p->DP = maxi(0, mini(p->SP, 100));
    release(&p->lock);
    if (p->DP < oldpriority)
      yield();
    return oldpriority;
  }
}
