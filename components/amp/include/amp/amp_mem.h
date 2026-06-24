#if !defined(_AMP_MEM_H_)
#define _AMP_MEM_H_

#include <stddef.h>

void *amp_malloc(size_t);

void *amp_calloc(size_t nmemb, size_t size);

void *amp_realloc(void *ptr, size_t size);

#endif // _AMP_MEM_H_