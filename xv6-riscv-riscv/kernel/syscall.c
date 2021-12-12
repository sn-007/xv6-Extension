#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_strace(void);
extern uint64 sys_setpriority(void);

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_strace]   sys_strace,
[SYS_setpriority] sys_setpriority,

};

void
syscall(void)
{
  int num;
  int firstarg;
  firstarg=0;
  struct proc *p = myproc();


  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) 
  {
    firstarg=p->trapframe->a0;
    p->trapframe->a0 = syscalls[num]();
    
  } 
  else 
  {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
  
    
  if (p->mask & (1<<num)) 
  {
    switch(num)
    {
      case 1:
        printf("%d: syscall fork{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 2:
        printf("%d: syscall exit{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 3:
        printf("%d: syscall wait{%d} => %d\n", p->pid, firstarg , p->trapframe->a0);
        break;
      case 4:
        printf("%d: syscall pipe{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 5:
        printf("%d: syscall read{%d %d %d} => %d\n", p->pid, firstarg,p->trapframe->a1,p->trapframe->a2,  p->trapframe->a0);
        break;
      case 6:
        printf("%d: syscall kill{%d %d} => %d\n", p->pid, firstarg, p->trapframe->a1 , p->trapframe->a0);
        break;
      case 7:
        printf("%d: syscall exec{%d %d} => %d\n", p->pid, firstarg, p->trapframe->a1 , p->trapframe->a0);
        break;
      case 8:
        printf("%d: syscall fstat{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 9:
        printf("%d: syscall chdir{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 10:
        printf("%d: syscall dup{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 11:
        printf("%d: syscall getpid{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 12:
        printf("%d: syscall sbrk{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 13:
        printf("%d: syscall sleep{%d} => %d\n", p->pid, firstarg ,  p->trapframe->a0);
        break;
      case 14:
        printf("%d: syscall uptime{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 15:
        printf("%d: syscall open{%d %d} => %d\n", p->pid, firstarg, p->trapframe->a1 , p->trapframe->a0);
        break;
      case 16:
        printf("%d: syscall write{%d %d %d} => %d\n", p->pid, firstarg,p->trapframe->a1,p->trapframe->a2,  p->trapframe->a0);
        break;
      case 17:
        printf("%d: syscall mknod{} => %d\n", p->pid,  p->trapframe->a0);
        break;
      case 18:
        printf("%d: syscall unlink{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 19:
        printf("%d: syscall link{%d %d} => %d\n", p->pid, firstarg, p->trapframe->a1 , p->trapframe->a0);
        break;
      case 20:
        printf("%d: syscall mkdir{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 21:
        printf("%d: syscall close{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;
      case 22:
        printf("%d: syscall strace{%d} => %d\n", p->pid, firstarg,  p->trapframe->a0);
        break;


    }

    
    
      



    
  }
  
  
}

/*
switch(num)
    {
      case 1:
        printf("pid is %d-------num is %d---------syscall is fork-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 2:
        printf("pid is %d-------num is %d---------syscall is exit-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 3:
        printf("pid is %d-------num is %d---------syscall is wait-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 4:
        printf("pid is %d-------num is %d---------syscall is pipe-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 5:
        printf("pid is %d-------num is %d---------syscall is read-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 6:
        printf("pid is %d-------num is %d---------syscall is kill-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 7:
        printf("pid is %d-------num is %d---------syscall is exec-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 8:
        printf("pid is %d-------num is %d---------syscall is fstat-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 9:
        printf("pid is %d-------num is %d---------syscall is chdir-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 10:
        printf("pid is %d-------num is %d---------syscall is dup-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 11:
        printf("pid is %d-------num is %d---------syscall is getpid-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 12:
        printf("pid is %d-------num is %d---------syscall is sbrk-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 13:
        printf("pid is %d-------num is %d---------syscall is sleep-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 14:
        printf("pid is %d-------num is %d---------syscall is uptime-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 15:
        printf("pid is %d-------num is %d---------syscall is open-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 16:
        printf("pid is %d-------num is %d---------syscall is write-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 17:
        printf("pid is %d-------num is %d---------syscall is mknod-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 18:
        printf("pid is %d-------num is %d---------syscall is unlink-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 19:
        printf("pid is %d-------num is %d---------syscall is link-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 20:
        printf("pid is %d-------num is %d---------syscall is mkdir-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 21:
        printf("pid is %d-------num is %d---------syscall is close-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;
      case 22:
        printf("pid is %d-------num is %d---------syscall is strace-----------return value is %d\n", p->pid, num, p->trapframe->a0);
        break;


    }
    */
