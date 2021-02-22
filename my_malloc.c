#include "my_malloc.h"
#include <stdio.h>
#include <limits.h>
#include <pthread.h>


// free list data
static Header * free_list = NULL; // entry of the free blocks cyclic ll
static Header base; // the very first Header


// mutexes
static pthread_mutex_t free_list_mutex = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t sbrk_mutex = PTHREAD_MUTEX_INITIALIZER;


// TLS static data
static __thread Header * tls_free_list = NULL;
static __thread Header tls_base;


// prototypes
Header * malloc_sys(size_t n, Header ** fl, int need);
void * processBlock(Header * start, size_t size);
void ts_sys_free_lock(void * ptr);
void insert_free_list(void * ptr, Header ** fl);
void coalescing_blocks(Header * toAdd, Header * block, Header ** fl);
void * my_malloc(size_t n, Header ** fl, int need_lock);


/* ts_malloc_lock
 * ---------------
 * Best fit memory allocator. Iterate through the linked list, find the
 * smallest block that fits. If no such block found, ask OS for space.
 *
 * size: number of bytes
 * 
 * return: pointer the new space
 */
void * ts_malloc_lock(size_t size) {
  my_malloc(size, &free_list, 1);
}


/* processBlock
 * -------------
 * allocate from a suitable sized block by chopping memory of size 'size'
 * and update related Header data.
 * 
 * start: the header of block to be chopped allocated
 * size: size of memory in request
 * 
 * return: the pointer to the memory following the chopped out block
 */
void * processBlock(Header * start, size_t size) {
  start->size -= size;
  start += start->size;
  start->size = size;
  return (void *)(start + 1);
}


/* malloc_sys
 * -------------
 * malloc uses this function to ask OS for more space to add to
 * free list. sbrk is called to increment the program break and return the
 * last program break, which is the pointer to new space. The minmum request
 * amount is defined by MIN_ALLOC, and the amount is the multiple of the
 * requested number of header.
 *
 * num_units: number of header-sized units
 * fl: double pointer to the entry node of the list
 * need: use of lock or not
 * 
 * return: new free list starting pointer
 */
Header * malloc_sys(size_t num_units, Header ** fl, int need) {
  if (need) {
    pthread_mutex_unlock(&free_list_mutex); 
  }
  pthread_mutex_lock(&sbrk_mutex); // sbrk lock
  char * ptr = sbrk(num_units * sizeof(Header));
  pthread_mutex_unlock(&sbrk_mutex); // sbrk unlock
  if (ptr == (char *) -1) {
    return NULL;
  }
  Header * header = (Header *)ptr;
  header->size = num_units;
  header->tid = pthread_self();
  if (need) {
    ts_sys_free_lock((void *)(header + 1));
  }
  else {
    insert_free_list((void *)(header + 1), fl);
  }
  return *fl;
}


/* ts_free_lock
 * ------------
 * Public thread-safe free operation for external free call.
 * Thread safety is achieved by using pthread mutex lock.
 *
 * ptr: space to be free and inserted into free list
 */
void ts_free_lock(void * ptr) {
  pthread_mutex_lock(&free_list_mutex); // locking free_list: insert & search again
  insert_free_list(ptr, &free_list);
  // free_list = insert_free_list(ptr);
  pthread_mutex_unlock(&free_list_mutex);
}


/* ts_sys_free_lock
 * -----------------
 * Private thread-safe free operation used after calling sbrk to insert
 * a new allocated block into the free list. The mutex will not be 
 * immediately unlocked until the block is allocated to user.
 * 
 * ptr: space to be free and inserted into free list
 */
void ts_sys_free_lock(void * ptr) {
  pthread_mutex_lock(&free_list_mutex);
  insert_free_list(ptr, &free_list);
}


/* insert_free_list
 * ----------------
 * Take a pointer to a block of memory and insert it to the free list
 * managed by my_malloc. Searching the free list arena to find the right
 * place according to its address. The list is address-sorted.
 * 
 * ptr: pointer to the block of memory to insert
 * fl: double pointer to the entry node of the list
 */
void insert_free_list(void * ptr, Header ** fl) {
  Header * toAdd = (Header *)ptr - 1;
  if (toAdd->tid != pthread_self()) {
    return;
  }
  Header * temp = *fl;
  while (1) {
    if ((toAdd > temp && toAdd < temp->next) // inside the arena
    || (temp >= temp->next && (toAdd > temp || toAdd < temp->next))) { // outside
      break;
    }
    temp = temp->next;
  }
  coalescing_blocks(toAdd, temp, fl);
}


/* coalescing_blocks
 * -----------------
 * While inserting block into the free list, coalescing with adjacent 
 * blocks if possible
 * 
 * toAdd: block to be inserted
 * block: block in the free list
 * fl: double pointer to the entry node of the list
 */
void coalescing_blocks(Header * toAdd, Header * block, Header ** fl) {
  if (toAdd + toAdd->size == block->next) { // upper coalescing
    toAdd->size += block->next->size;
    toAdd->next = block->next->next;
  }
  else {
    toAdd->next = block->next;
  }
  if (toAdd == block + block->size) { // lower coalescing
    block->size += toAdd->size;
    block->next = toAdd->next;
  }
  else {
    block->next = toAdd;
  }
  *fl = block;
}


/* ts_malloc_nolock
 * -----------------
 * Allocate memory with n bytes without using lock to achieve thread-safe malloc
 * 
 * n: in bytes of requested memory
 */
void * ts_malloc_nolock(size_t n) { 
  my_malloc(n, &tls_free_list, 0);
}


/* ts_free_nolock
 * -----------------
 * Return the memory from user to the free list in a lock-free thread safe way
 * 
 * ptr: pointer to memory to return to free list
 */
void ts_free_nolock(void * ptr) {
  insert_free_list(ptr, &tls_free_list);
}


/* initialize_alloc
 * ----------------
 * First time malloc setup. Setup the arena using a base header. Make TLS or
 * Lock version based on need_lock
 * 
 * prev: double pointer to prev node of current node
 * need_lock: indicate use of lock or not
 * fl: double pointer to list entry node
 */
void initialize_alloc(Header ** prev, int need_lock, Header ** fl) {
  if (need_lock) {
    *prev = *fl = &base;
    base.size = 0;
    base.next = *fl;
    base.tid = pthread_self();
  }
  else {
    *prev = *fl = &tls_base;
    tls_base.size = 0;
    tls_base.next = *fl;
    tls_base.tid = pthread_self();
  }
}


/* my_malloc
 * ---------
 * The actual memory allocator main logic. Search the free list for suitable
 * block and return a chopped block if found. Ask OS for more heap if no such
 * block found. If need lock, the acquire/free lock operations will be on.
 * 
 * n: size in bytes of requested memo
 * fl: double pointer to the entry node of the list
 * need_lock: indicate use of lock or not
 * 
 * return: pointer to the allocated memory
 */
void * my_malloc(size_t n, Header ** fl, int need_lock) {
  if (need_lock) {
    pthread_mutex_lock(&free_list_mutex); // lock access to free list 
  }
  size_t sunits = (n + sizeof(Header) - 1) / sizeof(Header) + 1;
  Header * curr = NULL, * prev = *fl;
  unsigned mindiff = UINT_MAX;
  if (prev == NULL) {
    initialize_alloc(&prev, need_lock, fl);
  }
  curr = prev->next;
  Header * best = NULL, * bestPrev = NULL;
  while (1) {
    if (curr->size >= sunits) {
      if (curr->size == sunits) {
        prev->next = curr->next;
        *fl = prev;
        if (need_lock) {
          pthread_mutex_unlock(&free_list_mutex); // success unlock
        }
        return (void *)(curr + 1);
      }
      else if (curr->size - sunits < mindiff) {
        mindiff = curr->size - sunits;
        best = curr;
        bestPrev = prev;
      }
    }
    if (curr == *fl) { // done one iteration
      if (best) {
        *fl = bestPrev;
        void * res = processBlock(best, sunits); 
        if (need_lock) {
          pthread_mutex_unlock(&free_list_mutex); // success unlock
        }
        return res;
      }
      else {
        if ((curr = malloc_sys(sunits, fl, need_lock)) == NULL) {
          if (need_lock) {
            pthread_mutex_unlock(&free_list_mutex);
          }
          return NULL;
        }
      }
    }
    prev = curr;
    curr = curr->next;
  }
}