/*
 * experiments/targets/libxml2/harness.c
 *
 * libxml2 parser fuzz harness. Persistent mode when built with
 * afl-clang-fast; file/stdin fallback otherwise.
 *
 * Mirrors experiments/targets/cjson/harness.c in shape so the
 * controller's recipe-store + plateau + inline-agent paths exercise
 * a target whose coverage growth is slow enough to actually plateau
 * within a reasonable budget (cjson plateaus in seconds, which is
 * why the W1b regression silently masquerades as success there).
 *
 * Build:
 *   afl-clang-fast harness.c \
 *     -I build/install/include/libxml2 \
 *     build/install/lib/libxml2.a \
 *     -O1 -g -lm -liconv -o libxml2_fuzzer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>

#ifdef __AFL_HAVE_MANUAL_CONTROL
__AFL_FUZZ_INIT();
#endif

#define MAX_INPUT_LEN (1u << 20)

static void silent_error(void *ctx, const char *msg, ...) { (void)ctx; (void)msg; }

static void run_once(const unsigned char *data, size_t len) {
    if (len == 0 || len > INT_MAX) return;
    xmlDocPtr doc = xmlReadMemory(
        (const char *)data, (int)len,
        "fuzz.xml", NULL,
        XML_PARSE_RECOVER | XML_PARSE_NOENT | XML_PARSE_NONET);
    if (doc) xmlFreeDoc(doc);
}

#ifdef __AFL_HAVE_MANUAL_CONTROL
int main(void) {
    xmlInitParser();
    xmlSetGenericErrorFunc(NULL, silent_error);

    __AFL_INIT();
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;
        if (len < 0) continue;
        if ((size_t)len > MAX_INPUT_LEN) len = MAX_INPUT_LEN;
        run_once(buf, (size_t)len);
    }

    xmlCleanupParser();
    return 0;
}
#else
int main(int argc, char **argv) {
    xmlInitParser();
    xmlSetGenericErrorFunc(NULL, silent_error);

    FILE *fp = (argc > 1) ? fopen(argv[1], "rb") : stdin;
    if (!fp) { perror("fopen"); xmlCleanupParser(); return 1; }

    unsigned char *buf = (unsigned char *)malloc(MAX_INPUT_LEN);
    if (!buf) { if (fp != stdin) fclose(fp); xmlCleanupParser(); return 1; }

    size_t n = fread(buf, 1, MAX_INPUT_LEN, fp);
    run_once(buf, n);

    free(buf);
    if (fp != stdin) fclose(fp);
    xmlCleanupParser();
    return 0;
}
#endif
