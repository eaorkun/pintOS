#ifndef FRAME_ALLOC
#define FRAME_ALLOC

struct frame_element ** frame_table;

void frame_init(void);
struct frame_element{
    void * upage; //user virtual address
    struct thread * t; // user thread
    bool pinned;
};

void frame_init(void);

// We know that a frame num will always map to the same k page
void * get_kpage_by_frame_num(int frame_num);
int frame_address_to_frame_num(void * addr);
void * falloc_get_frame(void* upage);
int evict(void);
void falloc_free_frame(void * page);

// void evict(void *upage, struct thread *t);
#endif 