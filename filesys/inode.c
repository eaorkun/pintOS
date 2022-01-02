#include <debug.h>
#include <round.h>
#include <string.h>

#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define BYTES_DPtr (4*NUM_DIRECT_BLOCKS)
#define BYTES_IDPtr (4*NUM_INDIRECT_BLOCKS)
#define BYTES_DIDPtr (4*NUM_INDIRECT_BLOCKS*128)

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors(off_t size)
{
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
 * within INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
    ASSERT(inode != NULL);
    if (pos < inode->data.length) {
        int sector_index = pos / BLOCK_SECTOR_SIZE;
        
        // printf("%d/%d\n", sector_index,NUM_DIRECT_BLOCKS);
        // // Case 1: We have enough Direct Pointers
        if(sector_index < NUM_DIRECT_BLOCKS){
            return inode->data.DPters[sector_index];
        }

        // // Case 2: We need to use INODE_INDIRECTS
        int indirect_sector_index = sector_index - NUM_DIRECT_BLOCKS;
        if(indirect_sector_index < NUM_INDIRECT_BLOCKS){
            uint32_t indirect_block[NUM_INDIRECT_BLOCKS]; //TODO: CHECK IF WE CAN PASS A POINTER AS A BUFFER
            block_read(fs_device, inode->data.IDPter,&indirect_block);
            return  indirect_block[indirect_sector_index];
        }

        // // Case 3: Double Indirect Blocks
        // int double_indirect_sector_index = sector_index - NUM_DIRECT_BLOCKS - NUM_INDIRECT_BLOCKS;
        // int double_indirect_offset = double_indirect_sector_index % NUM_INDIRECT_BLOCKS;
        // double_indirect_sector_index /= NUM_INDIRECT_BLOCKS;

        // if(double_indirect_sector_index < NUM_INDIRECT_BLOCKS){
        //     uint32_t doubly_indirect_block[NUM_INDIRECT_BLOCKS]; //TODO: CHECK IF WE CAN PASS A POINTER AS A BUFFER
        //     block_read(fs_device, inode->data.DIDPter, &doubly_indirect_block);
        //     uint32_t indirect_block[NUM_INDIRECT_BLOCKS]; //TODO: CHECK IF WE CAN PASS A POINTER AS A BUFFER
        //     block_read(fs_device,doubly_indirect_block[double_indirect_sector_index], &indirect_block);
        //     return indirect_block[double_indirect_offset];
        // }

        // PASSES
        return inode->data.DPters[0] + sector_index;
    } else {
        return -1;
    }
}

// Returns true if no issues, false if fails or out of disk space
// if new_length < inode.data.length then just return true, no additional allocations needed
bool inode_extend(struct inode_disk *inode, int new_length){
    
    //return true if new length is smaller 
    if(new_length < inode->length){
        return true;
    }
    
    //return true if still space in current sector
    off_t difference = new_length - inode->length;
    off_t offset_length = inode->length % BLOCK_SECTOR_SIZE;
    
    if(offset_length != 0 && (offset_length + difference) <= BLOCK_SECTOR_SIZE){
        inode->length += difference;
        return true;
    }
    
    //account for space in current sector
    //extend to account for the rest of the difference
    if(inode->length % BLOCK_SECTOR_SIZE != 0){
        inode->length += (BLOCK_SECTOR_SIZE - offset_length);
        difference -= (BLOCK_SECTOR_SIZE - offset_length);
    }



    //Entering while loop assumes that data.length is 512 aligned and difference is postive.

    const int BYTES_IN_ALL_DPTRS = (NUM_DIRECT_BLOCKS*BLOCK_SECTOR_SIZE);
    const int BYTES_IN_ALL_IDPTRS = (NUM_INDIRECT_BLOCKS*BLOCK_SECTOR_SIZE);
    while(difference){

        // CASE 1: extend in double indirect pointer block
        if(inode->length >= (BYTES_IN_ALL_IDPTRS + BYTES_IN_ALL_DPTRS)){
            int cur_didptr_index = (inode->length - NUM_DIRECT_BLOCKS*BLOCK_SECTOR_SIZE - NUM_INDIRECT_BLOCKS*BLOCK_SECTOR_SIZE)/(BLOCK_SECTOR_SIZE*NUM_INDIRECT_BLOCKS*NUM_DIRECT_BLOCKS); 
            int cur_idptr_index = (inode->length - NUM_DIRECT_BLOCKS*BLOCK_SECTOR_SIZE - NUM_INDIRECT_BLOCKS*BLOCK_SECTOR_SIZE)%(BLOCK_SECTOR_SIZE*NUM_INDIRECT_BLOCKS*NUM_DIRECT_BLOCKS);
            block_sector_t new_sector_outer = 0;
            block_sector_t new_sector_inner = 0;
            
            //extend up to data.length + difference
            if(difference <= ((BYTES_DIDPtr*BLOCK_SECTOR_SIZE) - inode->length)){
                uint32_t *bounce_outer = malloc(BLOCK_SECTOR_SIZE);
                block_read(fs_device, inode->DIDPter, bounce_outer); 
                for(int i = 0; i < difference/(BLOCK_SECTOR_SIZE*BYTES_IDPtr); ++i){

                    //Only allocate for outer if needed (don't allocate every time) 
                    if(cur_idptr_index != 0){
                        if(!free_map_allocate(1, &new_sector_outer)){
                            PANIC("DISK FULL, COULD NOT ALLOCATE");
                        }
                        bounce_outer[cur_didptr_index+i] = new_sector_outer;
                    }
                    uint32_t *bounce_inner = malloc(BLOCK_SECTOR_SIZE);
                    block_read(fs_device, bounce_outer[cur_didptr_index+i], bounce_inner);
                    for(int j = cur_idptr_index; j < NUM_INDIRECT_BLOCKS; ++j){
                        if(!free_map_allocate(1, &new_sector_inner)){
                            PANIC("DISK FULL, COULD NOT ALLOCATE");
                        }
                        bounce_inner[cur_idptr_index] = new_sector_inner;
                    }
                    cur_idptr_index=0;
                    block_write(fs_device, bounce_outer[cur_didptr_index+i], bounce_inner);
                    free(bounce_inner);
                } 

                inode->length += difference;
                difference -= difference;
                block_write(fs_device, inode->IDPter, bounce_outer);
                free(bounce_outer);
            }
            //fill up block completely
            else{ 
                PANIC("Requested to extend more than inode struct current defines");
            }
        }

        //CASE 2: extend in indirect pointer block
        else if(inode->length >= BYTES_IN_ALL_DPTRS){ 
            // ALLOCATE IF INDIRECT BLOCK NECESSARY 
            block_sector_t indirect_sector = 0;
            if(inode->IDPter == 0){
                if(!free_map_allocate(1, &indirect_sector)){
                    PANIC("DISK FULL, COULD NOT ALLOCATE");
                }
                inode->IDPter = indirect_sector;
            }

            int cur_idptr_index = (inode->length - BYTES_IN_ALL_DPTRS)/BLOCK_SECTOR_SIZE;
            block_sector_t newSector = 0;
            uint32_t bounce[128];
            block_read(fs_device, inode->IDPter, bounce);

            //extend up to data.length + difference
            if(difference < (BYTES_IN_ALL_IDPTRS - (inode->length - BYTES_IN_ALL_DPTRS))){ 
                for(int i = 0; i < bytes_to_sectors(difference); ++i){ //this for loop may be wrong
                    if(!free_map_allocate(1, &newSector)){
                        PANIC("DISK FULL, COULD NOT ALLOCATE");
                    }
                    bounce[cur_idptr_index+i] = newSector;
                }   
                
                inode->length += difference;
                difference -= difference;
            }
            //fill up IDP block completely
            else{ 
                for(int i = cur_idptr_index; i < NUM_INDIRECT_BLOCKS; ++i){
                    if(!free_map_allocate(1, &newSector)){
                        PANIC("DISK FULL, COULD NOT ALLOCATE");
                    }
                    bounce[i] = newSector;
                } 
                
                difference -= ((NUM_INDIRECT_BLOCKS*BLOCK_SECTOR_SIZE) - inode->length);
                inode->length += (BYTES_IN_ALL_IDPTRS - inode->length);
            }
            block_write(fs_device, inode->IDPter, bounce);
            // free(bounce);
        }

        //CASE 3: extend direct pointers
        else{ 
            int cur_dptr_index = inode->length/BLOCK_SECTOR_SIZE;
            block_sector_t newSector = 0;
            // extend dp up to data.length+difference

            static char zeroes[BLOCK_SECTOR_SIZE]; 
            if(difference < (BYTES_IN_ALL_DPTRS - inode->length)){ 
                for(unsigned int i = 0; i < bytes_to_sectors(difference); ++i){ // could be the problem
                    if(!free_map_allocate(1, &newSector)){
                        PANIC("DISK FULL, COULD NOT ALLOCATE");
                    }
                    inode->DPters[cur_dptr_index+i] = newSector;
                    block_write(fs_device, inode->DPters[cur_dptr_index+i], zeroes);
                    // if(newSector == 81){
                    //     printf("bitch\n");
                    // }
                }                
                inode->length += difference;
                difference -= difference;
            }
            // extend dp fully
            else{
                for(int i = cur_dptr_index; i < NUM_DIRECT_BLOCKS; ++i){
                    if(!free_map_allocate(1, &newSector)){
                        PANIC("DISK FULL, COULD NOT ALLOCATE");
                    }
                    inode->DPters[i] = newSector;
                    block_write(fs_device, inode->DPters[i], zeroes);
                }   
                difference -= (BYTES_IN_ALL_DPTRS - inode->length);
                inode->length += (BYTES_IN_ALL_DPTRS - inode->length);
            }
        }
    }
    return true; 
}


/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init(void)
{
    list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * device.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create(block_sector_t sector, off_t length, int type)
{
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
     * one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);

    if (disk_inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->type = type; // Added Type: undefined, directory, or file 
        disk_inode->magic = INODE_MAGIC;
        disk_inode->length = 0;

        if(type == INODE_FREEMAP){
            disk_inode->length = length;
            if (free_map_allocate(sectors, &disk_inode->DPters[0])) {
                // Writing the INODE
                block_write(fs_device, sector, disk_inode);
                if (sectors > 0) {
                    static char zeros[BLOCK_SECTOR_SIZE];
                    size_t i;

                    for (i = 0; i < sectors; i++) {
                        // Writing the data contents of the file/directory
                        block_write(fs_device, disk_inode->DPters[0] + i, zeros);
                    }
                }
                success = true;
            }
        }
        else if(type == INODE_FILE || type == INODE_DIR){
            // Just Write the INODE
            disk_inode->length = 0;
            inode_extend(disk_inode, length);
            block_write(fs_device, sector, disk_inode);
            success = true;
        }

        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector)
{
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL) {
        return NULL;
    }

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    block_read(fs_device, inode->sector, &inode->data);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
    if (inode != NULL) {
        inode->open_cnt++;
    }
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
    return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close(struct inode *inode)
{
    /* Ignore null pointer. */
    if (inode == NULL) {
        return;
    }

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);
            free_map_release(inode->data.DPters[0],
                             bytes_to_sectors(inode->data.length));
        }

        free(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove(struct inode *inode)
{
    ASSERT(inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    uint8_t *bounce = NULL;

    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
            break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Read full sector directly into caller's buffer. */
            block_read(fs_device, sector_idx, buffer + bytes_read);
        } else {
            /* Read sector into bounce buffer, then partially copy
             * into caller's buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL) {
                    break;
                }
            }
            block_read(fs_device, sector_idx, bounce);
            memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    free(bounce);

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at(struct inode *inode, const void *buffer_, off_t size,
               off_t offset)
{
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t *bounce = NULL;

    if (inode->deny_write_cnt) {
        return 0;
    }
    
    // if(!inode_extend(inode, size + offset)){
    //     PANIC("Out of Space");
    // }
    // else{
    //     block_write(fs_device,inode->sector,&inode->data);
    // }

    if(inode->data.type != FREE_MAP_SECTOR){ 
        inode_extend(&(inode->data), size + offset);
        block_write(fs_device, inode->sector, &inode->data);
    }

    
    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset; //closer to end of file
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs; //closer to end of sector
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
            break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Write full sector directly to disk. */
            block_write(fs_device, sector_idx, buffer + bytes_written);
        } else {
            /* We need a bounce buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL) {
                    break;
                }
            }

            /* If the sector contains data before or after the chunk
             * we're writing, then we need to read in the sector
             * first.  Otherwise we start with a sector of all zeros. */
            if (sector_ofs > 0 || chunk_size < sector_left) {
                block_read(fs_device, sector_idx, bounce);
            } else {
                memset(bounce, 0, BLOCK_SECTOR_SIZE);
            }
            memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
            block_write(fs_device, sector_idx, bounce);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    free(bounce);

    return bytes_written;
}

/* Disables writes to INODE.
 * May be called at most once per inode opener. */
void
inode_deny_write(struct inode *inode)
{
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write(struct inode *inode)
{
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length(const struct inode *inode)
{
    return inode->data.length;
}
