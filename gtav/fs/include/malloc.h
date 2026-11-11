/* gtav - freestanding stand-in for <malloc.h> (clang's mm_malloc.h on Windows). */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* _aligned_malloc(size_t, size_t);
void _aligned_free(void*);
#ifdef __cplusplus
}
#endif
