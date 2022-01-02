#ifndef SWAP_H
#define SWAP_H

#include "devices/block.h"
#include "vm/swap.h"
#include "bitmap.h"
#include "vm/spt.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#define SECTORS_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)
struct block* swap_device_ptr;
struct bitmap* swap_table;
int numSlotsInUse;
struct lock swap_lock;

void init_swap(void);
int read_from_swap_disk(void * kpage, int index);
int put_on_swap_disk(struct spte_t* spte);

#endif