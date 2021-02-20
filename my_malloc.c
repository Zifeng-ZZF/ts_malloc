#include "my_malloc.h"
#include <stdio.h>
#include <limits.h>
#include <pthread.h>


static Header * free_list = NULL; // entry of the free blocks cyclic ll
static Header base; // the very first Header


static pthread_mutex_t free_list_mutex = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t sbrk_mutex = PTHREAD_MUTEX_INITIALIZER;


/* prototypes */
Header * malloc_sys(size_t n);
void * processBlock(Header * start, size_t size);
void ts_sys_free_lock(void * ptr);
Header * insert_free_list(void * ptr);
void coalescing_blocks(Header * toAdd, Header * block);


/* ts_malloc_lock
 * ---------------
 * Best fit memory allocator. Iterate through the linked list, find the
 * smallest block that fits. If no such block found, ask OS for space.
 *
 * size: number of bytes
 * return: pointer the new space
 */
void * ts_malloc_lock(size_t size) {
  pthread_mutex_lock(&free_list_mutex); // lock access to free list 
  size_t sunits = (size + sizeof(Header) - 1) / sizeof(Header) + 1;
  Header * curr = NULL, * prev = free_list;
  unsigned mindiff = UINT_MAX;
  if (prev == NULL) { // first time malloc
    prev = free_list = &base;
    base.size = 0;
    base.next = free_list;
  }
  curr = prev->next;
  Header * best = NULL, * bestPrev = NULL;
  while (1) {
    if (curr->size >= sunits) {
      if (curr->size == sunits) {
        prev->next = curr->next;
        free_list = prev;
        pthread_mutex_unlock(&free_list_mutex); // success unlock
        return (void *)(curr + 1);
      }
      else if (curr->size - sunits < mindiff) {
        mindiff = curr->size - sunits;
        best = curr;
        bestPrev = prev;
      }
    }
    // printf("freelist=%p, curr=%p\n", free_list, curr);
    // done iterating
    if (curr == free_list) { 
      if (best) {
        free_list = bestPrev;
        void * res = processBlock(best, sunits); 
        pthread_mutex_unlock(&free_list_mutex); // success unlock
        return res;
      }
      else {
        pthread_mutex_unlock(&free_list_mutex); // fail unlock
        curr = malloc_sys(sunits);
        if (!curr) {
          pthread_mutex_unlock(&free_list_mutex);
          return NULL;
        }  // OS failed
      }
    }
    prev = curr;
    curr = curr->next;
  }
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
 * return: new free list starting pointer
 */
Header * malloc_sys(size_t num_units) {
  pthread_mutex_lock(&sbrk_mutex); // sbrk lock
  char * ptr = sbrk(num_units * sizeof(Header));
  pthread_mutex_unlock(&sbrk_mutex); // sbrk unlock
  if (ptr == (char *) -1) {
    return NULL;
  }
  Header * header = (Header *)ptr;
  header->size = num_units;
  ts_sys_free_lock((void *)(header + 1));
  return free_list;
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
  insert_free_list(ptr);
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
  insert_free_list(ptr);
}


/* insert_free_list
 * ----------------
 * Take a pointer to a block of memory and insert it to the free list
 * managed by my_malloc. Searching the free list arena to find the right
 * place according to its address. The list is address-sorted.
 * 
 * ptr: pointer to the block of memory to insert
 * 
 * return: the header to a managed memory block
 */
Header * insert_free_list(void * ptr) {
  Header * toAdd = (Header *)ptr - 1;
  Header * temp = free_list;
  while (1) {
    if ((toAdd > temp && toAdd < temp->next) // inside the arena
    || (temp >= temp->next && (toAdd > temp || toAdd < temp->next))) { // outside
      break;
    }
    temp = temp->next;
  }
  coalescing_blocks(toAdd, temp);
  return temp;
}


/* coalescing_blocks
 * -----------------
 * While inserting block into the free list, coalescing with adjacent 
 * blocks if possible
 * 
 * toAdd:
 * block:
 * 
 * return:
 */
void coalescing_blocks(Header * toAdd, Header * block) {
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
  free_list = block;
}
