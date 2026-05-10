#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = (char*)malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);

    cJSON* json = cJSON_Parse(data);
    if (json) {
        char* rendered = cJSON_Print(json);
        if (rendered) free(rendered);
        cJSON_Delete(json);
    }

    free(data);
    return 0;
}
