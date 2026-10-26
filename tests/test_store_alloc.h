#ifndef TEST_STORE_ALLOC_H
#define TEST_STORE_ALLOC_H
#include <stdlib.h>
void* test_store_calloc(size_t,size_t);
void test_store_free(void*);
#define calloc test_store_calloc
#define free test_store_free
#endif
