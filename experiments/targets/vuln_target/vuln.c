#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

void process_packet(const uint8_t *buf, size_t size) {
    if (size < 9) return;

    // Gate 1: Magic
    if (strncmp((char*)buf, "FP93", 4) != 0) return;

    // Gate 2: Length
    uint32_t claimed_len = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                           ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
    uint32_t actual_len = (uint32_t)(size - 9);
    if (claimed_len != actual_len) {
        printf("Gate 2 Failed: claimed %u, actual %u\n", claimed_len, actual_len);
        return;
    }
    printf("Gate 2 Passed\n");

    // Gate 3: Checksum
    uint8_t expected_sum = buf[8];
    uint8_t actual_sum = 0;
    for (size_t i = 0; i < size; i++) {
        if (i == 8) continue;
        actual_sum ^= buf[i];
    }

    if (expected_sum != actual_sum) {
        printf("Gate 3 Failed: expected %02X, actual %02X\n", expected_sum, actual_sum);
        return;
    }
    printf("Gate 3 Passed! Triggering crash with len %u\n", actual_len);

    if (actual_len > 10) {
        char target[8];
        memcpy(target, buf + 9, actual_len);
        printf("You shouldn't see this if it crashed\n");
    }
}

int main() {
    uint8_t buf[1024];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n > 0) process_packet(buf, n);
    return 0;
}
