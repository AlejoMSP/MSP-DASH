#pragma once
#include <cstdlib>
#include <cstddef>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
void *heap_caps_malloc(size_t, int); size_t heap_caps_get_free_size(int); size_t heap_caps_get_largest_free_block(int); size_t heap_caps_get_minimum_free_size(int);
