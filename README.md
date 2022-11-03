CS 61 Problem Set 1
===================

**Fill out both this p_file and `AUTHORS.md` before submitting.** We grade
anonymously, so put all personally identifying information, including
collaborators and citations, in `AUTHORS.md`.

Grading notes (if any)
----------------------
To implement internal metadata I used a struct called 'header' which includes the following members:

size_t block_size:  size of header + p_payload + padding

char* p_payload:  pointer for the payload

char* p_end_marker: pointer for the end marker

char* p_status: identifier indicating whether the block is FREE or ALLOCATED

const char* p_file: source code file where the allocation/free request was made

int line: source code line where the allocation/free request was made

struct header* p_next: header pointer for the next block of memory

struct header* p_prev: header pointer for the previous block of memory



Extra credit attempted (if any)
-------------------------------
I implemented m61_realloc and wrote tests (test52 through 57) to validate its correctness.