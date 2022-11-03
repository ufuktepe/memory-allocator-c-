#include "m61.hh"
#include <cassert>
// Check if using zero bytes for reallocation returns a null pointer and check the statistics.

int main() {
    void* ptr = m61_malloc(10000);
    void* ptr2 = m61_realloc(ptr, 0);
    assert(ptr2 == nullptr);
    m61_print_statistics();
}

//! alloc count: active          1   total          1   fail          0
//! alloc size:  active      10000   total      10000   fail          0