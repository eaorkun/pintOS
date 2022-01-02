//FROM NOW ON: anytime we want a frame, we make a call to our frame
//allocator instead of palloc_get_page(PAL_USER); (Replace all palloc_get_page calls with our own calls)
#define NUMBER_USER_FRAMES 367
#define BASE_USER_POOL 0xc0271000 //IS THIS ACTUALLY CORRECT? (and consistent between different executions)
#define FRAME_SIZE 4096

#include "threads/palloc.h"
#include "lib/kernel/bitmap.h"
#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/spt.h"
#include "threads/synch.h" // For Locks
#include "vm/swap.h"

//get all pages from user pool and hog them all => Greedy bitch :)
// Note: Kpage are from user pool -> Confusing bitch :)

// Bitmap for All User Pages

//global elements owned by OS in Kernel pool
struct bitmap *pages;
struct lock frame_lock;
int frames_in_use = 0;
int clock_hand = 0;

void frame_init(void){
    // Initialize Bitmap to 367 bits -> representing offset in (bit * sizeofframe)
    pages = bitmap_create(NUMBER_USER_FRAMES);
    frame_table = malloc(NUMBER_USER_FRAMES * sizeof(struct frame_element));
    lock_init(&frame_lock);

    // Frame Table: Should have 367 entries with each entry having a pointer to the user page and the process for which the user page resides in 
    //367 calls to palloc_get_page(PAL_USER), alternatively we could just make palloc_get_page return null?)
    for(int i = 0; i < NUMBER_USER_FRAMES; ++i){
        // Returning kernel virtual address == physical frame address (LMAO)
        void* b = palloc_get_page(PAL_USER);
        // printf("POINTER: %p %d\n", b,i);
        frame_table[i] = malloc(sizeof(struct frame_element));
        frame_table[i]->pinned = false;
    }
    // Debug Print Contents of our buffer
    bitmap_set_all(pages,false);
    // bitmap_dump(pages);
    frames_in_use = 0;
}

// We know that a frame num will always map to the same k page
void * get_kpage_by_frame_num(int frame_num){
    void* kpage = (void * ) (BASE_USER_POOL + (FRAME_SIZE) * frame_num);
    return kpage;
}

int frame_address_to_frame_num(void * addr){
    return ((int) addr - BASE_USER_POOL) >> 12; 
}


/* Obtains a single free page and returns its kernel virtual
 * address.
 * The page is obtained from the user pool,
 * If no pages are available, returns a null pointer */

void * falloc_get_frame(void * upage){
    lock_acquire(&frame_lock);
    // Test if you can even access thread in current context
    struct thread *t = thread_current();
     
    if(frames_in_use == NUMBER_USER_FRAMES){
        // printf("Filled up\n");
        
        // Clock Algorithm
        int frameIndex = evict(); 

        // Update new page in the Frame Table (and swap if necessary)
        frame_table[frameIndex]->upage = upage;
        frame_table[frameIndex]->t = t;
        frame_table[frameIndex]->pinned = true;

        // Return Address
        lock_release(&frame_lock);
        return get_kpage_by_frame_num(frameIndex);
    }
    else{
        // TODO: Locks and concurrency
        for(int i = 0; i < NUMBER_USER_FRAMES; ++i){
            if(bitmap_test(pages, i) == 0){
                bitmap_set(pages, i, true);
                frames_in_use+=1;

                // House Keeping
                frame_table[i]->upage = upage;
                frame_table[i]->t = t;
                frame_table[i]->pinned = true;
                struct spte_t* result = spt_get(pg_no(frame_table[i]->upage),t);
                result->value.location = MEMORY;
                
                lock_release(&frame_lock);
                return get_kpage_by_frame_num(i);
            }
            
        }
    }
    lock_release(&frame_lock);
    return NULL; 
}

void falloc_free_frame(void * page){
    lock_acquire(&frame_lock);
    bitmap_set(pages, frame_address_to_frame_num(page), false);
    frames_in_use-=1;
    lock_release(&frame_lock);
}

//eviction algorithm returns the frame index which can then be used.
int evict(void){
    int refIndex;
    // Clock Algorithm 
    struct thread* t = frame_table[clock_hand]->t;

    //finding frame
    while((pagedir_is_accessed(t->pagedir, frame_table[clock_hand]->upage)) || frame_table[clock_hand]->pinned == true){
        pagedir_set_accessed(t->pagedir, frame_table[clock_hand]->upage, false);
        // printf("Checking Fram %d\n",clock_hand);
        clock_hand = ((clock_hand + 1) % NUMBER_USER_FRAMES);  
        t = frame_table[clock_hand]->t;
    }
    // Out of loop, perform actions
    refIndex = clock_hand;
    // printf("REFINDEX: %d\n",refIndex);
    // printf("Accessed bits: %d\n",pagedir_is_accessed(t->pagedir, frame_table[clock_hand]->upage));
    
    //Evict old owner
    struct spte_t* result = spt_get(pg_no(frame_table[refIndex]->upage),t);

    if(result->value.writable){
        
        // Check if on swap and not dirty => DONT WRITE
        if(result->value.swappedBefore == true && !pagedir_is_dirty(t->pagedir, frame_table[refIndex]->upage)){
            result->value.location = SWAP;
        }
        else{ //write if dirty             
            result->value.location = SWAP;
            int swapIndex = put_on_swap_disk(result);
            result->value.swappedBefore = true;
            result->value.swapIndex = swapIndex;
        }

        // Run out of SWAP Space    
        if(result->value.swapIndex == -1){
            PANIC("SWAP RETURNED -1 \n");
        }
    }
    else{
        // Make a note that it can be recovered from DISK
        result->value.location = DISK;
    }

    //regardless, free frame
    // falloc_free_frame(get_kpage_by_frame_num(ref));
    // memset(get_kpage_by_frame_num(refIndex), 0, PGSIZE);
    
    // TODO: SET P bit to 0 in old owners page directory
    pagedir_clear_page(frame_table[refIndex]->t->pagedir, frame_table[refIndex]->upage);
    frame_table[refIndex]->pinned=true;
    // bitmap_dump(pages);

    // Increment Clock hand to set up for next time.
    clock_hand = ((clock_hand + 1) % NUMBER_USER_FRAMES); 
    return refIndex;
}

