/*
 * mm_explicit.c - ...
 *
 * In this approach, a block is allocated by ...
 *
 * This code is faster than ...
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_checkheap() mm_checkheap(1)
#else
#define dbg_printf(...)
#define dbg_checkheap()
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define WORD_1X     4                /* single word size (bytes) */
#define WORD_2X     8                /* double word size (bytes) */
#define WORD_4X    16                /*  quad  word size (bytes) */
#define PAGE_SIZE  (1<<12) /* extend heap by this amount (bytes) */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT WORD_2X

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* PACK a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* GET and PUT a word at address p */
#define GET_WORD(p)        (*(unsigned int *)(p)         )
#define PUT_WORD(p, word)  (*(unsigned int *)(p) = (word))

/* GET the SIZE and ALLOCated fields from address p */
#define GET_SIZE(p)   (GET_WORD(p) & ~0x7)
#define GET_ALOC(p)  (GET_WORD(p) &  0x1)

/* Given block (ptr), get the address of its HEADER and FOOTER */
#define HEADER(block)  ((char *)(block) - WORD_1X)
#define FOOTER(block)  ((char *)(block) + GET_SIZE(HEADER(block)) - WORD_2X)

#define IS_ALOC(block)    (GET_ALOC(HEADER(block)))
#define IS_FREE(block)    (!IS_ALOC(block))

/* block to its NEXT and PREVious blocks in Virtual Memory */
#define VM_NEXT(block) ((char *)(block) + GET_SIZE(HEADER(block)))
#define VM_PREV(block) ((char *)(block) - GET_SIZE(((char *)(block) - WORD_2X)))

/* block to its NEXT and PREVious blocks in Free List */
#define FL_NEXT(block) (*((void **)(block)    ))
#define FL_PREV(block) (*((void **)(block) + 1))

/* Global variables */
static char *first_free_block = NULL; /* Pointer to the first free block */
int free_block_count = 0;
int aloc_block_count = 0;

/* Private methods */

/*
 * Insert the new block at the head of the list.
 */
static void put_before_head(void *block)
{
    assert(IS_FREE(block));
    FL_NEXT(block) = first_free_block;
    if (first_free_block) {
        assert(FL_PREV(first_free_block) == NULL);
        FL_PREV(first_free_block) = block;
    }
    first_free_block = block;
    FL_PREV(first_free_block) = NULL;
}

static void *coalesce(void *block)
{
    dbg_printf("coalesce(%p) is ready to start.\n", block);

    int prev_free = IS_FREE(VM_PREV(block));
    int next_free = IS_FREE(VM_NEXT(block));
    size_t size = GET_SIZE(HEADER(block));

    if (prev_free && next_free) {            /* Case 4 */
        --free_block_count;
        --free_block_count;
        size += GET_SIZE(HEADER(VM_PREV(block)))
              + GET_SIZE(FOOTER(VM_NEXT(block)));
    }
    else if (prev_free && !next_free) {      /* Case 3 */
        --free_block_count;
    }
    else if (!prev_free && next_free) {      /* Case 2 */
        --free_block_count;
    }
    else {                                   /* Case 1 */
    }

    put_before_head(block);
    dbg_printf("coalesce() -> %p is ready to exit.\n", block);
    return block;
}

static void *extend_heap(size_t size)
{
    dbg_printf("extend_heap(%ld=0x%lx) is ready to start.\n", size, size);

    char *block;

    block = mem_sbrk(size);
    if (block == (void *)-1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT_WORD(HEADER(block), PACK(size, 0));         /* Free block header */
    PUT_WORD(FOOTER(block), PACK(size, 0));         /* Free block footer */
    PUT_WORD(HEADER(VM_NEXT(block)), PACK(0, 1)); /* New epilogue header */

    ++free_block_count;
    block = coalesce(block);

    dbg_printf("extend_heap(%ld=0x%lx) -> %p is ready to exit.\n", size, size, block);
    return block;
}

static void *find_fit(size_t alloc_size)
{
    dbg_printf("find_fit(%ld=0x%lx) is ready to start.\n", alloc_size, alloc_size);

    char *block = first_free_block;
    size_t size; /* size of current block */

    while (block && (size = GET_SIZE(HEADER(block))) < alloc_size)
        block = FL_NEXT(block);

    dbg_printf("find_fit(%ld=0x%lx) -> %p is ready to exit.\n", alloc_size, alloc_size, block);
    return block;
}

static void place(void *block, size_t alloc_size)
{
    dbg_printf("place(%p, %ld=0x%lx) is ready to start.\n", block, alloc_size, alloc_size);

    assert(IS_FREE(block));
    char *fl_prev = FL_PREV(block);
    char *fl_next = FL_NEXT(block);
    size_t block_size = GET_SIZE(HEADER(block));
    int split = (block_size > alloc_size + WORD_4X);

    if (split) {
        /* The remaining of current block can hold a min-sized block. */
        PUT_WORD(HEADER(block), PACK(alloc_size, 1));
        PUT_WORD(FOOTER(block), PACK(alloc_size, 1));
        block = VM_NEXT(block);
        block_size -= alloc_size;
    }
    else {
        --free_block_count;
    }
    ++aloc_block_count;
    PUT_WORD(HEADER(block), PACK(block_size, !split));
    PUT_WORD(FOOTER(block), PACK(block_size, !split));

    /* fix links */
    if (fl_prev) {
        FL_NEXT(fl_prev) = fl_next;
    }
    else {
        first_free_block = fl_next;
        if (fl_next)
            FL_PREV(fl_next) = NULL;
    }
    if (fl_next)
        FL_NEXT(fl_next) = fl_prev;
    if (split)
        put_before_head(block);

    dbg_checkheap();
    dbg_printf("place(%p) is ready to exit.\n", block - split * alloc_size);
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void)
{
    dbg_printf("mm_init() is ready to start.\n");

    char* block;
    /* Create the initial empty heap */
    mem_reset_brk();
    assert(mem_heap_lo() == mem_heap_hi() + 1);

    /* Extend the empty heap with a free block of PAGE_SIZE bytes */
    assert(WORD_1X <= ALIGNMENT && ALIGNMENT <= WORD_4X);
    block = mem_sbrk(WORD_4X);
    assert(block == mem_heap_lo());
    PUT_WORD(block, 0);                                /* Padding on head */
    PUT_WORD(block + (WORD_1X), PACK(WORD_2X, 1));     /* Prologue header */
    PUT_WORD(block + (WORD_2X), PACK(WORD_2X, 1));     /* Prologue footer */
    PUT_WORD(block + (WORD_2X + WORD_1X), PACK(0, 1)); /* Epilogue header */
    free_block_count = 0;
    aloc_block_count = 2;

    block = extend_heap(PAGE_SIZE - WORD_4X);
    if (block == NULL)
        return -1;
    assert(block == first_free_block);
    assert(block == mem_heap_lo() + WORD_4X);
    dbg_checkheap();

    dbg_printf("mm_init() is ready to exit.\n\n");
    return 0;
}

/*
 * malloc - Allocate a block by ...
 *      ...
 */
void *malloc(size_t size)
{
    dbg_printf("malloc(%ld=0x%lx) is ready to start.\n", size, size);

    void *block;

    if (first_free_block == NULL)
        mm_init();

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    size = ALIGN(MAX(size, WORD_4X)/* payload */ + WORD_2X/* overhead */);

    /* Search the free list for a fit */
    if ((block = find_fit(size)) == NULL) {
        /* No fit found. Get more memory. */
        block = extend_heap(MAX(size, PAGE_SIZE));
        if (block == NULL)  
            return NULL;
    }
    place(block, size);
    dbg_printf("malloc(%ld=0x%lx (aligned)) -> %p is ready to exit.\n\n", size, size, block);
    return block;
}

/*
 * free -  ...
 *      ...
 */
void free(void *block)
{
    dbg_printf("free(%p) is ready to start.\n", block);

    if (block == NULL) 
        return;

    assert(IS_ALOC(block));
    size_t size = GET_SIZE(HEADER(block));
    PUT_WORD(HEADER(block), PACK(size, 0));
    PUT_WORD(FOOTER(block), PACK(size, 0));
    --aloc_block_count;
    ++free_block_count;
    block = coalesce(block);

    dbg_printf("free(%p) is ready to exit.\n\n", block);
}

/*
 * realloc - Change the size of the block by ...
 *      ...
 */
void *realloc(void *old_block, size_t size)
{
    size_t old_size;
    void *new_block;

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(old_block);
        return NULL;
    }

    /* If old_block is NULL, then this is just malloc. */
    if (old_block == NULL) {
        return malloc(size);
    }

    new_block = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!new_block) {
        return NULL;
    }

    /* Copy the old data. */
    old_size = GET_SIZE(HEADER(old_block));
    if (size < old_size)
        old_size = size;
    memcpy(new_block, old_block, old_size);

    /* Free the old block. */
    free(old_block);

    return new_block;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size)
{
    size_t bytes = nmemb * size;
    void *block;

    block = malloc(bytes);
    memset(block, 0, bytes);

    return block;
}

/*
 * mm_checkheap - ...
 *      ...
 */
void mm_checkheap(int verbose)
{

    int n_free_blocks = 0, n_aloc_blocks = 0;
    char *block = mem_heap_lo() + ALIGNMENT;
    char *fl_prev, *fl_next;

    while ((size_t)block < (size_t)mem_heap_hi()) {
        /* check each block */
        assert(GET_WORD(HEADER(block)) == GET_WORD(FOOTER(block)));
        if (IS_FREE(block)) {
            ++n_free_blocks;
            fl_prev = FL_PREV(block);
            if (fl_prev) {
                assert(block == FL_NEXT(fl_prev));
                assert(IS_FREE(fl_prev));
            }
            else {
                /* `block` is the first free block */
                assert(block == first_free_block);
            }
            fl_next = FL_NEXT(block);
            if (fl_next) {
                assert(block == FL_PREV(fl_next));
                assert(IS_FREE(fl_next));
            }
            else {
                /* `block` is the last free block */
            }
        }
        else {
            ++n_aloc_blocks;
        }
        block = VM_NEXT(block);
    }
    ++n_aloc_blocks; /* epilogue block is allocated */
    assert(n_aloc_blocks == aloc_block_count);
    assert(n_free_blocks == free_block_count);

    /* traverse the free list */
    n_free_blocks = 0;
    block = first_free_block;
    while (block) {
        assert(IS_FREE(block));
        ++n_free_blocks;
        block = FL_NEXT(block);
    }
    assert(n_free_blocks == free_block_count);

    if (verbose) {
        dbg_printf("mm_checkheap() succeeds.\n");
    }
}
