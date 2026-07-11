#ifndef HYBRID_KNOT_INDEXER_LOCAL_GC_H
#define HYBRID_KNOT_INDEXER_LOCAL_GC_H

#include <stdlib.h>

#define GC_INIT() ((void)0)
#define GC_MALLOC(size) malloc(size)
#define GC_REALLOC(ptr, size) realloc((ptr), (size))

#endif
