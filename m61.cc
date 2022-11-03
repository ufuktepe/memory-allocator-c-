#include "m61.hh"
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>
#include <sys/mman.h>

// Free block identifier
#define FREE (char*) 0xCAFEFEED

// Allocated block identifier
#define ALLOCATED (char*) 0xDEADF00D

// Array that is written at the beginning of each block's padding
constexpr char END_MARKER[8] = {0x44, 0x45, 0x41, 0x44, 0x43, 0x30, 0x44, 0x45};

// Alignment used for the blocks
const size_t ALIGNMENT = alignof(std::max_align_t);

// Minimum required size for a block
const size_t MIN_BLOCK_SIZE = sizeof(header) + ALIGNMENT;

// Head node that stores per-allocation metadata
header* head = nullptr;

struct m61_memory_buffer {
    char* buffer;
    size_t pos = 0;
    size_t size = 8 << 20; /* 8 MiB */

    m61_memory_buffer();
    ~m61_memory_buffer();
};

static m61_memory_buffer default_buffer;

m61_memory_buffer::m61_memory_buffer() {
    void* buf = mmap(nullptr,    // Place the buffer at a random address
                     this->size,              // Buffer should be 8 MiB big
                     PROT_WRITE,              // We want to read and write the buffer
                     MAP_ANON | MAP_PRIVATE, -1, 0);
    // We want memory freshly allocated by the OS
    assert(buf != MAP_FAILED);
    this->buffer = (char*) buf;
}

m61_memory_buffer::~m61_memory_buffer() {
    munmap(this->buffer, this->size);
}

static m61_statistics gstats = {
        .nactive = 0,
        .active_size = 0,
        .ntotal = 0,
        .total_size = 0,
        .nfail = 0,
        .fail_size = 0,
        .heap_min = 0,
        .heap_max = 0
};

/// add_block(p_header)
///    Adds a node to the head of the linked list.
static void add_block(header* p_header) {
    p_header->p_next = head;
    p_header->p_prev = nullptr;
    if (head) {
        head->p_prev = p_header;
    }
    head = p_header;
}

/// remove_block(p_header)
///    Removes a node from the the linked list. Does nothing if the given header pointer is null or if the linked list
///    includes no nodes.
static void remove_block(header* p_header) {
    if (head == nullptr || p_header == nullptr) {
        return;
    }

    header* p_header_next = p_header->p_next;
    header* p_header_prev = p_header->p_prev;

    if (p_header == head) {
        head = p_header_next;
    }

    if (p_header_next) {
        assert(p_header_next->p_prev == p_header);
        p_header_next->p_prev = p_header_prev;
    }

    if (p_header_prev) {
        assert(p_header_prev->p_next == p_header);
        p_header_prev->p_next = p_header_next;
    }
}

/// insert_before_block(p_header_new, p_header_next)
///    Inserts a node into the linked list before a given node. That is, the header pointed to by 'p_header_new' is
///    inserted into the linked list immediately before the header pointed to by p_header_next.
static void insert_before_block(header* p_header_new, header* p_header_next) {
    p_header_new->p_next = p_header_next;
    p_header_new->p_prev = p_header_next->p_prev;
    if (p_header_next->p_prev) {
        p_header_next->p_prev->p_next = p_header_new;
    }
    p_header_next->p_prev = p_header_new;
}

/// add_end_marker(ptr)
///    Copies the END_MARKER array byte by byte to the memory location pointed to by the given pointer.
static void add_end_marker(char* ptr) {
    for (char marker : END_MARKER) {
        memset(ptr, marker, 1);
        ++ptr;
    }
}

/// is_end_marker_valid(ptr)
///    Checks byte by byte whether the memory location includes the END_MARKER array starting at the address that the
///    given pointer points to.
static bool is_end_marker_valid(char* ptr) {
    for (char marker : END_MARKER) {
        if (*ptr != marker) {
            return false;
        }
        ++ptr;
    }
    return true;
}

/// is_header_valid(p_header, p_payload)
///    Checks the validity of the given header pointer. If the header pointer is misaligned or the header points to an
///    incorrect payload address the function returns false. Otherwise, it returns true. The header's payload pointer is
///    checked against 'p_payload'.
static bool is_header_valid(header* p_header, void* p_payload) {
    // Check for misalignment
    if (((uintptr_t) p_header) % ALIGNMENT != 0) {
        return false;
    }

    // Check if the header's payload pointer points to the correct address
    if (p_header->p_payload && (p_header->p_payload != (char*) p_payload)) {
        return false;
    }

    return true;
}

/// is_block_free(p_header)
///    Returns true if the block pointed to by the given header is free. Otherwise, returns false.
static bool is_block_free(header* p_header) {
    return p_header && (p_header->p_status == FREE);
}

/// is_overflowing(a, b)
///    Returns true if multiplying 'a' and 'b' results in overflow. Otherwise, returns false.
static bool is_overflowing(size_t a, size_t b) {
    if (a == 0 || b == 0) {
        return false;
    }
    size_t c = a * b;
    return a != c / b || c % b != 0;
}

/// get_payload_size(p_header)
///    Returns the size of the payload for the given header pointer.
static size_t get_payload_size(header* p_header) {
    auto payload_addr = (uintptr_t) p_header->p_payload;
    return ((uintptr_t) p_header->p_end_marker) - payload_addr;
}

/// add_to_statistics(sz, ptr)
///    Updates the statistics for allocation. 'sz' is the allocated size and 'ptr' is the pointer for the starting
///    address of the allocation.
static void add_to_statistics(size_t sz, void* ptr) {
    ++gstats.ntotal;
    ++gstats.nactive;
    gstats.total_size += sz;
    gstats.active_size += sz;

    if (!gstats.heap_min || gstats.heap_min > (uintptr_t) ptr) {
        gstats.heap_min = (uintptr_t) ptr;
    }
    if (!gstats.heap_max || gstats.heap_max < (uintptr_t) ptr + sz) {
        gstats.heap_max = (uintptr_t) ptr + sz;
    }
}

/// remove_from_statistics(size_t sz)
///    Updates the statistics for freeing a memory block. 'sz' is the freed size that was previously allocated.
static void remove_from_statistics(size_t sz) {
    --gstats.nactive;
    gstats.active_size -= sz;
}

/// update_statistics_for_failure(size_t sz)
///    Updates the statistics for a failed allocation. 'sz' is the requested size for the failed allocation.
static void update_statistics_for_failure(size_t sz) {
    gstats.fail_size += sz ;
    ++gstats.nfail;
}

/// can_coalesce_up(p_header)
///    Returns true if the block pointed to by the given header pointer can be merged with its predecessor. Otherwise,
///    returns false.
static bool can_coalesce_up(header* p_header) {
    if (!is_block_free(p_header->p_prev)) {
        return false;
    }
    assert(p_header->p_prev->p_next == p_header);
    return true;
}

/// can_coalesce_down(p_header)
///    Returns true if the block pointed to by the given header pointer can be merged with its successor. Otherwise,
///    returns false.
static bool can_coalesce_down(header* p_header) {
    if (!is_block_free(p_header->p_next)) {
        return false;
    }
    assert(p_header->p_next->p_prev == p_header);
    return true;
}

/// coalesce(p_header)
///    If possible, merges the freed block pointed to by the given header pointer with its adjacent blocks. Does nothing
///    if the block cannot be coalesced either up or down.
static void coalesce(header* p_header) {
    // Try to merge the current block with its predecessor
    if (can_coalesce_up(p_header)) {
        p_header->block_size += p_header->p_prev->block_size;
        remove_block(p_header->p_prev);
    }

    // Try to merge the current block with its successor
    if (can_coalesce_down(p_header)) {
        p_header->p_next->block_size += p_header->block_size;
        remove_block(p_header);
    }
}

/// move_buffer_pos()
///    If the last block in the linked list (head) is a free block, moves the buffer position to the starting address
///    of the last block and removes that block from the linked list.
static void move_buffer_pos() {
    if (head == nullptr || head->p_status == ALLOCATED) {
        return;
    }
    default_buffer.pos -= head->block_size;
    remove_block(head);
}

/// report_ptr_inside_alloc_block(ptr)
///    Traverses the linked list and prints an error if the given pointer is inside an allocated block.
static void report_ptr_inside_alloc_block(void* ptr) {

    auto ptr_addr = (uintptr_t) ptr;

    header* p_header = head;
    while (p_header) {
        if (p_header->p_status != ALLOCATED) {
            continue;
        }

        auto payload_addr = (uintptr_t) p_header->p_payload;
        auto end_marker_addr = (uintptr_t) p_header->p_end_marker;

        // Check if the given pointer is between the payload's and end marker's starting addresses
        if (payload_addr <= ptr_addr && ptr_addr < end_marker_addr) {
            size_t offset = ptr_addr - payload_addr;
            size_t payload_size = get_payload_size(p_header);
            fprintf(stderr, "  %s:%d: %p is %zu bytes inside a %zu byte region allocated here\n", p_header->p_file,
                    p_header->line, ptr, offset, payload_size);
            return;
        }
        p_header = p_header->p_next;
    }
}

/// generate_generic_block(ptr, block_size, file, line)
///    Creates a generic block for the given payload pointer 'ptr' and returns the pointer for the header of the block.
///    'block_size' is the size of the block including the header and padding. The request was made at source code
///    location `file`:`line`.
static header* generate_generic_block(void* ptr, size_t block_size, const char* file, int line) {
    auto p_header = (header*) ptr;
    p_header->block_size = block_size;
    p_header->p_payload = (char*)(p_header + 1);
    p_header->p_file = file;
    p_header->line = line;

    return p_header;
}

/// generate_alloc_block(ptr, block_size, payload_size, file, line)
///    Creates an allocated block for the given payload pointer 'ptr' and returns the pointer for the header of the
///    block. 'block_size' is the size of the block including the header and padding whereas 'payload_size' is the
///    requested allocation size. The allocation request was made at source code location `file`:`line`.
static header* generate_alloc_block(void* ptr, size_t block_size, size_t payload_size, const char* file, int line) {
    // First create a generic block and get the pointer of its header
    auto p_header = generate_generic_block(ptr, block_size, file, line);

    p_header->p_status = ALLOCATED;
    p_header->p_end_marker = p_header->p_payload + payload_size;
    add_end_marker(p_header->p_end_marker);

    return p_header;
}

/// generate_free_block(ptr, block_size, file, line)
///    Creates a free block for the given payload pointer 'ptr' and returns the pointer for the header of the block.
///    'block_size' is the size of the block including the header and padding. The request was made at source code
///    location `file`:`line`.
static header* generate_free_block(void* ptr, size_t block_size, const char* file, int line) {
    // First create a generic block and get the pointer of its header
    auto p_header = generate_generic_block(ptr, block_size, file, line);

    p_header->p_status = FREE;
    p_header->p_end_marker = nullptr;

    return p_header;
}

/// split_block(p_header, required_size)
///    Splits the block which is pointed to by p_header if the difference between the block's size and 'required_size'
///    is at least as large as MIN_BLOCK_SIZE. 'required_size' is the required block size for p_header.
static void split_block(header* p_header, size_t required_size) {
    size_t residual_size = p_header->block_size - required_size;

    if (residual_size < MIN_BLOCK_SIZE) {
        return;
    }

    // Generate a free block
    void* ptr = (char*)p_header + required_size;
    header* p_header_new = generate_free_block(ptr, residual_size, p_header->p_file, p_header->line);

    // Insert the new free block into the linked list and adjust the block size of p_header
    insert_before_block(p_header_new, p_header);
    p_header->block_size = required_size;
}

/// find_freed_block(required_size, payload_size, file, line)
///    Traverses the linked list of blocks to find a free block that is at least as large as 'required_size'.
///    'required_size' is the block size that includes the header and padding. If a block is found, the block is
///    converted to an allocated block and the split_block function is called to split the block if possible. If no
///    block is found then nullptr is returned.
static void* find_freed_block(size_t required_size, size_t payload_size, const char* file, int line) {
    header* p_header = head;
    while (p_header) {
        if (p_header->p_status == FREE && p_header->block_size >= required_size) {
            // Allocate the block and then try to split it in case there is left over extra space
            p_header = generate_alloc_block((void*) p_header, p_header->block_size, payload_size, file, line);
            split_block(p_header, required_size);

            return p_header->p_payload;
        }
        p_header = p_header->p_next;
    }

    return nullptr;
}

/// find_free_space(block_size, payload_size, file, line)
///    Finds free space for the requested allocation. First tries to find a space in the default buffer. If there is
///    not enough space in the default buffer then calls find_freed_block to find a free space among freed blocks.
///    'block_size' is the required number of bytes including the header and padding. The allocation request was made
///    at source code location `file`:`line`. If it succeeds, returns a pointer for the payload. Otherwise, returns
///    nullptr.
static void* find_free_space(size_t block_size, size_t payload_size, const char* file, int line) {
    // Check if there is enough space in the default buffer
    if (default_buffer.size - default_buffer.pos >= block_size) {
        void* ptr = &default_buffer.buffer[default_buffer.pos];
        header* p_header = generate_alloc_block(ptr, block_size, payload_size, file, line);
        add_block(p_header);
        default_buffer.pos += block_size;

        return p_header->p_payload;
    }

    // Otherwise try to find a free space among the freed blocks
    return find_freed_block(block_size, payload_size, file, line);
}

/// m61_malloc(sz, p_file, line)
///    Returns a pointer to `sz` bytes of freshly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc may
///    return either `nullptr` or a pointer to a unique allocation.
///    The allocation request was made at source code location `file`:`line`.
void* m61_malloc(size_t sz, const char* file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings

    size_t padding = ALIGNMENT - ((sizeof(header) + sz) % ALIGNMENT);

    // Ensure there is enough space in the padding for END_MARKER
    if (padding < sizeof(END_MARKER)) {
        padding += ALIGNMENT;
    }

    // Check for overflow
    if (sz > SIZE_MAX - padding - sizeof(header)) {
        update_statistics_for_failure(sz);
        return nullptr;
    }

    size_t block_size = sizeof(header) + sz + padding;

    void* p_payload = find_free_space(block_size, sz, file, line);

    // Check if failed
    if (p_payload == nullptr) {
        update_statistics_for_failure(sz);
        return nullptr;
    }

    add_to_statistics(sz, p_payload);

    return (void*) p_payload;
}

/// m61_free(ptr, p_file, line)
///    Frees the memory allocation pointed to by `ptr`. If `ptr == nullptr`,
///    does nothing. Otherwise, `ptr` must point to a currently active
///    allocation returned by `m61_malloc`. The free was called at location
///    `p_file`:`line`.
void m61_free(void* ptr, const char* file, int line) {
    // avoid uninitialized variable warnings
    (void) ptr, (void) file, (void) line;

    if (ptr == nullptr) {
        return;
    }

    // Check whether ptr is a non-heap pointer
    if ((uintptr_t) ptr < gstats.heap_min || (uintptr_t) ptr > gstats.heap_max) {
        fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
        abort();
    }

    // Retrieve the header pointer of the block
    header* p_header = ((header*) ptr) - 1;

    // Check if p_header is a valid header pointer
    if (!is_header_valid(p_header, ptr)) {
        fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
        abort();
    }

    // Print errors if the block is not allocated
    if (p_header->p_status != ALLOCATED) {
        if (p_header->p_status == FREE) {
            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, double free\n", file, line, ptr);
        } else {
            fprintf(stderr, "MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            report_ptr_inside_alloc_block(ptr);
        }
        abort();
    }

    // Check if the end marker is valid
    if (!is_end_marker_valid(p_header->p_end_marker)) {
        fprintf(stderr, "MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
        abort();
    }

    // Update the statistics
    size_t payload_size = get_payload_size(p_header);
    remove_from_statistics(payload_size);

    // Free the block pointed to by p_header
    p_header = generate_free_block((void*) p_header, p_header->block_size, file, line);

    // Try to coalesce and move the buffer position
    coalesce(p_header);
    move_buffer_pos();
}

/// m61_calloc(count, sz, p_file, line)
///    Returns a pointer a fresh dynamic memory allocation big enough to
///    hold an array of `count` elements of `sz` bytes each. Returned
///    memory is initialized to zero. The allocation request was at
///    location `p_file`:`line`. Returns `nullptr` if out of memory; may
///    also return `nullptr` if `count == 0` or `size == 0`.
void* m61_calloc(size_t count, size_t sz, const char* file, int line) {
    if (is_overflowing(count, sz)) {
        gstats.fail_size += sz ;
        ++gstats.nfail;
        return nullptr;
    }

    void* ptr = m61_malloc(count * sz, file, line);
    if (ptr) {
        memset(ptr, 0, count * sz);
    }
    return ptr;
}

/// m61_get_statistics()
///    Return the current memory statistics.
m61_statistics m61_get_statistics() {
    return gstats;
}

/// m61_print_statistics()
///    Prints the current memory statistics.
void m61_print_statistics() {
    m61_statistics stats = m61_get_statistics();
    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

/// m61_print_leak_report()
///    Prints a report of all currently-active allocated blocks of dynamic memory.
void m61_print_leak_report() {
    header* p_header = head;
    // Traverse the linked list
    while (p_header) {
        // Print to stdout if the block is allocated
        if (p_header->p_status == ALLOCATED) {
            size_t payload_size = get_payload_size(p_header);
            fprintf(stdout, "LEAK CHECK: %s:%d: allocated object %p with size %zu\n", p_header->p_file, p_header->line,
                    p_header->p_payload, payload_size);
        }
        p_header = p_header->p_next;
    }
}

/// m61_realloc(ptr, sz, p_file, line)
///    Changes the size of the dynamic allocation pointed to by `ptr`
///    to hold at least `sz` bytes. If the existing allocation cannot be
///    enlarged, this function makes a new allocation, copies as much data
///    as possible from the old allocation to the new, and returns a pointer
///    to the new allocation. If `ptr` is `nullptr`, behaves like
///    `m61_malloc(sz, p_file, line). `sz` must not be 0. If a required
///    allocation fails, returns `nullptr` without freeing the original
///    block.
void* m61_realloc(void* ptr, size_t sz, const char* file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings

    if (!sz){
        return nullptr;
    }

    void* new_ptr = m61_malloc(sz, file, line);

    if (!ptr || !new_ptr) {
        return new_ptr;
    }

    // Retrieve the header pointer and payload size of the new block
    header* p_header = ((header*) new_ptr) - 1;
    size_t payload_size = get_payload_size(p_header);

    // Copy the whole payload if 'sz' is larger than the payload. Otherwise, copy only 'sz' bytes
    if (sz > payload_size) {
        memcpy(new_ptr, ptr, payload_size);
    } else {
        memcpy(new_ptr, ptr, sz);
    }

    m61_free(ptr, file, line);

    return new_ptr;
}