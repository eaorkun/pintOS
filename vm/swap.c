#include "swap.h"

/*
The swap device is accessed and saved by invoking the block_get_role function with BLOCK_SWAP 
as the input that returns a pointer to the device. 

Then create a bitmap of length block_size/8 to keep track of the slots.
*/

// Size of swap is 4MB :)


void init_swap(void){
    swap_device_ptr = block_get_role(BLOCK_SWAP);
    swap_table = bitmap_create( block_size(swap_device_ptr)/SECTORS_PER_PAGE);
    bitmap_set_all(swap_table, false);
    numSlotsInUse = 0;
    lock_init(&swap_lock);
}


//Takes in spte and returns bitmapIndex location on swap disk. If invalid return -1;
int put_on_swap_disk(struct spte_t* spte){
    lock_acquire(&swap_lock);
    int startIndex;
    if(spte->value.swappedBefore == true){ //write to same spot
        startIndex = spte->value.swapIndex;
    }
    else{
        startIndex = bitmap_scan_and_flip(swap_table, 0 , 1, false);
        numSlotsInUse++;
        // printf("%d\n", numSlotsInUse);
        // if(numSlotsInUse == 514){
        //     bitmap_dump(swap_table);
        // }
    }
    // printf("WRITING FILE TO : 0x%x %d\n",spt3e->value.disk_file_ptr,spte->value.disk_file_ptr_offset);
    if (startIndex == BITMAP_ERROR){
        PANIC("YOU GOT A PANIC YO");
    }
    // printf("PUTTING ON SWAP DISK\n");
    void * buffer = spte->value.kpage;
    if (startIndex != BITMAP_ERROR){
        if(startIndex%10 == 0){
            // printf("Writing to Swap Slot: %d (printing every 10)\n", startIndex);
        }
         // Perform Swap
         for(block_sector_t i = startIndex * SECTORS_PER_PAGE; i < (startIndex + 1 ) * SECTORS_PER_PAGE; i++){
             block_write(swap_device_ptr, i , buffer);
             buffer += BLOCK_SECTOR_SIZE;
         }
         // Clear evicted memory
         memset(spte->value.kpage,0,PGSIZE);
         // Return Result
         lock_release(&swap_lock);
         return startIndex;   
    }
    // PANIC THE KERNEL, WE ARE OUT OF SWAP SPACE, should never be reached
    else{
    // PANIC("WE ARE OUT OF SWAP SPACE, NOTHING MAKES SENSE ANYMORE");
    }

    lock_release(&swap_lock);
    return -1;
}


//returns whether successful or not.
int read_from_swap_disk(void* kpage, int bitmapIndex){
    lock_acquire(&swap_lock);
    bool success = bitmap_test(swap_table, bitmapIndex);
    if(!success){
        lock_release(&swap_lock);
        return success;
    }

    for(block_sector_t swapIndex = bitmapIndex * SECTORS_PER_PAGE; swapIndex < (bitmapIndex+1) * SECTORS_PER_PAGE; swapIndex++){
        block_read(swap_device_ptr, swapIndex, kpage);
        kpage += BLOCK_SECTOR_SIZE;
    }
    // printf("READING FROM SWAP DISK: %x\n",kpage);
    // bitmap_set(swap_table, bitmapIndex, false);
    lock_release(&swap_lock);
    return success;
}
