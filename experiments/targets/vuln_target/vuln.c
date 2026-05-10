#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parse_input(const char* data, size_t size) {
    if (size < 4) return;

    if (strncmp(data, "FUZZ", 4) == 0) {
        if (size > 10 && strncmp(data + 4, "MAGIC", 5) == 0) {
            // Stack Buffer Overflow
            char buf[16];
            // Deliberately cause a crash if payload is large
            strcpy(buf, data + 9); 
            printf("Buf: %s\n", buf);
        }
    } else if (strncmp(data, "HEAP", 4) == 0) {
        if (size > 8) {
            int len = data[4];
            if (len > 0) {
                // Heap Buffer Overflow
                char* heap_buf = (char*)malloc(10);
                memcpy(heap_buf, data + 5, len); // len could be > 10, causing heap corruption
                printf("Heap: %s\n", heap_buf);
                free(heap_buf);
            }
        }
    } else if (strncmp(data, "NULL", 4) == 0) {
        if (size > 5 && data[4] == 'X') {
            // Null Pointer Deref
            volatile int* ptr = NULL;
            *ptr = 1;
        }
    } else if (strncmp(data, "ZERO", 4) == 0) {
        if (size > 5) {
            // Divide by Zero
            int val = data[4] - '0'; // If data[4] == '0', val is 0
            int res = 100 / val;
            printf("Res: %d\n", res);
        }
    } else if (strncmp(data, "OOBR", 4) == 0) {
        if (size > 8) {
            // Out of bounds read causing crash
            int index = data[4] * 10000;
            printf("Val: %c\n", data[index]); // Crash on unmapped memory
        }
    }
}

int main(int argc, char** argv) {
    char data[1024];
    size_t size;

    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) return 1;
        size = fread(data, 1, sizeof(data) - 1, f);
        fclose(f);
    } else {
        size = fread(data, 1, sizeof(data) - 1, stdin);
    }
    
    data[size] = '\0';
    parse_input(data, size);

    return 0;
}
