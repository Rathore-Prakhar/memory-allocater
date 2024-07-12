/*
how to run this

gcc -o malloc malloc.c -lpthread
./malloc
*/

#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define POOL_SIZE (1024 * 1024) // 1 MB

typedef struct Header {
    size_t size;
    struct Header* next;
    struct Header* prev;
    int is_free;
} header_t;

typedef struct {
    header_t* head;
    header_t* tail;
    size_t allocated_memory;
    size_t free_memory;
} memory_pool_t;

static memory_pool_t global_pool = {NULL, NULL, 0, 0};
static pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t pool_initialized = PTHREAD_ONCE_INIT;
static void* pool_start = NULL;

void initialize_memory_pool() {
    pool_start = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (pool_start == MAP_FAILED) {
        pool_start = NULL;
        fprintf(stderr, "Memory pool initialization failed\n");
        exit(EXIT_FAILURE);
    }
    printf("Memory pool initialized at %p\n", pool_start);
}

void add_to_free_list(header_t* block) {
    block->is_free = 1;
    block->next = global_pool.head;
    if (global_pool.head) {
        global_pool.head->prev = block;
    }
    global_pool.head = block;
    if (!global_pool.tail) {
        global_pool.tail = global_pool.head;
    }
    global_pool.free_memory += block->size;
}

void remove_from_free_list(header_t* block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        global_pool.head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    } else {
        global_pool.tail = block->prev;
    }
    global_pool.free_memory -= block->size;
}

header_t* get_free_block(size_t size) {
    header_t* curr = global_pool.head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            remove_from_free_list(curr);
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void split_block(header_t* block, size_t size) {
    if (block->size >= size + sizeof(header_t) + ALIGNMENT) {
        header_t* new_block = (header_t*)((char*)block + sizeof(header_t) + size);
        new_block->size = block->size - size - sizeof(header_t);
        block->size = size;
        add_to_free_list(new_block);
    }
}

void coalesce_free_blocks() {
    header_t* curr = global_pool.head;
    while (curr && curr->next) {
        if ((char*)curr + curr->size + sizeof(header_t) == (char*)curr->next) {
            curr->size += curr->next->size + sizeof(header_t);
            remove_from_free_list(curr->next);
        } else {
            curr = curr->next;
        }
    }
}

void* malloc(size_t size) {
    size_t total_size;
    void* block;
    header_t* header;

    if (size == 0) {
        return NULL;
    }

    pthread_once(&pool_initialized, initialize_memory_pool);

    size = ALIGN(size);
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    if (header) {
        split_block(header, size);
        header->is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
    }

    total_size = size + sizeof(header_t);
    if (!pool_start || (char*)pool_start + POOL_SIZE - (char*)global_pool.tail - total_size < 0) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

    header = (header_t*)((char*)pool_start + global_pool.allocated_memory);
    header->size = size;
    header->next = NULL;
    header->prev = NULL;
    header->is_free = 0;

    if (global_pool.tail) {
        global_pool.tail->next = header;
        header->prev = global_pool.tail;
        global_pool.tail = header;
    } else {
        global_pool.head = global_pool.tail = header;
    }

    global_pool.allocated_memory += total_size;
    block = (void*)(header + 1);
    pthread_mutex_unlock(&global_malloc_lock);
    return block;
}

void* realloc(void* block, size_t size) {
    header_t* header;
    void* ret;
    if (!block) {
        return malloc(size);
    }
    if (size == 0) {
        free(block);
        return NULL;
    }

    header = (header_t*)block - 1;
    if (header->size >= size) {
        return block;
    }

    ret = malloc(size);
    if (ret) {
        memcpy(ret, block, header->size);
        free(block);
    }
    return ret;
}

void free(void* block) {
    header_t* header, *tmp;
    void* programbreak;
    if (!block) {
        return;
    }

    pthread_once(&pool_initialized, initialize_memory_pool);

    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1;

    programbreak = (char*)pool_start + POOL_SIZE;
    if ((char*)block + header->size == programbreak) {
        if (global_pool.head == global_pool.tail) {
            global_pool.head = global_pool.tail = NULL;
        } else {
            tmp = global_pool.head;
            while (tmp->next != global_pool.tail) {
                tmp = tmp->next;
            }
            tmp->next = NULL;
            global_pool.tail = tmp;
        }
    } else {
        add_to_free_list(header);
        coalesce_free_blocks();
    }

    pthread_mutex_unlock(&global_malloc_lock);
}

void* calloc(size_t num, size_t nsize) {
    size_t size;
    void* block;

    if (!num || !nsize) {
        return NULL;
    }

    size = num * nsize;
    if (nsize != size / num) {
        return NULL;
    }

    block = malloc(size);
    if (block) {
        memset(block, 0, size);
    }
    return block;
}

// debugging
size_t get_allocated_memory() {
    return global_pool.allocated_memory;
}

size_t get_free_memory() {
    return global_pool.free_memory;
}

void print_memory_usage() {
    printf("Allocated memory: %zu bytes\n", get_allocated_memory());
    printf("Free memory: %zu bytes\n", get_free_memory());
}

void print_free_list() {
    header_t* curr = global_pool.head;
    printf("Free list:\n");
    while (curr) {
        printf("Block at %p, size: %zu\n", (void*)curr, curr->size);
        curr = curr->next;
    }
}

void print_pool_status() {
    printf("Memory pool starts at: %p\n", pool_start);
    printf("Memory pool ends at: %p\n", (char*)pool_start + POOL_SIZE);
    printf("Total pool size: %d bytes\n", POOL_SIZE);
}

// Test program 1
int main() {
    printf("Initializing memory pool...\n");
    initialize_memory_pool();
    print_pool_status();

    int* arr = (int*)malloc(10 * sizeof(int));
    if (arr) {
        printf("Allocated array at %p\n", arr);
        for (int i = 0; i < 10; i++) {
            arr[i] = i;
        }
        free(arr);
        printf("Freed array at %p\n", arr);
    }

    print_memory_usage();
    print_free_list();
    print_pool_status();

    return 0;
}
