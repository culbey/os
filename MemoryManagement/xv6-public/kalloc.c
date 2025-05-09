// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld


unsigned char ref_counts[NUMPHYPGS];   // Reference counts for each physical page
struct spinlock ref_lock;   

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  initlock(&ref_lock, "ref_counts");        // Initialize the spinlock
  for (int i = 0; i < NUMPHYPGS; i++) {
    ref_counts[i] = 0;                     // Set all reference counts to 0
  }
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");


  if (kmem.use_lock) {
    int ind = (V2P(v) >> PTXSHIFT);
    acquire(&ref_lock);

    ref_counts[ind]--;
    if (ref_counts > 0) {
      release(&ref_lock);
      return;
    }

    release(&ref_lock);
  }


  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);


  if (kmem.use_lock) {
    int ind = (V2P((char *) r) >> PTXSHIFT);
    acquire(&ref_lock);
    ref_counts[ind] = 1;
    release(&ref_lock);
  }  

  return (char*)r;
}


uint get_pg_ref(uint pa) {
  uint ind = (pa >> PTXSHIFT);
  uint v;
  acquire(&ref_lock);
  v = ref_counts[ind];
  release(&ref_lock);
  return v;
}

uint
inc_page(uint pa) {
  
  uint ind = (pa >> PTXSHIFT);
  uint v;
  acquire(&ref_lock);
  ref_counts[ind]++;
  v = ref_counts[ind];
  release(&ref_lock);
  return v;
}

uint
dec_page(uint pa) {
  
  uint ind = (pa >> PTXSHIFT);
  uint v;
  acquire(&ref_lock);
  ref_counts[ind]--;
  v = ref_counts[ind];
  release(&ref_lock);
  return v;
  
}
