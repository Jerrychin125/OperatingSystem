#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#define POOL_SIZE   20000UL
#define ALIGNMENT   32UL
#define HEADER_SIZE 32UL
#define LEVELS      11

typedef struct block {
    size_t size;            // payload 大小（不含 header），必為 32 的倍數
    int    free;            // 1 = free, 0 = used
    struct block *next_phys; // 物理上下一個 chunk
    struct block *next_free; // 該 level freelist 的下一個 free block
} block;

static void  *heap_start = NULL;     // mmap 回來的起始位址
static block *head       = NULL;     // 整個 chunk list 的第一個 block
static block *free_lists[LEVELS];    // multilevel free lists

// ------------------ malloc ------------------

void *malloc(size_t size)
{
    block *cur, *best;
    size_t req;
    int i;

    // 第一次呼叫（且 size != 0）：初始化 memory pool
    if (!heap_start && size != 0) {
        heap_start = mmap(NULL, POOL_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE,
                          -1, 0);
        if (heap_start == MAP_FAILED) {
            heap_start = NULL;
            return NULL;
        }

        // 初始化 chunk list：一個大 free block
        head = (block *)heap_start;
        head->size      = POOL_SIZE - HEADER_SIZE;
        head->free      = 1;
        head->next_phys = NULL;
        head->next_free = NULL;

        // 初始化 multilevel free list
        for (i = 0; i < LEVELS; ++i) free_lists[i] = NULL;

        // 把整塊放到對應 level
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

    // malloc(0)：輸出最大 free chunk，並 munmap 整個 pool
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

    // 如果 mmap 失敗過
    if (!heap_start) return NULL;

    // 對齊到 32 bytes
    req = (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    // 找 req 對應的起始 level
    size_t lower = 0, upper = ALIGNMENT;
    int start_lvl = LEVELS - 1;
    for (i = 0; i < LEVELS; ++i) {
        if (req > lower && req <= upper) { start_lvl = i; break; }
        lower = upper;
        upper <<= 1;
    }

    // 從 start_lvl 往更大的 level 找 Best Fit
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

    if (!best) {
        // 沒有足夠大的 free block
        return NULL;
    }

    // 從 best 所在的 free list 裡移除它
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

    // 如果剩下空間夠大，就切一個新的 free block
    if (free_size >= req + HEADER_SIZE + ALIGNMENT) {
        size_t new_free_size = free_size - req - HEADER_SIZE;
        block *newb = (block *)((char *)best + HEADER_SIZE + req);

        newb->size      = new_free_size;
        newb->free      = 1;
        newb->next_phys = best->next_phys;
        newb->next_free = NULL;

        best->next_phys = newb;

        // 把 newb 放到對應 level freelist 的尾巴
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

        best->size = req;    // 給使用者的 payload 大小
    }
    // 否則就整塊給使用者，不切割（size 保持原 free_size）

    best->free = 0;
    return (void *)((char *)best + HEADER_SIZE);
}

// ------------------ free ------------------

void free(void *ptr)
{
    if (!ptr || !heap_start) return;

    block *b = (block *)((char *)ptr - HEADER_SIZE);
    b->free = 1;

    // 先試著跟右邊合併（next_phys）
    if (b->next_phys && b->next_phys->free) {
        block *right = b->next_phys;

        // 從 right 的 freelist 移除它
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

        // 物理合併
        b->size += HEADER_SIZE + right->size;
        b->next_phys = right->next_phys;
    }

    // 再找左邊（需要從 head 掃一次）
    block *prev_phys = NULL;
    block *cur = head;
    while (cur && cur != b) {
        prev_phys = cur;
        cur = cur->next_phys;
    }

    if (prev_phys && prev_phys->free) {
        block *left = prev_phys;

        // 從 left 的 freelist 移除
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

        // 物理合併 left + b
        left->size += HEADER_SIZE + b->size;
        left->next_phys = b->next_phys;
        b = left; // 最終要丟回 freelist 的 block
    }

    // 把合併後的 block b 丟回對應 level freelist 尾巴
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
