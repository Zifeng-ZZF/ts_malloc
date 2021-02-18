#include "my_malloc.h"
#include <stdio.h>
#include <limits.h>

static Header * free_list = NULL; // entry of the free blocks cyclic ll
static Header base; // the very first Header

Header * malloc_sys(size_t n, void (* freef)(void *));


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
Header * malloc_sys(size_t num_units, void (* freef)(void *)) {
  if (num_units < MIN_ALLOC) { // to reduce call to sbrk
    unsigned num = MIN_ALLOC / num_units;
    num_units *= num;
  }
  char * ptr = sbrk(num_units * sizeof(Header));
  if (ptr == (char *) -1) {
    return NULL;
  }
  Header * header = (Header *)ptr;
  header->size = num_units;
  freef((void *)(header + 1));
  return free_list;
}


/* ts_malloc_lock
 * ----------
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
  while (1) { // iterate through the linked list of free blocks
    if (curr->size >= sunits) {
      if (curr->size == sunits) { // exactly the same
        prev->next = curr->next;
        free_list = prev;
        return (void *)(curr + 1);
      }
      else if (curr->size - sunits < mindiff) { // update the most fit block
        mindiff = curr->size - sunits;
        best = curr;
        bestPrev = prev;
      }
    }
    if (curr == free_list) { // done iterating
      if (best) { // if a suitable block found, chop from its tail
        best->size -= sunits;
        best += best->size;
        best->size = sunits;
        free_list = bestPrev;
        return (void *)(best + 1);
      }
      if ((curr = malloc_sys(sunits, bf_free)) == NULL) {
        return NULL;
      }
    }
    prev = curr;
    curr = curr->next;
  }
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
  // search for position in the list for the new blank block
  Header * toAdd = (Header *)ptr - 1;
  Header * temp = free_list;
  while (1) { // find the interval where the new block resides
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
