#include <stdbool.h>    // bool, true, false
#include <assert.h>     // assert
#include <stddef.h>     // NULL, size_t
#include <string.h>     // memcpy
#include <stdlib.h>     // abort, malloc, free
#include <stdio.h>      // FILE, fopen, fclose, ftell, fseek, fread, ferror, fprintf, stderr,
                        // fflush, printf

#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "../lib/stb_image.h"

// Redefinition of typedefs is a C11 feature.
// This is the official™ guard, which is used across different headers to protect u8 and friends.
// (Or just add a #define before including this header, if you already have short names defined.)
#ifndef SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED
    #define SHORT_NAMES_FOR_PRIMITIVE_TYPES_WERE_DEFINED

    #include <stdint.h>
    #include <stddef.h>

    typedef uint8_t u8;
    typedef int8_t i8;
    typedef uint16_t u16;
    typedef int16_t i16;
    typedef uint32_t u32;
    typedef int32_t i32;
    typedef uint64_t u64;
    typedef int64_t i64;

    typedef uintptr_t uptr;
    typedef size_t usize;
    typedef ptrdiff_t isize;

    typedef float f32;
    typedef double f64;
#endif

#define sizeof(expression) (isize)sizeof(expression)

#if defined(__GCC__) || defined(__clang__)
    #define UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define UNREACHABLE() __assume(0)
#else
    #define UNREACHABLE() assert(0)
#endif

#define LOG_ERROR(...)                                                          \
    do {                                                                        \
        fprintf(stderr, "[ERROR] \"");                                          \
        fprintf(stderr, __VA_ARGS__);                                           \
        fprintf(stderr, "\" at %s (%s:%d)\n", __func__, __FILE__, __LINE__);    \
        fflush(stderr);                                                         \
    } while (0)

#define ARENA_DEFAULT_ALIGNMENT 16

typedef struct {
    u8 *begin;
    u8 *end;
} Arena;

void *arena_alloc_aligned(Arena *arena, isize size, isize alignment) {
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);

    if (size == 0) {
        return NULL;
    }

    isize padding = (isize)((~(uptr)arena->begin + 1) & ((uptr)alignment - 1));
    isize memory_left = arena->end - arena->begin - padding;
    if (memory_left < 0 || memory_left < size) {
        LOG_ERROR("Arena overflow.");
        abort();
    }

    void *ptr = arena->begin + padding;
    arena->begin += padding + size;
    return ptr;
}

void *arena_alloc(Arena *arena, isize size) {
    return arena_alloc_aligned(arena, size, ARENA_DEFAULT_ALIGNMENT);
}

void *arena_realloc(Arena *arena, void *old_ptr, isize old_size, isize new_size) {
    if (old_size >= new_size) {
        return old_ptr;
    }

    if (old_ptr != NULL && (u8 *)old_ptr + old_size == arena->begin) {
        arena->begin = old_ptr;
        return arena_alloc(arena, new_size);
    } else {
        void *new_ptr = arena_alloc(arena, new_size);
        memcpy(new_ptr, old_ptr, (size_t)old_size);
        return new_ptr;
    }
}

typedef struct {
    u8 *data;
    isize size;
} StringView;

// 1 byte:  0b00000000 .. 0b01111111 = 0x00 .. 0x7f
//
// (Codes from 0x20 to 0x7f are covered by 1 byte case, so 0xc0 or 0xc1 as first byte is invalid.)
// 2 bytes: 0b11000010 .. 0b11011111 = 0xc2 .. 0xdf
//
// 3 bytes: 0b11100000 .. 0b11101111 = 0xe0 .. 0xef
//
// (Codes greater than 0x10ffff are invalid.)
// 4 bytes: 0b11110000 .. 0b11110100 = 0xf0 .. 0xf4
static u8 utf8_char_size[] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // C
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // D
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // E
    4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F
};

// Unsafe function: passing invalid UTF-8 here will cause all sorts of UB.
StringView utf8_chop_char(StringView *string, u32 *char_code) {
    isize char_size = utf8_char_size[string->data[0]];

    switch (char_size) {
    case 1: {
        *char_code =
            (u32)(string->data[0]);
    } break;

    case 2: {
        *char_code =
            (u32)(string->data[0] & 0x1f) << 6 |
            (u32)(string->data[1] & 0x3f);
    } break;

    case 3: {
        *char_code =
            (u32)(string->data[0] & 0x0f) << 12 |
            (u32)(string->data[1] & 0x3f) << 6 |
            (u32)(string->data[2] & 0x3f);
    } break;

    case 4: {
        *char_code =
            (u32)(string->data[0] & 0x07) << 18 |
            (u32)(string->data[1] & 0x3f) << 12 |
            (u32)(string->data[2] & 0x3f) << 6 |
            (u32)(string->data[3] & 0x3f);
    } break;

    default: {
        UNREACHABLE();
    } break;
    }

    StringView result = {string->data, char_size};
    string->data += char_size;
    string->size -= char_size;
    return result;
}

bool utf8_validate(StringView string) {
    while (string.size > 0) {
        isize char_size = utf8_char_size[string.data[0]];
        if (char_size == 0 || string.size < char_size) {
            return false;
        }

        u32 char_code;
        utf8_chop_char(&string, &char_code);

        if (char_size == 3) {
            if (char_code < 0x0800 || char_code > 0xffff) {
                return false;
            }
            // Reserved for UTF-16 surrogate pairs.
            if (char_code >= 0xd800 && char_code <= 0xdfff) {
                return false;
            }
        }
        if (char_size == 4) {
            if (char_code < 0x10000 || char_code > 0x10ffff) {
                return false;
            }
        }
    }

    return true;
}

bool char_is_space(u32 char_code) {
    return
        char_code == 0x0020 ||  // Space
        char_code == 0x0009 ||  // Character Tabulation
        char_code == 0x000a ||  // End of Line
        char_code == 0x000c ||  // Form Feed
        char_code == 0x000d;    // Carriage Return
}

typedef struct {
    u8 *data;
    isize size;
    isize capacity;
} String;

static inline StringView as_string_view(String string) {
    return (StringView){string.data, string.size};
}

bool file_read_to_string(char const *file_path, String *string, Arena *arena) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) < 0) {
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) < 0) {
        fclose(file);
        return false;
    }

    if (string->size + file_size > string->capacity) {
        isize new_capacity = string->size + file_size;
        string->data = arena_realloc(arena, string->data, string->capacity, new_capacity);
        string->capacity = new_capacity;
    }

    fread(string->data + string->size, 1, (size_t)file_size, file);
    if (ferror(file) != 0) {
        fclose(file);
        return false;
    }
    string->size += file_size;

    fclose(file);
    return true;
}

#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 8

typedef struct {
    u32 char_code;
    char *char_data;
    u32 *bitmap;
} Glyph;

void glyphs_print(Glyph *glyphs, isize glyph_count) {
    Glyph *glyph_iter = glyphs;
    Glyph *glyphs_end = glyphs + glyph_count;

    while (glyph_iter < glyphs_end) {
        printf("'%s' (U+%x)\n", glyph_iter->char_data, glyph_iter->char_code);
        for (isize y = 0; y < GLYPH_HEIGHT; y += 1) {
            for (isize x = 0; x < GLYPH_WIDTH; x += 1) {
                u8 alpha = (glyph_iter->bitmap[y * GLYPH_WIDTH + x] >> 24) & 0xff;
                printf("%c ", alpha == 0x00 ? ' ' : '@');
            }
            printf("\n");
        }

        printf("\n");
        glyph_iter += 1;
    }
}

#define ARENA_CAPACITY (64 * 1024 * 1024)

int main(void) {
    u8 *arena_memory = malloc(ARENA_CAPACITY);
    if (arena_memory == NULL) {
        LOG_ERROR("Failed to allocate the arena.");
        return 1;
    }
    Arena arena = {arena_memory, arena_memory + ARENA_CAPACITY};

    String font_chars = {0};
    if (!file_read_to_string("./res/font8x8.txt", &font_chars, &arena)) {
        LOG_ERROR("Failed to load font chars from the file.");
        return 1;
    }
    if (!utf8_validate(as_string_view(font_chars))) {
        LOG_ERROR("Failed to load font chars due to invalid UTF-8.");
        return 1;
    }

    isize font_char_count = 0;
    {
        StringView font_char_iter = as_string_view(font_chars);
        while (font_char_iter.size > 0) {
            u32 char_code;
            utf8_chop_char(&font_char_iter, &char_code);
            if (!char_is_space(char_code)) {
                font_char_count += 1;
            }
        }
    }

    struct {
        int width;
        int height;
        int bytes_per_color;
        u8 *data;
    } font;

    font.data = stbi_load(
        "./res/font8x8.png",
        &font.width,
        &font.height,
        &font.bytes_per_color,
        0
    );
    if (font.data == NULL) {
        LOG_ERROR("Failed to load font glyphs from the file.");
        return 1;
    }
    if (font.width % GLYPH_WIDTH != 0 || font.height % GLYPH_HEIGHT != 0) {
        LOG_ERROR("Font bitmap dimensions are not divisble by the glyph dimensions.");
        return 1;
    }

    Glyph *glyphs = arena_alloc(&arena, font_char_count * sizeof(Glyph));
    Glyph *glyph_iter = glyphs;
    Glyph *glyphs_end = glyphs + font_char_count;

    StringView font_char_iter = as_string_view(font_chars);

    for (isize font_grid_y = 0; font_grid_y < font.height; font_grid_y += GLYPH_HEIGHT) {
        for (isize font_grid_x = 0; font_grid_x < font.width; font_grid_x += GLYPH_WIDTH) {
            isize font_data_index = font_grid_y * font.width + font_grid_x;

            bool glyph_is_empty = true;
            {
                u8 *glyph_line_start = &font.data[font_data_index * font.bytes_per_color];

                for (isize glyph_y = 0; glyph_is_empty && glyph_y < GLYPH_HEIGHT; glyph_y += 1) {
                    for (isize glyph_x = 0; glyph_is_empty && glyph_x < GLYPH_WIDTH; glyph_x += 1) {
                        u8 color = glyph_line_start[glyph_x * font.bytes_per_color];
                        if (color == 0x00) {
                            glyph_is_empty = false;
                        }
                    }

                    glyph_line_start += font.width * font.bytes_per_color;
                }
            }

            if (!glyph_is_empty) {
                if (glyph_iter == glyphs_end) {
                    LOG_ERROR("There are more glyphs in the bitmap than chars in the text file.");
                    return 1;
                }

                u32 char_code;
                StringView char_data;
                do {
                    char_data = utf8_chop_char(&font_char_iter, &char_code);
                } while (char_is_space(char_code));

                glyph_iter->char_code = char_code;

                glyph_iter->char_data = arena_alloc_aligned(&arena, char_data.size + 1, 1);
                memcpy(glyph_iter->char_data, char_data.data, (size_t)char_data.size);
                glyph_iter->char_data[char_data.size] = 0;

                glyph_iter->bitmap =
                    arena_alloc_aligned(&arena, GLYPH_WIDTH * GLYPH_HEIGHT * sizeof(u32), 4);

                u8 *glyph_line_start = &font.data[font_data_index * font.bytes_per_color];

                for (isize glyph_y = 0; glyph_y < GLYPH_HEIGHT; glyph_y += 1) {
                    for (isize glyph_x = 0; glyph_x < GLYPH_WIDTH; glyph_x += 1) {
                        u8 color = glyph_line_start[glyph_x * font.bytes_per_color];

                        if (color == 0x00) {
                            glyph_iter->bitmap[glyph_y * GLYPH_WIDTH + glyph_x] = 0xff000000;
                        } else {
                            glyph_iter->bitmap[glyph_y * GLYPH_WIDTH + glyph_x] = 0x00000000;
                        }
                    }

                    glyph_line_start += font.width * font.bytes_per_color;
                }

                glyph_iter += 1;
            }
        }
    }

    if (glyph_iter != glyphs_end) {
        LOG_ERROR("There are more chars in the text file than glyphs in the bitmap.");
        return 1;
    }

    glyphs_print(glyphs, glyphs_end - glyphs);

    return 0;
}
