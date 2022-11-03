#include "m61.hh"
#include <cassert>
// Check if reallocating exactly the same size as the original payload size returns a non-null pointer.
// Check if reallocating less than the original payload size returns a non-null pointer.
// Check if reallocating greater than the original payload size returns a non-null pointer.

int main() {
    void* ptr1 = m61_malloc(2000);
    void* ptr2 = m61_malloc(2000);
    void* ptr3 = m61_malloc(2000);
    void* new_ptr1 = m61_realloc(ptr1, 1000);
    void* new_ptr2 = m61_realloc(ptr2, 2000);
    void* new_ptr3 = m61_realloc(ptr3, 3000);
    assert(new_ptr1);
    assert(new_ptr2);
    assert(new_ptr3);
    printf("OK\n");
}

//! OK