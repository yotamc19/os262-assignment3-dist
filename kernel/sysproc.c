#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// sys_flip_display: zero-copy page flip.
//
// Syscall argument 0: user virtual address of a page-aligned buffer
// that is exactly GPU_FB_PAGES (300) * PGSIZE bytes (i.e. 640x480x4 =
// 1,228,800 bytes).  The buffer must already be fully mapped in the
// calling process's address space with user permission.
//
// Returns 0 on success, -1 on error.
uint64
sys_flip_display(void)
{
  uint64 buf;
  struct proc *p = myproc();

  argaddr(0, &buf);

  // Buffer must be page-aligned
  if (buf % PGSIZE != 0)
    return -1;

  // Walk page table, validate all pages have PTE_U, and re-point GPU backing
  if (virtio_gpu_flip(p->pagetable, buf) < 0)
    return -1;

  return 0;
}

// sys_map_display: map the GPU's kernel framebuffer pages (fb[]) directly
// into the calling process's address space with PTE_U|PTE_R|PTE_W.
//
// Syscall argument 0: desired user virtual address (must be page-aligned).
//   Pass 0 to let the kernel auto-select the next available VA above p->sz.
//
// Returns the mapped virtual address on success, (uint64)-1 on failure.
uint64
sys_map_display(void)
{
  uint64 addr;
  struct proc *p = myproc();

  argaddr(0, &addr);

  if (addr == 0) {
    // Auto-select: first page-aligned VA above the current heap
    addr = PGROUNDUP(p->sz);
  } else {
    // Caller-supplied address must be page-aligned
    if (addr % PGSIZE != 0)
      return -1;
  }

  // Ensure the entire region fits below the trapframe (top of user space)
  if (addr + (uint64)GPU_FB_PAGES * PGSIZE > TRAPFRAME)
    return -1;

  // Check that none of the GPU_FB_PAGES pages are already mapped
  for (uint64 va = addr; va < addr + (uint64)GPU_FB_PAGES * PGSIZE; va += PGSIZE) {
    pte_t *pte = walk(p->pagetable, va, 0);
    if (pte && (*pte & PTE_V))
      return -1;
  }

  // Install the mapping (kernel fb[] pages -> user VA, PTE_U|PTE_R|PTE_W)
  if (virtio_gpu_map_fb(p->pagetable, addr) < 0)
    return -1;

  // Remember the VA so freeproc() can remove the mapping on exit
  p->fb_mapped_va = addr;

  return addr;
}
