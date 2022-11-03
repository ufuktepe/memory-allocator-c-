#include "m61.hh"
#include <cassert>
// Check if using a null pointer for reallocation behaves like m61_malloc(sz, file, line).

int main() {
    void* ptr = m61_realloc(nullptr, 10000);
    assert(ptr);
    m61_print_statistics();
}

//! alloc count: active          1   total          1   fail          0
//! alloc size:  active      10000   total      10000   fail          0