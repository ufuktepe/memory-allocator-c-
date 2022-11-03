#include "m61.hh"
// Check if after allocating and reallocating a block, the active count and size stays the same whereas the total count
// and size doubles.

int main() {
    void* ptr = m61_malloc(1000);
    m61_realloc(ptr, 1000);
    m61_print_statistics();
}

//! alloc count: active          1   total          2   fail          0
//! alloc size:  active       1000   total       2000   fail          0