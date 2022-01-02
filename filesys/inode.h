#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <list.h>
#include <stdbool.h>

#include "devices/block.h"
#include "filesys/off_t.h"

#define NUM_DIRECT_BLOCKS 123
#define NUM_INDIRECT_BLOCKS 128

enum inode_types{
    INODE_UNINIT,
    INODE_DIR,
    INODE_FILE,
    INODE_FREEMAP
};



struct bitmap;

/* On-disk inode.
 * Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    int type; // 0 = uninitialized, 1 = dir, 2 = file
    off_t          length;      /* File size in bytes. */
    unsigned       magic;       /* Magic number. */
    uint32_t       DPters[NUM_DIRECT_BLOCKS]; /* Direct Pointer */
    uint32_t       IDPter; // Indirect Pointer
    uint32_t       DIDPter; /* Double Indirect Pointer */
};

/* In-memory inode. */
struct inode {
    struct list_elem  elem;           /* Element in inode list. */
    block_sector_t    sector;         /* Sector number of disk location. */
    int               open_cnt;       /* Number of openers. */
    bool              removed;        /* True if deleted, false otherwise. */
    int               deny_write_cnt; /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;           /* Inode content. */
};

void inode_init(void);
bool inode_create(block_sector_t, off_t, int);
bool inode_extend(struct inode_disk *inode, int new_length);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);

#endif /* filesys/inode.h */
