#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define NUM_FREE_LISTS 10

typedef struct Header {
    size_t size;
    struct Header* next;
    struct Header* prev;
    int is_free;
} header_t;

static header_t* free_lists[NUM_FREE_LISTS] = {NULL};
static pthread_mutex_t free_list_locks[NUM_FREE_LISTS] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_mutex_t global_sbrk_lock = PTHREAD_MUTEX_INITIALIZER;

size_t get_free_list_index(size_t size) {
    size_t index = 0;
    size_t temp_size = size;
    while (temp_size >>= 1) {
        index++;
    }
    return (index < NUM_FREE_LISTS) ? index : NUM_FREE_LISTS - 1;
}

void add_to_free_list(header_t* block) {
    size_t index = get_free_list_index(block->size);
    pthread_mutex_lock(&free_list_locks[index]);

    block->is_free = 1;
    block->next = free_lists[index];
    if (free_lists[index]) {
        free_lists[index]->prev = block;
    }
    free_lists[index] = block;

    pthread_mutex_unlock(&free_list_locks[index]);
}

void remove_from_free_list(header_t* block) {
    size_t index = get_free_list_index(block->size);
    pthread_mutex_lock(&free_list_locks[index]);

    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_lists[index] = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }

    pthread_mutex_unlock(&free_list_locks[index]);
}

header_t* get_free_block(size_t size) {
    size_t index = get_free_list_index(size);
    for (; index < NUM_FREE_LISTS; ++index) {
        pthread_mutex_lock(&free_list_locks[index]);
        header_t* curr = free_lists[index];
        while (curr) {
            if (curr->is_free && curr->size >= size) {
                remove_from_free_list(curr);
                pthread_mutex_unlock(&free_list_locks[index]);
                return curr;
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&free_list_locks[index]);
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
    header = get_free_block(size);
    if (header) {
        header->is_free = 0;
        return (void*)(header + 1);
    }
    pthread_mutex_lock(&global_sbrk_lock);
    total_size = size + sizeof(header_t);
    header = sbrk(total_size);
    if (header == (void*) -1) {
        pthread_mutex_unlock(&global_sbrk_lock);
        return NULL;
    }
    header->size = size;
    header->next = NULL;
    header->prev = NULL;
    header->is_free = 0;
    pthread_mutex_unlock(&global_sbrk_lock);
    block = (void*)(header + 1);
    return block;
}

void* realloc(void* block, size_t size) {
    header_t* header;
    void* ret;
    if (!block || size == 0) {
        return malloc(size);
    }
    header = (header_t*)block - 1;
    if (header->size >= size) {
        return block;
    }
    ret = malloc(size);
    if (ret) {
        memmove(ret, block, header->size);
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
    header = (header_t*)block - 1;

    pthread_mutex_lock(&global_sbrk_lock);
    programbreak = sbrk(0);
    if ((char*)block + header->size == programbreak) {
        sbrk(0 - sizeof(header_t) - header->size);
        pthread_mutex_unlock(&global_sbrk_lock);
        return;
    }
    pthread_mutex_unlock(&global_sbrk_lock);

    add_to_free_list(header);
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
    if (!block) {
        return NULL;
    }
    memset(block, 0, size);
    return block;
}

size_t get_allocated_memory() {
    size_t total = 0;
    for (int i = 0; i < NUM_FREE_LISTS; ++i) {
        pthread_mutex_lock(&free_list_locks[i]);
        header_t* curr = free_lists[i];
        while (curr) {
            if (!curr->is_free) {
                total += curr->size;
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&free_list_locks[i]);
    }
    return total;
}

size_t get_free_memory() {
    size_t total = 0;
    for (int i = 0; i < NUM_FREE_LISTS; ++i) {
        pthread_mutex_lock(&free_list_locks[i]);
        header_t* curr = free_lists[i];
        while (curr) {
            if (curr->is_free) {
                total += curr->size;
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&free_list_locks[i]);
    }
    return total;
}

void print_memory_usage() {
    printf("Allocated memory: %zu bytes\n", get_allocated_memory());
    printf("Free memory: %zu bytes\n", get_free_memory());
}

/*
test program

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
