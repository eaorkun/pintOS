//greedy "load" everything into spt from process i
#include <stdio.h>
#include <stdlib.h>
#include "vm/hash.h"
#include "vm/spt.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/** 
 * @brief We pass this as a function pointer to routines in the hash api that work with ordering
 * @return Returns a hash value for page p.
 */
unsigned
page_hash (const struct hash_elem *p_, void *aux)
{
  const struct spte_t *p = hash_entry(p_, struct spte_t, hash_elem);
  return hash_bytes (&p->key, sizeof(p->key));
}

/**
 * @brief  Returns true if foo a precedes foo b. 
 */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux)
{
  const struct spte_t *a = hash_entry (a_, struct spte_t, hash_elem);
  const struct spte_t *b = hash_entry (b_, struct spte_t, hash_elem);

  return a->key < b->key;
}


/* Creates a Supplemental Page Table that takes a VPN as its key.
 * Returns the new spt, or a null pointer if memory
 * allocation fails. */
struct hash* spt_create(void){
  // Malloc the SPT first
  struct hash * spt = malloc(sizeof(struct hash));
  hash_init(spt, page_hash, page_less, NULL);
  return spt;
}

//get result struct by the key of the vpn
struct spte_t* spt_get(uintptr_t vpn, struct thread * t){
    //search spt by vpn (which we recover from the stack pointer)
    struct hash * spt = t->spt;
    struct spte_t scratch; 
    scratch.key = vpn;
    struct hash_elem *e;
    struct spte_t *result;
    
    e = hash_find(spt, &scratch.hash_elem);
    if (e != NULL){
        result = hash_entry(e, struct spte_t, hash_elem);
    }
    else{
      result = NULL;
    }
    return result;
}






