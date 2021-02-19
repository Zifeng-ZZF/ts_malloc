#include "my_malloc.h"
#include <stdio.h>
#include <limits.h>
#include <pthread.h>

static Header * free_list = NULL; // entry of the free blocks cyclic ll
static Header base; // the very first Header
static pthread_mutex_t free_list_mutex = PTHREAD_MUTEX_INITIALIZER; // static mutex lock
static pthread_mutex_t sbrk_mutex = PTHREAD_MUTEX_INITIALIZER;

Header * malloc_sys(size_t n);

/* ts_malloc_lock
 * ---------------
 * Best fit memory allocator. Iterate through the linked list, find the
 * smallest block that fits. If no such block found, ask OS for space.
 *
 * size: number of bytes
 * return: pointer the new space
 */
void * ts_malloc_lock(size_t size) {
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
  pthread_mutex_lock(&free_list_mutex); // lock access to free list
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
    // done iterating
    if (curr == free_list) { 
      if (best) {
        free_list = bestPrev;
        pthread_mutex_unlock(&free_list_mutex); // success unlock
        return processBlock(best, sunits); 
      }
      else {
          pthread_mutex_unlock(&free_list_mutex); // fail unlock
          curr = malloc_sys(sunits);
          if (!curr) return NULL; // OS failed
          pthread_mutex_lock(&free_list_mutex); // lock free list to search again
      }
    }
    prev = curr;
    curr = curr->next;
  }
}

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
  if (num_units < MIN_ALLOC) {
    unsigned num = MIN_ALLOC / num_units;
    num_units *= num;
  }
  pthread_mutex_lock(&sbrk_mutex); // sbrk lock
  char * ptr = sbrk(num_units * sizeof(Header));
  pthread_mutex_unlock(&sbrk_mutex); // sbrk unlock
  if (ptr == (char *) -1) {
    return NULL;
  }
  Header * header = (Header *)ptr;
  header->size = num_units;
  pthread_mutex_lock(&free_list_mutex); // locking free_list: insert & search again
  ts_free_lock((void *)(header + 1));
  return free_list;
}


/* ts_free_lock
 * --------
 * actual implementation of free operation. Since the free list is address sorted
 * cyclic linked list, this functions find the position of the free space and do
 * necessary coalescing with adjacent blocks.
 *
 * ptr: space to be free and inserted into free list
 */
void ts_free_lock(void * ptr) {
  Header * toAdd = (Header *)ptr - 1;
  Header * temp = free_list;
  while (1) {
    if ((toAdd > temp && toAdd < temp->next) // inside the arena
    || (temp >= temp->next && (toAdd > temp || toAdd < temp->next))) { // outside
      break;
    }
    temp = temp->next;
  }
  if (toAdd + toAdd->size == temp->next) { // upper coalescing
    toAdd->size += temp->next->size;
    toAdd->next = temp->next->next;
  }
  else {
    toAdd->next = temp->next;
  }
  if (toAdd == temp + temp->size) { // lower coalescing
    temp->size += toAdd->size;
    temp->next = toAdd->next;
  }
  else {
    temp->next = toAdd;
  }
  free_list = temp;
}
