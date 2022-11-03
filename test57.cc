#include "m61.hh"
#include <cstdio>
// Check if large blocks of memory can be reallocated when the buffer is almost full.

int main() {
    void* ptrs[1000];
    for (int i = 0; i != 1000; ++i) {
        ptrs[i] = m61_malloc(8000);
    }
    for (int i = 0; i != 1000; ++i) {
        void* ptr = m61_realloc(ptrs[i], 8000);
        assert(ptr);
    }
    m61_print_statistics();
}

//! alloc count: active       1000   total       2000   fail          0
//! alloc size:  active    8000000   total   16000000   fail          0