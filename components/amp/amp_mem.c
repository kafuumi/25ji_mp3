
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "amp/amp_mem.h"

void *amp_malloc(size_t size) {
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = malloc(size);
#endif // CONFIG_SPIRAM_BOOT_INIT
    return data;
}

void *amp_calloc(size_t nmemb, size_t size) {
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = calloc(nmemb, size);
#endif // CONFIG_SPIRAM_BOOT_INIT
    return data;
}

void *amp_realloc(void *ptr, size_t size) {
    void *data = NULL;
#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    data = realloc(ptr, size);
#endif // CONFIG_SPIRAM_BOOT_INIT
    return data;
}
