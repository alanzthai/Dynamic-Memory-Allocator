#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "helper.h"
#include <errno.h>

// The sum of the pay loads of the currently allocated blocks (according to textbook)
size_t maximum_aggregate_payload = 0;
size_t aggregate_payload = 0;

void *sf_malloc(size_t size) {
    // To be implemented.
    size_t adjusted_size; // Size to be searched
    // size_t extended_size; // Size after being extended to find a matching fit
    sf_block *block_pointer; // Pointer to a block
    sf_block *epilogue;
    if (size == 0)
        return NULL;
    // Set up heap and initialize other important variables if it hasn't been
    if(sf_mem_start() == sf_mem_end()){
        sf_mem_grow();
        set_free_list_pointers();
        // Set up the prologue and initial epilogue
        // Prologue
        sf_block *prologue = (sf_block*) (sf_mem_start());
        prologue -> header = (32 + THIS_BLOCK_ALLOCATED) ^ MAGIC;
        // First actual block in the heap
        sf_block *starting_block = (sf_block*) (sf_mem_start() + 32);
        starting_block -> header = (PAGE_SZ - 48 + PREV_BLOCK_ALLOCATED) ^ MAGIC;
        // Epilogue (Block size set to 0)
        epilogue = (sf_block *) (sf_mem_end() - 16);
        epilogue -> header = THIS_BLOCK_ALLOCATED ^ MAGIC;
        // Create a footer to match the header
        sf_footer *starting_block_footer = (sf_footer *) epilogue;
        *starting_block_footer = starting_block -> header;
        add_free_list_block(starting_block, PAGE_SZ - 48);
    }
    // Set epilogue for cases where malloc is called and heap isn't initialized
    epilogue = (sf_block *) (sf_mem_end() - 16);
    epilogue -> header = THIS_BLOCK_ALLOCATED ^ MAGIC;
    // Add padding to requested size and round up to nearest multiple of 16
    adjusted_size = size + 8;
    int remainder = adjusted_size % 16;
    if(remainder != 0){
        adjusted_size = adjusted_size + (16 - remainder);
    }
    if (adjusted_size <= 32){
        // Block size will be 32 if the request is less than 32
        adjusted_size = 32;
    }
    // If it can fit in a quicklist
    if (adjusted_size <= 176){
        int index = (adjusted_size - 32) / 16;
        // If there is a block to use in the quicklist
        if ((sf_quick_lists + index) -> length > 0){
            sf_block *block_pointer = (sf_quick_lists + index) -> first;
            // Remove block
            (sf_quick_lists + index) -> first = block_pointer -> body.links.next;
            (sf_quick_lists + index) -> length--;
            // Set quicklist bit
            block_pointer -> header = (block_pointer -> header) & ~IN_QUICK_LIST;
            aggregate_payload += get_block_size(block_pointer) - 8;
            if (aggregate_payload > maximum_aggregate_payload){
                maximum_aggregate_payload = aggregate_payload;
            }
            return block_pointer -> body.payload;
        }
    }
    while(epilogue != NULL && sf_errno != ENOMEM){
        // Search free list for appropriate size
        if((block_pointer = search_free_list(adjusted_size)) != NULL){
            place(block_pointer, adjusted_size);
            // sf_show_heap();
            aggregate_payload += get_block_size(block_pointer) - 8;
            if (aggregate_payload > maximum_aggregate_payload){
                maximum_aggregate_payload = aggregate_payload;
            }
            return block_pointer -> body.payload;
        }
        // No fit was found so grow the heap and attempt to find a fit, save value of new epilogue for future iterations
        if ((epilogue = extend_heap(epilogue)) == NULL){
            // Don't need to set ENOMEM because extend_heap does it
            return NULL;
        }
        // sf_show_heap();
    }

    sf_errno = ENOMEM;
    return NULL;
}

void sf_free(void *pp) {
    // To be implemented.
    // First verify pointer
    // The pointer is NULL
    if (pp == NULL){
        abort();
    }
    // The pointer is not 16-byte aligned.
    if (((uintptr_t)pp & 0xF) != 0){
        abort();
    }
    size_t block_size = get_block_size(pp - 16);
    // The block size is less than the minimum block size of 32
    if (block_size < 32){
        abort();
    }
    // The block size is not a multiple of 16
    if (block_size % 16 != 0){
        abort();
    }
    // The header of the block is before the start of the first block of the heap
    if (pp <  sf_mem_start() + 32){
        abort();
    }
    // The footer of the block is after the end of the last block in the heap
    if (pp >  sf_mem_end() - 16){
        abort();
    }
    // Cast pointer
    sf_block *block_pointer = pp - 16;
    int allocated = ((block_pointer -> header) ^ MAGIC) & THIS_BLOCK_ALLOCATED;
    // The allocated bit in the header is 0
    if (allocated == 0){
        abort();
    }
    int quicklist = ((block_pointer -> header) ^ MAGIC) & IN_QUICK_LIST;
    // The in quick list bit in the header is 1
    if (quicklist == 1){
        abort();
    }
    int prev_allocated = ((block_pointer -> header) ^ MAGIC) & PREV_BLOCK_ALLOCATED;
    if (prev_allocated == 0){
        // The prev_alloc field in the header is 0, indicating that the previous block is free, but the alloc field of the previous block footer is not 0.
        int prev_block_allocated = ((block_pointer -> prev_footer) ^ MAGIC) & THIS_BLOCK_ALLOCATED;
        if (prev_block_allocated != 0){
            abort();
        }
    }
    // Valid pointer so we can calculate the payload and free
    aggregate_payload -= block_size - 8;
    if (aggregate_payload > maximum_aggregate_payload){
        maximum_aggregate_payload = aggregate_payload;
    }
    // If blocksize is less than or equal to 176 (Max blocksize in a quicklist (32 + 16 * 9)) try to put it into a quicklist
    if (block_size <= 176){
        // Quicklist index
        int index = (block_size - 32) / 16;
        // If quicklist is full, flush
        if ((sf_quick_lists + index) -> length == QUICK_LIST_MAX){
            flush(index);
        }
        // Insert into quicklist
        block_pointer -> body.links.next = (sf_quick_lists + index) -> first;
        (sf_quick_lists + index) -> first = block_pointer;
        (sf_quick_lists + index) -> length++;
        // Set quicklist bit
        block_pointer -> header = (((block_pointer -> header) ^ MAGIC) | IN_QUICK_LIST) ^ MAGIC;
    }
    // Cannot put in quicklist
    else{
        // Set allocated bit
        block_pointer -> header = ((block_pointer -> header ^ MAGIC) & (~THIS_BLOCK_ALLOCATED)) ^ MAGIC;
        create_footer(block_pointer);
        add_free_list_block(block_pointer, block_size);
        // Change prev allocated bit for the ORIGINAL block in front of it if there is one
        sf_block *block_in_front = pp - 16 + block_size;
        block_in_front -> header = ((block_in_front -> header ^ MAGIC) & (~PREV_BLOCK_ALLOCATED)) ^ MAGIC;
        // If the block in front is free, we need to coalesce and change the footer
        if (!(((block_in_front -> header) ^ MAGIC) & THIS_BLOCK_ALLOCATED)){
            create_footer(block_in_front);
            block_pointer = coalesce(block_in_front);
        }
        // If the block before it is also free, we need to coalece it as well
        if (!prev_allocated){
            block_pointer = coalesce(block_pointer);
        }

    }

}

void *sf_realloc(void *pp, size_t rsize) {
    // To be implemented.
        // First verify pointer
    // The pointer is NULL
    if (pp == NULL){
        sf_errno = EINVAL;
        return NULL;
    }
    // The pointer is not 16-byte aligned.
    if (((uintptr_t)pp & 0xF) != 0){
        sf_errno = EINVAL;
        return NULL;
    }
    size_t block_size = get_block_size(pp - 16);
    // The block size is less than the minimum block size of 32
    if (block_size < 32){
        sf_errno = EINVAL;
        return NULL;
    }
    // The block size is not a multiple of 16
    if (block_size % 16 != 0){
        sf_errno = EINVAL;
        return NULL;
    }
    // The header of the block is before the start of the first block of the heap
    if (pp <  sf_mem_start() + 32){
        sf_errno = EINVAL;
        return NULL;
    }
    // The footer of the block is after the end of the last block in the heap
    if (pp >  sf_mem_end() - 16){
        sf_errno = EINVAL;
        return NULL;
    }
    // Cast pointer
    sf_block *block_pointer = pp - 16;
    int allocated = ((block_pointer -> header) ^ MAGIC) & THIS_BLOCK_ALLOCATED;
    // The allocated bit in the header is 0
    if (allocated == 0){
        sf_errno = EINVAL;
        return NULL;
    }
    int quicklist = ((block_pointer -> header) ^ MAGIC) & IN_QUICK_LIST;
    // The in quick list bit in the header is 1
    if (quicklist == 1){
        sf_errno = EINVAL;
        return NULL;
    }
    int prev_allocated = ((block_pointer -> header) ^ MAGIC) & PREV_BLOCK_ALLOCATED;
    if (prev_allocated == 0){
        // The prev_alloc field in the header is 0, indicating that the previous block is free, but the alloc field of the previous block footer is not 0.
        int prev_block_allocated = ((block_pointer -> prev_footer) ^ MAGIC) & THIS_BLOCK_ALLOCATED;
        if (prev_block_allocated != 0){
            sf_errno = EINVAL;
            return NULL;
        }
    }
    // Size is 0
    if (rsize == 0){
        sf_free(pp);
        return NULL;
    }

    size_t adjusted_size;

    // Add padding to requested size and round up to nearest multiple of 16
    adjusted_size = rsize + 8;
    int remainder = adjusted_size % 16;
    if(remainder != 0){
        adjusted_size = adjusted_size + (16 - remainder);
    }
    if (adjusted_size <= 32){
        // Block size will be 32 if the request is less than 32
        adjusted_size = 32;
    }
    // If the request is larger than the current block size (the blocksize - 8 (the header)) realloc to a larger block
    void *malloc_block_pointer;
    if (adjusted_size > block_size - 8){
        if ((malloc_block_pointer = sf_malloc(rsize)) == NULL){
            // malloc should've set ENONEM
            return NULL;
        }
        // Get the original start point of the malloc'd block to get the header from which to get the blocksize
        sf_block *malloc_block_start = malloc_block_pointer - 16;
        // Get payload size by bitshifting and ANDing with 0xF
        memcpy(&(malloc_block_pointer), &(block_pointer -> body.payload), (((malloc_block_start -> header) ^ MAGIC) << 32 & 0xF));
        sf_free(pp);
        return malloc_block_pointer;
    }
    else{
        size_t remainder = block_size - adjusted_size;
        // Request cannot be made
        if(remainder < 0){
            sf_errno = ENOMEM;
            return NULL;
        }
        // Split block
        if(remainder >= 32){
            // Address of where the free block will start
            sf_block *free_block_start = ((void*) (block_pointer) + 8);
            // XOR headers
            free_block_start -> header = (block_size - adjusted_size) ^ MAGIC;
            block_pointer -> header = (adjusted_size) ^ MAGIC;
            // Set allocacted bit
            block_pointer -> header = (((block_pointer -> header) ^ MAGIC) | THIS_BLOCK_ALLOCATED) ^ MAGIC;
            // Set prev allocated bit
            block_pointer -> header = (((block_pointer -> header) ^ MAGIC) | PREV_BLOCK_ALLOCATED) ^ MAGIC;
            // Set a pointer to connect the blocks together
            sf_block *connect_block = ((void*) (block_pointer) + get_block_size(block_pointer));
            connect_block -> header = (((free_block_start -> header) ^ MAGIC) ^ MAGIC);
            // Set its prev allocated block since the block before it is allocated
            connect_block -> header = (((connect_block -> header) ^ MAGIC) | PREV_BLOCK_ALLOCATED) ^ MAGIC;
            // Create footers
            create_footer(free_block_start);
            create_footer(connect_block);
            create_footer(block_pointer);
            add_free_list_block(connect_block, block_size - adjusted_size);
            // Handle coalesce case (if there is a free block after the newly split freed block)
            sf_block *coalesce_block = ((void *)connect_block) + get_block_size(connect_block);
            // If there is no block after the freed block (End of heap/epilogue)
            if (coalesce_block == (sf_mem_end() - 16)){
                return connect_block -> body.payload;
            }
            // Coalesce
            // Set prev allocated bit
            coalesce_block -> header = (((coalesce_block -> header) ^ MAGIC) & ~PREV_BLOCK_ALLOCATED) ^ MAGIC;
            create_footer(coalesce_block);
            connect_block = coalesce(coalesce_block);
            return block_pointer -> body.payload;
        }
        // Does not need to be split
        else{
            block_pointer ->  header = (((block_pointer -> header) ^ MAGIC) | THIS_BLOCK_ALLOCATED) ^ MAGIC;
            create_footer(block_pointer);
            return block_pointer -> body.payload;
        }

    }
    sf_errno = ENOMEM;
    return NULL;
}

// Ignore
double sf_fragmentation() {
    // To be implemented.
    abort();
}

// You should compute the utilization under the assumption that the payload occupies the entire block, except for the header.
double sf_utilization() {
    // To be implemented.
    // Heap hasn't been initialized
    if (sf_mem_end() == sf_mem_start()){
        return 0.0;
    }
    else{
    // Cast to match return type
    double heap_size = sf_mem_end() - sf_mem_start();
    //The peak memory utilization at a given time, is the ratio of the maximum aggregate payload up to that time, divided by the current heap size.
    return maximum_aggregate_payload / heap_size;
    }
}

// Searches for a free block and returns a pointer, if it does not find one, returns NULL/
void *search_free_list(size_t adjusted_size){
    // Start search from first fit policy
    for(int i = get_size_class(adjusted_size); i < NUM_FREE_LISTS; i++ ){
        // Casting the pointers
        sf_block *sentinel_node = (sf_free_list_heads + i); // Sentinel node to ensure loop ends once pointer comes back around
        sf_block *free_list_block = sentinel_node -> body.links.next; // Current block we are looking at (Start search at next as sentinel is a dummy node)
        while (free_list_block != sentinel_node){
            // Find blocksize
            size_t free_block_size = get_block_size(free_list_block);
            // If the block is not allocated and the adjusted size will fit, return the pointer
            if (!is_allocated(free_list_block) && (adjusted_size <= free_block_size)){
                return free_list_block;
            }
            else{
                // If it is not a match, get the next block
                free_list_block = free_list_block -> body.links.next;
            }
        }
    }
    return NULL;
}

// Returns the index of the proper size class
int get_size_class(size_t size){
    // Belongs in first index
    if (size <= 32){
        return 0;
    }
    if (size > 32 && size <= 64)
        return 1;
    if (size > 64 && size <= 128)
        return 2;
    if (size > 128 && size <= 256)
        return 3;
    if (size > 256 && size <= 512)
        return 4;
    if (size > 512 && size <= 1024)
        return 5;
    if (size > 1024 && size <= 2048)
        return 6;
    if (size > 2048 && size <= 4096)
        return 7;
    if (size > 4096 && size <= 8192)
        return 8;
    return 9;
}

size_t get_block_size(sf_block *block){
    size_t header = block -> header;
    return (header ^ MAGIC) & 0xFFFFFFF0;
}

// Checks if block is allocated
int is_allocated(sf_block *block){
    size_t header = block -> header;
    int value = (header ^ MAGIC) & THIS_BLOCK_ALLOCATED;
    if (value != 0){
        return 1;
    }
    return 0;
}

// Checks if prev block is allocated
int prev_allocated(sf_block *block){
    size_t header = block -> header;
    int value = (header ^ MAGIC) & PREV_BLOCK_ALLOCATED;
    if (value != 0){
        return 1;
    }
    return 0;
}

// Checks if block is in quick list
int in_quicklist(sf_block *block){
    size_t header = block -> header;
    int value = (header ^ MAGIC) & IN_QUICK_LIST;
    if (value != 0){
        return 1;
    }
    return 0;
}

// Removes block from free list
void remove_free_list(sf_block *block){
    sf_block *prev_block = block->body.links.prev;
    sf_block *next_block = block->body.links.next;
    // Connect prefv and next blocks
    if (prev_block != NULL) {
        prev_block->body.links.next = next_block;
    }
    if (next_block != NULL) {
        next_block->body.links.prev = prev_block;
    }
}

// Place the block at the start of free block
void place(sf_block *block_pointer, size_t adjusted_size){
    size_t block_size = get_block_size(block_pointer);
    // Find the difference to see if the block needs to be split
    size_t remainder = block_size - adjusted_size;
    remove_free_list(block_pointer);
    // Split
    if(remainder >= 32){
        // Address of where the free block will start
        sf_block *free_block_start = ((void*) (block_pointer) + 8);
        // XOR headers
        free_block_start -> header = (block_size - adjusted_size) ^ MAGIC;
        block_pointer -> header = (adjusted_size) ^ MAGIC;
        // Set allocacted bit
        block_pointer -> header = (((block_pointer -> header) ^ MAGIC) | THIS_BLOCK_ALLOCATED) ^ MAGIC;
        // Set prev allocated bit
        block_pointer -> header = (((block_pointer -> header) ^ MAGIC) | PREV_BLOCK_ALLOCATED) ^ MAGIC;
        // Set a pointer to connect the blocks together
        sf_block *connect_block = ((void*) (block_pointer) + get_block_size(block_pointer));
        connect_block -> header = (((free_block_start -> header) ^ MAGIC) ^ MAGIC);
        // Set its prev allocated block since the block before it is allocated
        connect_block -> header = (((connect_block -> header) ^ MAGIC) | PREV_BLOCK_ALLOCATED) ^ MAGIC;
        // Create footers
        create_footer(free_block_start);
        create_footer(connect_block);
        create_footer(block_pointer);
        add_free_list_block(connect_block, block_size - adjusted_size);

        // sf_show_heap();

    }
    // Does not need to be split
    else{
        block_pointer ->  header = (((block_pointer -> header) ^ MAGIC) | THIS_BLOCK_ALLOCATED) ^ MAGIC;
        create_footer(block_pointer);
    }

}

void create_footer(sf_block *block){
    size_t block_size = get_block_size(block);
    sf_block *footer_pointer = ((void *) block) + block_size;
    footer_pointer -> prev_footer = ((block -> header) ^ MAGIC) ^ MAGIC;
}

// Maintain free list heads as doubly linked list
void set_free_list_pointers(){
    for (int i = 0; i < NUM_FREE_LISTS; i++){
       (sf_free_list_heads + i) -> body.links.next = sf_free_list_heads + i;
       (sf_free_list_heads + i) -> body.links.prev = sf_free_list_heads + i;
    }
}

void add_free_list_block(sf_block *block, size_t size){
    // Pointer to the next block
    sf_block *next = (sf_free_list_heads + get_size_class(size)) -> body.links.next;
    // Switching pointers to add node to sentinel node
    (sf_free_list_heads + get_size_class(size)) -> body.links.next = block;
    block -> body.links.prev = (sf_free_list_heads + get_size_class(size));
    // Set the new node pointers so it is in between
    block -> body.links.next = next;
    next -> body.links.prev = block;

}

// Grow the heap and set pointers correctly
sf_block *extend_heap(sf_block *epilogue){
    if (sf_mem_grow() == NULL){
        sf_errno = ENOMEM;
        return NULL;
    }
    // Start of new block we get from mem_grow
    sf_block *new_block_start = epilogue;
    // Fix allocated and quicklist bits
    new_block_start -> header = (((new_block_start -> header) ^ MAGIC) & ~THIS_BLOCK_ALLOCATED) ^ MAGIC;
    new_block_start -> header = (((new_block_start -> header) ^ MAGIC) & ~IN_QUICK_LIST) ^ MAGIC;
    // Set the header and footer of the block
    new_block_start -> header = (get_block_size(new_block_start) | PAGE_SZ) ^ MAGIC;
    create_footer(new_block_start);
    // Adjust epilogue pointer
    sf_block *new_epilogue = (sf_block*) (sf_mem_end() - 16);
    new_epilogue -> header = THIS_BLOCK_ALLOCATED ^ MAGIC;
    sf_block *combined_block = coalesce(new_block_start);
    if (combined_block != NULL){
        return new_epilogue;
    }
    sf_errno = ENOMEM;
    return NULL;
}

// Combine two free blocks
sf_block *coalesce(sf_block *block){
    if (!prev_allocated(block)){
        size_t prev_block_size = ((block -> prev_footer) ^ MAGIC) &  0xFFFFFFF0;
        // Size of the combined free block
        size_t combined_block_size = (((block -> header) ^ MAGIC) &  0xFFFFFFF0) + prev_block_size;
        // Set header
        sf_block *combined_block = ((void *) block) - prev_block_size;
        combined_block -> header = ((((combined_block -> header) ^ MAGIC) & ~0xFFFFFFF0) | combined_block_size) ^ MAGIC;
        combined_block -> header = (((combined_block -> header) ^ MAGIC) & ~THIS_BLOCK_ALLOCATED) ^ MAGIC;
        // printf("%zu\n", get_block_size(combined_block));
        create_footer(combined_block);
        // Remove the block so it can be inserted with the correct information
        remove_free_list(combined_block);
        // Remove the original block as well
        remove_free_list(block);
        add_free_list_block(combined_block, combined_block_size);
        return combined_block;
    }
    // Block is allocated, cannot coalsece
    return NULL;

}

void flush(int index){
    while ((sf_quick_lists + index) -> first){
        sf_block *block_pointer = (sf_quick_lists + index) -> first;
        size_t block_size = get_block_size(block_pointer);
        // Cast pointer
        sf_block *block_in_front = ((void*)block_pointer) + block_size;
        // Set prev allocated bit
        block_in_front -> header = (((block_in_front -> header) ^ MAGIC) & ~PREV_BLOCK_ALLOCATED) ^ MAGIC;
        // Check if the block in front is allocated, if it is free it needs a footer
        if (!(((block_pointer -> header) ^ MAGIC) & THIS_BLOCK_ALLOCATED)){
            create_footer(block_in_front);
        }
        (sf_quick_lists + index) -> first = block_pointer -> body.links.next;
        // Set quicklist bit
        block_pointer -> header = (((block_pointer -> header) ^ MAGIC) & ~IN_QUICK_LIST) ^ MAGIC;
        // Set allocated bit
        block_pointer -> header = (((block_pointer -> header) ^ MAGIC) & ~THIS_BLOCK_ALLOCATED) ^ MAGIC;
        // printf("%lu\n", block_size);
        create_footer(block_pointer);
        // sf_show_block(block_pointer);
        add_free_list_block(block_pointer, block_size);
        // Cast to access the following block
        sf_block *coalesce_block = ((void *) block_pointer) + block_size;
        // If the block is not allocated coalesce
        if (!(((coalesce_block -> header) ^ MAGIC) & THIS_BLOCK_ALLOCATED)){
            block_pointer = coalesce(coalesce_block);
        }
        // Check if there is a free block before this block
        block_pointer = coalesce(block_pointer);
    }
    (sf_quick_lists + index) -> length = 0;
}