/*
 * experiments/targets/cjson/harness.c
 *
 * cJSON parser fuzz harness. Persistent mode when built with afl-clang-fast,
 * file/stdin fallback otherwise (works under qemu, regular clang, and
 * crash-replay tools).
 *
 * Build:
 *   afl-clang-fast harness.c src/cJSON.c -I src -O1 -g -o cjson_fuzzer
 *
 * Run:
 *   afl-fuzz -i seeds -o out -- ./cjson_fuzzer @@
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "cJSON.h"

#ifdef __AFL_HAVE_MANUAL_CONTROL
#  include <unistd.h>
__AFL_FUZZ_INIT();
#endif

/* Maximum input size for the persistent fuzz loop. AFL++ shared-memory
 * fuzzing wants a fixed upper bound; we copy into a NUL-terminated buffer. */
#define MAX_INPUT_LEN (1u << 20)  /* 1 MiB */

static void run_once(const unsigned char *data, size_t len) {
    if (len == 0) return;

    /* cJSON_Parse expects a NUL-terminated string. Copy into a fresh
     * buffer to avoid relying on the caller's framing. */
    char *buf = (char *)malloc(len + 1);
    if (!buf) return;
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json) {
        char *rendered = cJSON_Print(json);
        if (rendered) free(rendered);
        cJSON_Delete(json);
    }
    free(buf);
}

static int read_file(const char *path, unsigned char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    if ((size_t)n > MAX_INPUT_LEN) n = MAX_INPUT_LEN;
    unsigned char *buf = (unsigned char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *out = buf;
    *out_len = got;
    return 0;
}

static int read_stdin(unsigned char **out, size_t *out_len) {
    unsigned char *buf = (unsigned char *)malloc(MAX_INPUT_LEN);
    if (!buf) return -1;
    size_t total = 0;
    while (total < MAX_INPUT_LEN) {
        ssize_t r = read(0, buf + total, MAX_INPUT_LEN - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    *out = buf;
    *out_len = total;
    return 0;
}

int main(int argc, char **argv) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    /* Persistent mode: AFL++ keeps the process alive across many inputs.
     * __AFL_FUZZ_TESTCASE_BUF / _LEN are populated each loop iteration. */
    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        size_t len = (size_t)__AFL_FUZZ_TESTCASE_LEN;
        if (len > MAX_INPUT_LEN) len = MAX_INPUT_LEN;
        run_once(buf, len);
    }
    return 0;
#else
    /* Fallback: file path on argv, or stdin. */
    unsigned char *buf = NULL;
    size_t len = 0;
    if (argc >= 2) {
        if (read_file(argv[1], &buf, &len) != 0) return 1;
    } else {
        if (read_stdin(&buf, &len) != 0) return 1;
    }
    run_once(buf, len);
    free(buf);
    return 0;
#endif
}
