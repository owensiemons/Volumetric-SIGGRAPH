#include "common.hpp"

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}


// based off of uselibpng.c from CS 418
void save_image(const char* filename, int width, int height, std::vector<unsigned short>& pixels) {
    png_structp ps = nullptr;
    png_infop pi = nullptr;
    FILE* out = nullptr;

    ps = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!ps) goto fail1;
    pi = png_create_info_struct(ps);
    if (!pi) goto fail2;

    out = fopen(filename, "wb");
    if (!out) goto fail2;
    png_init_io(ps, out);

    png_set_IHDR(ps, pi, width, height, 16, 
        PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(ps, pi);

    png_set_packing(ps);

    png_set_swap(ps);
    
    
    for(int i = height - 1; i >= 0; i--) {
        std::span<unsigned short> row = std::span(pixels).subspan(width * i * 4, width * 4);
        png_write_row(ps, reinterpret_cast<png_const_bytep>(row.data()));

    }


    png_write_end(ps, NULL);
    fclose(out);

    fail2: png_destroy_write_struct(&ps, &pi);
    fail1: return;
}