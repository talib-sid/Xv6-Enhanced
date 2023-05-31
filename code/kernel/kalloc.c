// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void refcountinit();
void safe_increment_references(void* pa);
void safe_decrement_references(void* pa);
int get_refcount(void *pa);
void reset_refcount(void* pa);

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Creating a Semaphore to record number of mappings to each page, we need this to handle multiple forks on parent
// since we can't free a pagetable if there are still references to its pages
struct {
  struct spinlock lock;
  int no_of_references[PGROUNDUP(PHYSTOP) >> 12];        
  // PHYSTOP is the upper bound for RAM usage for both user and kernel space
  // KERNBASE is the address from where the kernel starts
  // Total size = 128*1024*1024 = 2^27 bytes --> 2^27 PTEs
  // Each physical page table has 512 = 2^9 pages in it --> 2^9 * 8 bytes = 2^12 bytes
  // PGROUNDUP rounds it up to nearest 406-multiple
  
}ref_count;

void
kinit()
{
  refcountinit();
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    safe_increment_references(p);    // increment page references
    kfree(p);
  }  
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

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

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;      // store pointers, run is associated with each acquire

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    safe_increment_references((void*)r);
  }  
  return (void*)r;
}

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
