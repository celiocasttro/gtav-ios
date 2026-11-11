/* gtav - freestanding stand-in for <stdlib.h>: clang's mm_malloc.h (pulled in by immintrin.h)
   only needs these declarations. The game DLL never calls them. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t);
void free(void*);
#ifdef __cplusplus
}
#endif
