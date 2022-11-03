#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cinttypes>
// Check if using a very large size for reallocation returns a null pointer and check the failed allocation size
// statistic.

int main() {
    void* ptr = m61_malloc(10000);
    size_t very_large_size = SIZE_MAX - 1;
    void* ptr2 = m61_realloc(ptr, very_large_size);
    assert(ptr2 == nullptr);
    m61_print_statistics();
}

//! alloc count: active          1   total          1   fail          1
//! alloc size:  active      10000   total      10000   fail 18446744073709551614