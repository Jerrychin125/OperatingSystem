#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#define POOL_SIZE   20000UL
#define ALIGNMENT   32UL
#define HEADER_SIZE 32UL
#define LEVELS      11

typedef struct block {
    size_t size;
    int    free;
    struct block *next_phys;
    struct block *next_free;
} block;

static void  *heap_start = NULL;
static block *head       = NULL;
static block *free_lists[LEVELS];

// ------------------ malloc ------------------

void *malloc(size_t size) {
    block *cur, *best;
    size_t req;
    int i;

    // Initial Heap
    if (!heap_start && size != 0) {
        heap_start = mmap(NULL, POOL_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE,
                          -1, 0);
        if (heap_start == MAP_FAILED) {
            heap_start = NULL;
            return NULL;
        }

        // Initial chunk list
        head = (block *)heap_start;
        head->size      = POOL_SIZE - HEADER_SIZE;
        head->free      = 1;
        head->next_phys = NULL;
        head->next_free = NULL;

        // Initial multilevel free list
        for (i = 0; i < LEVELS; ++i) free_lists[i] = NULL;

        size_t sz = head->size;
        size_t lower = 0, upper = ALIGNMENT;
        int lvl = LEVELS - 1;
        for (i = 0; i < LEVELS; ++i) {
            if (sz > lower && sz <= upper) { lvl = i; break; }
            lower = upper;
            upper <<= 1;
        }
        free_lists[lvl] = head;
    }

    // malloc(0): release free chunk and unmap
    if (size == 0) {
        if (!heap_start) return NULL;

        size_t maxsz = 0;
        cur = head;
        while (cur) {
            if (cur->free && cur->size > maxsz) maxsz = cur->size;
            cur = cur->next_phys;
        }

        // Format: Max Free Chunk Size = $size in bytes\n
        printf("Max Free Chunk Size = %zu in bytes\n", maxsz);

        munmap(heap_start, POOL_SIZE);
        heap_start = NULL;
        head = NULL;
        for (i = 0; i < LEVELS; ++i) free_lists[i] = NULL;

        return NULL;
    }

    // return if munmap failed
    if (!heap_start) return NULL;

    // Align to 32 bytes
    req = (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    // Start level
    size_t lower = 0, upper = ALIGNMENT;
    int start_lvl = LEVELS - 1;
    for (i = 0; i < LEVELS; ++i) {
        if (req > lower && req <= upper) { start_lvl = i; break; }
        lower = upper;
        upper <<= 1;
    }

    // Best Fit
    best = NULL;
    int best_lvl = -1;
    for (int lvl = start_lvl; lvl < LEVELS; ++lvl) {
        block *p = free_lists[lvl];
        block *candidate = NULL;
        while (p) {
            if (p->free && p->size >= req) {
                if (!candidate || p->size < candidate->size) {
                    candidate = p;
                }
            }
            p = p->next_free;
        }
        if (candidate) {
            best = candidate;
            best_lvl = lvl;
            break;
        }
    }

    // return if no enough free block
    if (!best) {
        return NULL;
    }

    // rm best block from free lish
    {
        block *p = free_lists[best_lvl];
        block *prev = NULL;
        while (p) {
            if (p == best) {
                if (prev) prev->next_free = p->next_free;
                else      free_lists[best_lvl] = p->next_free;
                p->next_free = NULL;
                break;
            }
            prev = p;
            p = p->next_free;
        }
    }

    size_t free_size = best->size;

    // if enough space? create a free block
    if (free_size >= req + HEADER_SIZE + ALIGNMENT) {
        size_t new_free_size = free_size - req - HEADER_SIZE;
        block *newb = (block *)((char *)best + HEADER_SIZE + req);

        newb->size      = new_free_size;
        newb->free      = 1;
        newb->next_phys = best->next_phys;
        newb->next_free = NULL;

        best->next_phys = newb;

        // put new block to free list
        size_t sz = newb->size;
        size_t low = 0, up = ALIGNMENT;
        int lvl = LEVELS - 1;
        for (i = 0; i < LEVELS; ++i) {
            if (sz > low && sz <= up) { lvl = i; break; }
            low = up;
            up <<= 1;
        }
        if (!free_lists[lvl]) {
            free_lists[lvl] = newb;
        } else {
            block *t = free_lists[lvl];
            while (t->next_free) t = t->next_free;
            t->next_free = newb;
        }

        best->size = req;
    }

    best->free = 0;
    return (void *)((char *)best + HEADER_SIZE);
}

// ------------------ free ------------------

void free(void *ptr)
{
    if (!ptr || !heap_start) return;

    block *b = (block *)((char *)ptr - HEADER_SIZE);
    b->free = 1;

    // merge with right block
    if (b->next_phys && b->next_phys->free) {
        block *right = b->next_phys;

        // rm from right free list
        size_t sz = right->size;
        size_t lower = 0, upper = ALIGNMENT;
        int lvl = LEVELS - 1;
        for (int i = 0; i < LEVELS; ++i) {
            if (sz > lower && sz <= upper) { lvl = i; break; }
            lower = upper;
            upper <<= 1;
        }
        block *p = free_lists[lvl], *prev = NULL;
        while (p) {
            if (p == right) {
                if (prev) prev->next_free = p->next_free;
                else      free_lists[lvl] = p->next_free;
                break;
            }
            prev = p;
            p = p->next_free;
        }

        // physical merge
        b->size += HEADER_SIZE + right->size;
        b->next_phys = right->next_phys;
    }

    // left
    block *prev_phys = NULL;
    block *cur = head;
    while (cur && cur != b) {
        prev_phys = cur;
        cur = cur->next_phys;
    }

    if (prev_phys && prev_phys->free) {
        block *left = prev_phys;

        // rm from left free list
        size_t sz = left->size;
        size_t lower = 0, upper = ALIGNMENT;
        int lvl = LEVELS - 1;
        for (int i = 0; i < LEVELS; ++i) {
            if (sz > lower && sz <= upper) { lvl = i; break; }
            lower = upper;
            upper <<= 1;
        }
        block *p = free_lists[lvl], *prev = NULL;
        while (p) {
            if (p == left) {
                if (prev) prev->next_free = p->next_free;
                else      free_lists[lvl] = p->next_free;
                break;
            }
            prev = p;
            p = p->next_free;
        }

        // physical merge left + b
        left->size += HEADER_SIZE + b->size;
        left->next_phys = b->next_phys;
        b = left; // tail
    }

    // put back to free list
    size_t sz = b->size;
    size_t lower = 0, upper = ALIGNMENT;
    int lvl = LEVELS - 1;
    for (int i = 0; i < LEVELS; ++i) {
        if (sz > lower && sz <= upper) { lvl = i; break; }
        lower = upper;
        upper <<= 1;
    }

    b->next_free = NULL;
    if (!free_lists[lvl]) {
        free_lists[lvl] = b;
    } else {
        block *t = free_lists[lvl];
        while (t->next_free) t = t->next_free;
        t->next_free = b;
    }
}
