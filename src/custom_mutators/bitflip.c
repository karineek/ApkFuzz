#include <stdio.h>
#include "bitflip.h"
#include "cm_ApkVulFuzz.h"

bool mutateBinary(uint8_t *new_buf, my_mutator_t *data) {

    if (!new_buf || !data) {
        perror(">> (mutateBinary) Error: null pointer\n");
        return false;
    }

    uint64_t buf_size = data->buf_size;
    uint64_t start = data->i;
    uint64_t end = data->j;

    if (end <= start) {
        perror(">> (mutateBinary) Error: Need 0 <= start < end\n");
        return false;
    }

    if (end > buf_size) {
        perror(">> (mutateBinary) Error: Range end is past buffer size\n");
        return false;
    }

    //Initialise RNG once
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    int max_iterations = (rand() % (50)) + 1;
    uint64_t range_size = end - start;
    for (int n = 0; n < max_iterations; n++) {
        uint64_t off = start + (uint64_t)(rand() % range_size);
        int bit = rand() % 8;

        uint8_t mask = (uint8_t)(1u << bit);

        uint8_t old_byte = new_buf[off];
        uint8_t new_byte = (uint8_t)(old_byte ^ mask);
        new_buf[off] = new_byte;

        #ifdef TEST_CM
        printf(">> (mutateBinary) Flipped bit %d at byte offset %llu: 0x%02x -> 0x%02x\n",
               bit,
               (unsigned long long)off,
               old_byte,
               new_byte);
        #endif
    }
    return true;   
}
