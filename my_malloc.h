#ifndef MY_MEM_ALLOC
#define MY_MEM_ALLOC
#include <unistd.h>

typedef double align; // alignment type
typedef union header_t { // free list data structure
  struct {
    union header_t * next;
    size_t size;
  };
  align al; // not used, simply for alignment
} Header;


// use lock
void * ts_malloc_lock(size_t n);
void ts_free_lock(void * ptr);

// no lock thread safe
// only locks for calling sbrk
// sbrk is not thread safe
void * ts_malloc_nolock(size_t n);
void ts_free_nolock(void * ptr);

#endif
