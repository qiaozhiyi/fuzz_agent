#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

void test_png(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return;
    }

    png_bytep image_data = NULL;
    png_bytep* row_pointers = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        free(row_pointers);
        free(image_data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int interlace_type = 0;
    int compression_type = 0;
    int filter_method = 0;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                 &interlace_type, &compression_type, &filter_method);

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    png_size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    const size_t max_image_bytes = 64 * 1024 * 1024;
    if (height == 0 || rowbytes == 0 || rowbytes > max_image_bytes / height) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return;
    }

    image_data = (png_bytep)malloc(rowbytes * height);
    row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    if (!image_data || !row_pointers) {
        free(row_pointers);
        free(image_data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return;
    }

    for (png_uint_32 y = 0; y < height; ++y) {
        row_pointers[y] = image_data + y * rowbytes;
    }

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, info_ptr);

    free(row_pointers);
    free(image_data);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return 1;
    }
    test_png(argv[1]);
    return 0;
}
