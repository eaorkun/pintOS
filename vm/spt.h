#ifndef SPT_H
#define SPT_H

#include <user/syscall.h>
#include "hash.h"
#include "lib/stdbool.h"
#include "filesys/off_t.h"
#include "threads/thread.h"

/** 
 * @brief We pass this as a function pointer to routines in the hash api that work with ordering
 * @return Returns a hash value for page p.
 */
unsigned
page_hash (const struct hash_elem *p_, void *aux);

/**
 * @brief  Returns true if foo a precedes foo b. 
 */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux);


/* Creates a Supplemental Page Table that takes a VPN as its key.
 * Returns the new spt, or a null pointer if memory
 * allocation fails. */
struct hash* spt_create(void);


//get result struct by the key of the vpn
struct spte_t* spt_get(uintptr_t vpn, struct thread * t);

struct spte_v {
    int location; // Memory, SWAP, or Disk

    // Disk Only 
    struct file* disk_file_ptr;
    off_t disk_file_ptr_offset;
    size_t disk_len;
    size_t disk_len_zero;
    bool writable;

    // In Memory -> Kernel page and frame are the same thing
    uint8_t* kpage; //idk exactly why it's needed
    int frame_no; // 0-366
    
    // SWAP
    int swapIndex;
    bool swappedBefore;
};

// Enums for SPTE Locations
enum locations{
    MEMORY,//0
    SWAP, //1
    DISK //2
};


/** This is the element type you are interested in making a hash of */ 
struct spte_t {   
    struct hash_elem hash_elem;   
    int key; /**< the key for ordering, can use any "comparable" type 
		a pointer for example (virtual addr of start of a page
		which can be thought of as the page number left 
		shifted by 12 bits) 
	     */
    struct spte_v value; ///< The payload - could be a struct
};

#endif 