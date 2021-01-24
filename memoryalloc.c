/* Memory allocator implemented as a linked list using sbrk()
 * 
 * Used as practice to understand more about mem alloc 
 * 
 * gcc -o memoryalloc.so -fPIC -shared memoryalloc.c
 * -fPIC and -shared -> ensures position-independent code and linker to produce shared object for dyn. linking
 * run export LD_PRELOAD=$PWD/memoryalloc.so to be able to use this before any other library 
 * unset LD_PRELOAD to revert 
 * 
 * or just link c file in another c file (simple example)
 * run gcc -o main <file name>.c 
 * 
 * Based on https://arjunsreedharan.org/post/148675821737/write-a-simple-memory-allocator
 * 
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h> /* posix has sbrk, stdlib is for c standard and actual malloc */
#include <pthread.h>

#define _XOPEN_SOURCE 700

typedef char alignment[16]; /* memory aligned to 16 bytes (addr multiple of) */

union header { /* header/node for a block of allocated memory */
    struct {
        size_t size; /* size of block, at most 8 bytes */
        unsigned is_free; /* boolean indicator of if the block is free, 4 bytes */
        union header * next; /* next block linked, 16 bytes */
    } metadata; 

    alignment stub; /* alignment array of 16 bytes */
};

typedef union header header_t; /* alias */ 

pthread_mutex_t malloc_lock; 

header_t *head, *tail; 

/* 
 * sbrk() - stack_break, break being the end of the heap segment 
 * sbrk(0) = current addr
 * sbrk(x) increments brk by x bytes, allocates memory
 * sbrk(-x) decrements brk by x bytes, deallocates
 * 
 * Returns previous break value, or (void *) -1 on error
 * 
 * note: not thread safe, LIFO (Stack) order, used by glibc malloc 
 */

void *malloc(size_t size);
header_t *check_for_block(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t nsize);
void *realloc();

/* c memory alloctor function of size "size" */
void *malloc(size_t size) {

    size_t total_size; 
    void *block;
    header_t *header; 

    /* size must be non zero */
    if(!size) {
        return NULL; 
    }

    /* check if header of size exists, first fit approach */
    pthread_mutex_lock(&malloc_lock); 
    header = check_for_block(size); 
    if(header) {
        header->metadata.is_free = 0; 
        pthread_mutex_unlock(&malloc_lock);
        return (void*)(header+1); /* data begins 1 byte after header */
    }
    
    /* create block otherwise */
    total_size = sizeof(header_t) + size; 
    block = sbrk(total_size);
    if(block == (void*) -1) { /* error with sbrk */
        pthread_mutex_unlock(&malloc_lock);
        return NULL;
    } 
    /* assign metadata on the block of memory */
    header = block; 
    header->metadata.size = size;
    header->metadata.is_free = 0;
    header->metadata.next = NULL; 

    /* heap bound checks/assignments */
    if(!head) { /* if head = NULL */
        head = header;
    }
    if(tail) { /* if tail != NULL */
        tail->metadata.next = header; 
    }
    tail = header; 
    pthread_mutex_unlock(&malloc_lock);
    return (void*)(header + 1); /* data begins 1 byte after header */
}

/* header checking function to see if existing header with desired malloc size exists */
header_t *check_for_block(size_t size) {
    header_t *curr = head; 
    while(curr) {
        if(curr->metadata.is_free && curr->metadata.size >= size) {
            return curr;
        }
        curr = curr->metadata.next; 
    }
    return NULL; 
}

/* c memory deallocator */ 
void free(void *block) {
    header_t *header;
    void * progbrk; 

    /* ptr must be nonnull */
    if(!block) {
        return;
    }

    /* setup header size by stepping back by 1 byte from address of block */
    /* recall that in malloc, we return address of header + 1 for the data */
    pthread_mutex_lock(&malloc_lock);
    header = (header_t*)block - 1; /* header_t* +- 1 (1 = sizeof(header_t*)*1) */

    /* get current heap break */
    progbrk = sbrk(0); 
    /* if block is at current heap break, it can be removed */
    if((char*)block + header->metadata.size == progbrk) { /* check end of block to see if at current break */
        if(head == tail) { /* 1 element heap */ 
            head = NULL;
            tail = NULL;
        } else { /* reset the tail block of header to the one before the tail */
            header_t *tmp = head; 
            while(tmp) {
                if(tmp->metadata.next == tail) {
                    tmp->metadata.next = NULL;
                    tail = tmp; 
                }
                tmp = tmp->metadata.next;
            }
        }
        sbrk(0 - sizeof(header_t) - header->metadata.size); /* deallocate heap of tail header and data */
        pthread_mutex_unlock(&malloc_lock);
        return; 
    }

    /* if block is not at the end, then mark it as free */
    /* can be reused later */ 
    header->metadata.is_free = 1;
    pthread_mutex_unlock(&malloc_lock);
    return;
}

/* initialize an heap array of size "num" with element of size "esize" */ 
void *calloc(size_t num, size_t esize) {
    size_t total; 
    void *block;

    if(!num || !esize) { /* non zero value of size or element size*/
        return NULL;
    }

    total = num * esize; /* array size = # of elements * size of 1 element (all in bytes) */ 
    if(esize != total / num) { /* check overflow from multiplication */
        return NULL;
    }
    
    block = malloc(total);
    if(!block) {
        return NULL;
    } 
    memset(block, 0, total); /* zero inidialize allocated block */ 
    return block; 
}

void *realloc(void *block, size_t size) {
    header_t *header;
    void *reblock;
    
    if(!block || !size) { /* block and size must be nonnull and nonzero */ 
        return malloc(size);
    }

    header = (header_t*)block - 1; /* find header by subtracting 1 from address */  
    if(header->metadata.size >= size) { /* if new size is same or smaller than old size */
        return block; 
    }

    reblock = malloc(size); /* this returns data only, and also may or may not allocate new break */
    if(reblock) { /* if new block is allocated, copy over new data and free old block */
        memcpy(reblock, block, header->metadata.size);
        free(block); /* may or may not deallocate in sbreak */
    }

    return reblock; 
}

void print_memory() {
    header_t *curr = head; 
    printf("head = %p, tail = %p \n", (void*)head, (void*)tail);

    while(curr) {
        printf("addr = %p, size =%zu, is_free %u, next=%p\n", 
        (void*)curr, curr->metadata.size, curr->metadata.is_free, (void*)curr->metadata.next);

        curr = curr->metadata.next; 
    }
}
