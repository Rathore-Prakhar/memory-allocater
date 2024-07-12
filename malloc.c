#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct Header {
    size_t size;
    struct Header* next;
    struct Header* prev;
    int is_free;
} header_t;

static header_t* head = NULL;
static header_t* tail = NULL;
static pthread_mutex_t global_malloc_lock = PTHREAD_MUTEX_INITIALIZER;

void add_to_free_list(header_t* block) {
    block->is_free = 1;
    block->next = head;
    if (head) {
        head->prev = block;
    }
    head = block;
    if (!tail) {
        tail = head;
    }
}

void remove_from_free_list(header_t* block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    } else {
        tail = block->prev;
    }
}

header_t* get_free_block(size_t size) {
    header_t* curr = head;
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
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    if (header) {
        header->is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
    }
    total_size = size + sizeof(header_t);
    header = sbrk(total_size);
    if (header == (void*) -1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }
    header->size = size;
    header->next = NULL;
    header->prev = NULL;
    header->is_free = 0;
    if (tail) {
        tail->next = header;
        header->prev = tail;
        tail = header;
    } else {
        head = tail = header;
    }
    block = (void*)(header + 1);
    pthread_mutex_unlock(&global_malloc_lock);
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
    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1;

    programbreak = sbrk(0);

    if ((char*)block + header->size == programbreak) {
        if (head == tail) {
            head = tail = NULL;
        } else {
            tmp = head;
            while (tmp->next != tail) {
                tmp = tmp->next;
            }
            tmp->next = NULL;
            tail = tmp;
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
    if (!block) {
        return NULL;
    }
    memset(block, 0, size);

    return block;
}
