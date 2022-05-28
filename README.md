# ts_malloc
c memory allocator implemented with linked list of memory blocks. It includes thread-safe implementations by using locks and thread local store.

## Implementation
Linkedlist(free list) is used to manage all memeory blocks. However, the last block in the list points to the first block of the list, which actually forms an areana. To search for suitable block, the arena will be iterated. Otherwise we will ask the OS for more memory by calling '''sbrk()'''. New memory from OS will be added to the list. Allocation calls to the allocator will find a block and chop the necessary memory from that block. The remain of the block will be kept in the arena. When two blocks are adjacent, they will be coalesced into one bigger block to improve utilization efficiency.

## strategy
We also explored Best-Fit and First-Fit strategy with tests attached. 
