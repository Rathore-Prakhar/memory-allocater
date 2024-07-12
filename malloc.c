#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define POOL_SIZE 1024 * 1024 // 1 MB

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
static void* pool_start = NULL;

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

void* malloc(size_t size) {
    size_t total_size;
    void* block;
    header_t* header;

    if (size == 0) {
        return NULL;
    }

    size = ALIGN(size);
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    if (header) {
        header->is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
    }

    total_size = size + sizeof(header_t);
    if (!pool_start || (char*)sbrk(0) - (char*)pool_start + total_size > POOL_SIZE) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

    header = sbrk(total_size);
    if (header == (void*) -1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

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

    global_pool.allocated_memory += size;
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

    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1;

    programbreak = sbrk(0);
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
        sbrk(0 - sizeof(header_t) - header->size);
    } else {
        add_to_free_list(header);
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

void initialize_memory_pool() {
    pthread_mutex_lock(&global_malloc_lock);
    if (!pool_start) {
        pool_start = sbrk(POOL_SIZE);
        if (pool_start == (void*)-1) {
            pool_start = NULL;
        }
    }
    pthread_mutex_unlock(&global_malloc_lock);
}

/*
test program 1

int main() {
    initialize_memory_pool();
    int* arr = (int*)malloc(10 * sizeof(int));
    if (arr) {
        for (int i = 0; i < 10; i++) {
            arr[i] = i;
        }
        free(arr);
    }
    print_memory_usage();
    return 0;
}
*/

/*
test program 2

#include <stdio.h>
#include <stdlib.h>

int main() {
    int *arr1 = (int *)malloc(5 * sizeof(int));
    for (int i = 0; i < 5; ++i) {
        arr1[i] = i * 2;
    }
    printf("Array 1: ");
    for (int i = 0; i < 5; ++i) {
        printf("%d ", arr1[i]);
    }
    printf("\n");
    free(arr1);
    int *arr2 = (int *)malloc(3 * sizeof(int));
    for (int i = 0; i < 3; ++i) {
        arr2[i] = i * 3;
    }
    printf("Array 2: ");
    for (int i = 0; i < 3; ++i) {
        printf("%d ", arr2[i]);
    }
    printf("\n");
    int *arr3 = (int *)realloc(arr2, 5 * sizeof(int));
    for (int i = 3; i < 5; ++i) {
        arr3[i] = i * 4;
    }
    printf("Array 3: ");
    for (int i = 0; i < 5; ++i) {
        printf("%d ", arr3[i]);
    }
    printf("\n");
    free(arr3);
    return 0;
}
*/
