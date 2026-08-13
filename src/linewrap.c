#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H


#define FONT_SIZE 24
#define LINE_WIDTH 700.0f
#define MIN_SPACE 15.0f

#define PARAGRAPH_SPACING 16.0f

/*
 * Very small penalty for breaking at an explicit hyphen.
 *
 * A hyphenated break is therefore preferred only when it improves
 * the actual spacing enough to justify using it.
 */
#define HYPHEN_SPLIT_PENALTY 0.1f
#define SPARSE_PENALITY 0.005f

#define INITIAL_PART_CAPACITY 2048
#define INITIAL_PARAGRAPH_CAPACITY 128
#define INPUT_CHUNK_SIZE 65536

#define EPSILON 0.00001f


static int debug_enabled = 0;


typedef struct {
    size_t start;
    size_t end;
    float width;
    int join_next;
} Part;


typedef struct {
    size_t start;
    size_t end;
} Paragraph;


static void debug_log(const char *fmt, ...)
{
    if (!debug_enabled)
        return;

    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}


static inline int is_word_delimiter(unsigned char c)
{
    return c == ' ' ||
           c == '\t' ||
           c == '\r' ||
           c == '\n';
}


static char *read_file(FILE *file, size_t *out_size)
{
    size_t size = 0;
    size_t capacity = INPUT_CHUNK_SIZE;

    char *buffer = malloc(capacity + 1);

    if (!buffer)
        return NULL;

    for (;;) {
        if (size + INPUT_CHUNK_SIZE + 1 > capacity) {
            size_t new_capacity = capacity * 2;

            while (
                new_capacity <
                size + INPUT_CHUNK_SIZE + 1
            ) {
                new_capacity *= 2;
            }

            char *tmp = realloc(
                buffer,
                new_capacity + 1
            );

            if (!tmp) {
                free(buffer);
                return NULL;
            }

            buffer = tmp;
            capacity = new_capacity;
        }

        size_t n = fread(
            buffer + size,
            1,
            INPUT_CHUNK_SIZE,
            file
        );

        size += n;

        if (n != INPUT_CHUNK_SIZE) {
            if (ferror(file)) {
                free(buffer);
                return NULL;
            }

            break;
        }
    }

    buffer[size] = '\0';
    *out_size = size;

    return buffer;
}


static int append_part(
    Part **parts,
    size_t *count,
    size_t *capacity,
    Part part
)
{
    if (*count == *capacity) {
        size_t new_capacity = *capacity * 2;

        Part *tmp = realloc(
            *parts,
            new_capacity * sizeof(**parts)
        );

        if (!tmp)
            return 0;

        *parts = tmp;
        *capacity = new_capacity;
    }

    (*parts)[(*count)++] = part;
    return 1;
}


static int parse_word(
    const char *text,
    size_t start,
    size_t end,
    Part **parts,
    size_t *part_count,
    size_t *part_capacity,
    const float *glyph_widths
)
{
    size_t piece_start = start;

    for (size_t i = start; i < end; ++i) {
        if (text[i] != '-')
            continue;

        size_t piece_end = i + 1;

        float width = 0.0f;

        for (
            size_t j = piece_start;
            j < piece_end;
            ++j
        ) {
            width += glyph_widths[
                (unsigned char)text[j]
            ];
        }

        if (!append_part(
                parts,
                part_count,
                part_capacity,
                (Part) {
                    .start = piece_start,
                    .end = piece_end,
                    .width = width,
                    .join_next = 1
                }
            )) {
            return 0;
        }

        piece_start = piece_end;
    }

    if (piece_start < end) {
        float width = 0.0f;

        for (
            size_t i = piece_start;
            i < end;
            ++i
        ) {
            width += glyph_widths[
                (unsigned char)text[i]
            ];
        }

        if (!append_part(
                parts,
                part_count,
                part_capacity,
                (Part) {
                    .start = piece_start,
                    .end = end,
                    .width = width,
                    .join_next = 0
                }
            )) {
            return 0;
        }
    }

    return 1;
}


static int parse_text(
    const char *text,
    Part **out_parts,
    size_t *out_part_count,
    Paragraph **out_paragraphs,
    size_t *out_paragraph_count,
    const float *glyph_widths
)
{
    size_t part_capacity =
        INITIAL_PART_CAPACITY;

    size_t paragraph_capacity =
        INITIAL_PARAGRAPH_CAPACITY;

    size_t part_count = 0;
    size_t paragraph_count = 0;

    Part *parts = malloc(
        part_capacity * sizeof(*parts)
    );

    Paragraph *paragraphs = malloc(
        paragraph_capacity *
        sizeof(*paragraphs)
    );

    if (!parts || !paragraphs) {
        free(parts);
        free(paragraphs);
        return 0;
    }

    size_t i = 0;
    size_t paragraph_start = 0;
    int have_parts = 0;

    while (text[i] != '\0') {
        if (text[i] == '\n') {
            size_t newline_count = 0;

            while (text[i] == '\n') {
                ++newline_count;
                ++i;
            }

            if (
                newline_count >= 2 &&
                have_parts
            ) {
                if (
                    paragraph_count ==
                    paragraph_capacity
                ) {
                    paragraph_capacity *= 2;

                    Paragraph *tmp =
                        realloc(
                            paragraphs,
                            paragraph_capacity *
                            sizeof(*paragraphs)
                        );

                    if (!tmp) {
                        free(parts);
                        free(paragraphs);
                        return 0;
                    }

                    paragraphs = tmp;
                }

                paragraphs[
                    paragraph_count++
                ] = (Paragraph) {
                    .start = paragraph_start,
                    .end = part_count
                };

                paragraph_start = part_count;
                have_parts = 0;
            }

            continue;
        }

        while (
            text[i] == ' ' ||
            text[i] == '\t' ||
            text[i] == '\r'
        ) {
            ++i;
        }

        if (text[i] == '\0')
            break;

        if (text[i] == '\n')
            continue;

        size_t start = i;

        while (
            text[i] != '\0' &&
            !is_word_delimiter(
                (unsigned char)text[i]
            )
        ) {
            ++i;
        }

        size_t end = i;

        if (!parse_word(
                text,
                start,
                end,
                &parts,
                &part_count,
                &part_capacity,
                glyph_widths
            )) {
            free(parts);
            free(paragraphs);
            return 0;
        }

        have_parts = 1;
    }

    if (have_parts) {
        if (
            paragraph_count ==
            paragraph_capacity
        ) {
            paragraph_capacity *= 2;

            Paragraph *tmp =
                realloc(
                    paragraphs,
                    paragraph_capacity *
                    sizeof(*paragraphs)
                );

            if (!tmp) {
                free(parts);
                free(paragraphs);
                return 0;
            }

            paragraphs = tmp;
        }

        paragraphs[
            paragraph_count++
        ] = (Paragraph) {
            .start = paragraph_start,
            .end = part_count
        };
    }

    *out_parts = parts;
    *out_part_count = part_count;

    *out_paragraphs = paragraphs;
    *out_paragraph_count = paragraph_count;

    return 1;
}


static inline size_t line_gap_count(
    const Part *parts,
    size_t start,
    size_t end
)
{
    if (end - start <= 1)
        return 0;

    size_t gaps = 0;

    for (
        size_t i = start;
        i + 1 < end;
        ++i
    ) {
        if (!parts[i].join_next)
            ++gaps;
    }

    return gaps;
}


static inline int is_hyphen_break(
    const Part *parts,
    size_t end,
    size_t paragraph_end
)
{
    if (end >= paragraph_end)
        return 0;

    if (end == 0)
        return 0;

    return parts[end - 1].join_next;
}


/*
 * The important change is the `final_line` argument.
 *
 * A zero-gap line is a single lexical word. It is invalid unless
 * it is the final line of the paragraph.
 */
static inline float line_cost(
    const Part *parts,
    size_t start,
    size_t end,
    size_t paragraph_end,
    float normal_space,
    float min_space
)
{
    size_t count = end - start;

    if (count == 0)
        return INFINITY;

    float word_width = 0.0f;

    for (
        size_t i = start;
        i < end;
        ++i
    ) {
        word_width += parts[i].width;
    }

    if (word_width > LINE_WIDTH)
        return INFINITY;

    size_t gaps =
        line_gap_count(
            parts,
            start,
            end
        );

    /*
     * No inter-word gaps means this is one lexical word,
     * even if it contains hyphenated pieces.
     *
     * A non-final one-word line should NEVER exist.
     */
    if (gaps == 0) {
        int final_line =
            end == paragraph_end;

        if (!final_line)
            return INFINITY;

        /*
         * Only the final line is allowed to be one word.
         *
         * Under your exact-width requirement, the remaining
         * width has to become leading spacing.
         */
        return LINE_WIDTH - word_width;
    }

    float spacing =
        (
            LINE_WIDTH -
            word_width
        ) / (float)gaps;

    if (spacing < min_space)
        return INFINITY;

    float cost =
        fabsf(
            spacing -
            normal_space
        ) * (float)gaps;

    /*
     * Penalize extreme expansion, preserving your existing
     * SPARSE_PENALITY behavior.
     */
    float extra_spacing =
        spacing - normal_space;

    if (extra_spacing > 0.0f) {
        cost +=
            extra_spacing *
            SPARSE_PENALITY;
    }

    /*
     * Tiny penalty for using an explicit hyphen breakpoint.
     */
    if (
        is_hyphen_break(
            parts,
            end,
            paragraph_end
        )
    ) {
        cost +=
            HYPHEN_SPLIT_PENALTY;
    }

    return cost;
}


static int optimize_paragraph(
    const Part *parts,
    size_t start,
    size_t end,
    float normal_space,
    float min_space,
    size_t **out_parent
)
{
    size_t n = end - start;

    if (n == 0)
        return 0;

    float *prefix =
        malloc(
            (n + 1) *
            sizeof(*prefix)
        );

    float *dp =
        malloc(
            (n + 1) *
            sizeof(*dp)
        );

    size_t *parent =
        malloc(
            (n + 1) *
            sizeof(*parent)
        );

    if (
        !prefix ||
        !dp ||
        !parent
    ) {
        free(prefix);
        free(dp);
        free(parent);
        return 0;
    }

    prefix[0] = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        prefix[i + 1] =
            prefix[i] +
            parts[start + i].width;
    }

    dp[0] = 0.0f;
    parent[0] = SIZE_MAX;

    for (size_t i = 1; i <= n; ++i) {
        dp[i] = INFINITY;
        parent[i] = SIZE_MAX;
    }

    debug_log(
        "\n=== PARAGRAPH [%zu, %zu) "
        "(%zu parts) ===\n",
        start,
        end,
        n
    );

    for (
        size_t end_pos = 1;
        end_pos <= n;
        ++end_pos
    ) {
        float best_cost = INFINITY;
        size_t best_parent = SIZE_MAX;

        for (
            size_t begin_pos = end_pos;
            begin_pos-- > 0;
        ) {
            float width =
                prefix[end_pos] -
                prefix[begin_pos];

            if (width > LINE_WIDTH)
                break;

            if (!isfinite(dp[begin_pos]))
                continue;

            float cost =
                line_cost(
                    parts,
                    start + begin_pos,
                    start + end_pos,
                    end,
                    normal_space,
                    min_space
                );

            if (!isfinite(cost))
                continue;

            float total =
                dp[begin_pos] +
                cost;

            if (
                total <
                best_cost - EPSILON
            ) {
                best_cost = total;
                best_parent = begin_pos;

            } else if (
                fabsf(
                    total -
                    best_cost
                ) <= EPSILON &&
                (
                    best_parent == SIZE_MAX ||
                    begin_pos < best_parent
                )
            ) {
                best_parent = begin_pos;
            }
        }

        dp[end_pos] = best_cost;
        parent[end_pos] = best_parent;

        if (debug_enabled) {
            if (best_parent != SIZE_MAX) {
                size_t count =
                    end_pos - best_parent;

                float width =
                    prefix[end_pos] -
                    prefix[best_parent];

                float cost =
                    line_cost(
                        parts,
                        start + best_parent,
                        start + end_pos,
                        end,
                        normal_space,
                        min_space
                    );

                int hyphen =
                    is_hyphen_break(
                        parts,
                        start + end_pos,
                        end
                    );

                debug_log(
                    "DP[%zu]: parent=%zu "
                    "count=%zu width=%.3f "
                    "cost=%.3f total=%.3f"
                    "%s\n",
                    end_pos,
                    best_parent,
                    count,
                    width,
                    cost,
                    dp[end_pos],
                    hyphen
                        ? " [HYPHEN BREAK]"
                        : ""
                );
            } else {
                debug_log(
                    "DP[%zu]: NO SOLUTION\n",
                    end_pos
                );
            }
        }
    }

    if (parent[n] == SIZE_MAX) {
        fprintf(
            stderr,
            "failed to optimize paragraph\n"
        );

        free(prefix);
        free(dp);
        free(parent);

        return 0;
    }

    debug_log(
        "FINAL COST: %.6f\n",
        dp[n]
    );

    free(prefix);
    free(dp);

    *out_parent = parent;

    return 1;
}


static void print_paragraph(
    const char *text,
    const Part *parts,
    size_t start,
    size_t end,
    const size_t *parent,
    float normal_space,
    int paragraph_start
)
{
    size_t n = end - start;

    if (n == 0)
        return;

    size_t line_count = 0;

    for (
        size_t pos = n;
        pos != 0;
        pos = parent[pos]
    ) {
        ++line_count;
    }

    size_t *breaks =
        malloc(
            (line_count + 1) *
            sizeof(*breaks)
        );

    if (!breaks) {
        fprintf(
            stderr,
            "failed to allocate line breaks\n"
        );
        return;
    }

    size_t pos = n;

    breaks[line_count] = n;

    for (
        size_t i = line_count;
        i > 0;
        --i
    ) {
        pos = parent[pos];
        breaks[i - 1] = pos;
    }

    for (
        size_t line = 0;
        line < line_count;
        ++line
    ) {
        size_t local_start =
            breaks[line];

        size_t local_end =
            breaks[line + 1];

        size_t global_start =
            start + local_start;

        size_t global_end =
            start + local_end;

        size_t count =
            global_end -
            global_start;

        float word_width = 0.0f;

        for (
            size_t i = global_start;
            i < global_end;
            ++i
        ) {
            word_width +=
                parts[i].width;
        }

        size_t gaps =
            line_gap_count(
                parts,
                global_start,
                global_end
            );

        if (
            paragraph_start &&
            line == 0
        ) {
            printf(
                "<%.3f>",
                PARAGRAPH_SPACING
            );
        }

        /*
         * Only the final line may have zero gaps.
         */
        if (gaps == 0) {
            float leading =
                LINE_WIDTH -
                word_width;

            printf(
                "<%.3f>",
                leading
            );

            for (
                size_t i = global_start;
                i < global_end;
                ++i
            ) {
                fwrite(
                    text + parts[i].start,
                    1,
                    parts[i].end -
                    parts[i].start,
                    stdout
                );
            }

            putchar('\n');

            debug_log(
                "OUTPUT LINE %zu: "
                "final single-word line "
                "leading=%.3f\n",
                line,
                leading
            );

            continue;
        }

        float spacing =
            (
                LINE_WIDTH -
                word_width
            ) / (float)gaps;

        debug_log(
            "OUTPUT LINE %zu: "
            "count=%zu parts_width=%.3f "
            "gaps=%zu spacing=%.6f "
            "total=%.6f\n",
            line,
            count,
            word_width,
            gaps,
            spacing,
            word_width +
            spacing * (float)gaps
        );

        for (
            size_t i = global_start;
            i < global_end;
            ++i
        ) {
            fwrite(
                text + parts[i].start,
                1,
                parts[i].end -
                parts[i].start,
                stdout
            );

            if (i + 1 >= global_end)
                continue;

            /*
             * Hyphenated pieces remain adjacent.
             */
            if (!parts[i].join_next) {
                printf(
                    "<%.3f> ",
                    spacing
                );
            }
        }

        putchar('\n');
    }

    free(breaks);

    (void)normal_space;
}


static void justify_text(
    const char *text,
    FT_Face face
)
{
    float glyph_widths[256];

    for (int c = 0; c < 256; ++c) {
        FT_UInt glyph =
            FT_Get_Char_Index(
                face,
                (FT_ULong)c
            );

        FT_Fixed advance = 0;

        if (
            FT_Get_Advance(
                face,
                glyph,
                FT_LOAD_DEFAULT,
                &advance
            ) != 0
        ) {
            glyph_widths[c] = 0.0f;
        } else {
            glyph_widths[c] =
                (float)advance /
                65536.0f;
        }
    }

    float normal_space =
        glyph_widths[
            (unsigned char)' '
        ];

    Part *parts = NULL;
    Paragraph *paragraphs = NULL;

    size_t part_count = 0;
    size_t paragraph_count = 0;

    if (
        !parse_text(
            text,
            &parts,
            &part_count,
            &paragraphs,
            &paragraph_count,
            glyph_widths
        )
    ) {
        fprintf(
            stderr,
            "failed to parse input\n"
        );
        return;
    }

    debug_log(
        "FONT SPACE: %.3f\n",
        normal_space
    );

    debug_log(
        "LINE WIDTH: %.3f\n",
        LINE_WIDTH
    );

    debug_log(
        "PARSED %zu parts, %zu paragraphs\n",
        part_count,
        paragraph_count
    );

    for (
        size_t p = 0;
        p < paragraph_count;
        ++p
    ) {
        Paragraph paragraph =
            paragraphs[p];

        debug_log(
            "\nPROCESSING PARAGRAPH %zu: "
            "[%zu, %zu)\n",
            p,
            paragraph.start,
            paragraph.end
        );

        size_t *parent = NULL;

        if (
            !optimize_paragraph(
                parts,
                paragraph.start,
                paragraph.end,
                normal_space,
                MIN_SPACE,
                &parent
            )
        ) {
            free(parts);
            free(paragraphs);
            return;
        }

        print_paragraph(
            text,
            parts,
            paragraph.start,
            paragraph.end,
            parent,
            normal_space,
            p != 0
        );

        free(parent);
    }

    free(parts);
    free(paragraphs);
}


static int process_file(
    const char *filename,
    FT_Face face
)
{
    FILE *file =
        fopen(
            filename,
            "rb"
        );

    if (!file) {
        fprintf(
            stderr,
            "cannot open '%s': %s\n",
            filename,
            strerror(errno)
        );
        return 0;
    }

    size_t size = 0;

    char *text =
        read_file(
            file,
            &size
        );

    fclose(file);

    if (!text) {
        fprintf(
            stderr,
            "failed to read '%s'\n",
            filename
        );
        return 0;
    }

    debug_log(
        "\n=== FILE: %s (%zu bytes) ===\n",
        filename,
        size
    );

    justify_text(
        text,
        face
    );

    free(text);

    return 1;
}


int main(int argc, char **argv)
{
    char **input_files =
        malloc(
            (size_t)argc *
            sizeof(*input_files)
        );

    if (!input_files) {
        fprintf(
            stderr,
            "failed to allocate argument array\n"
        );
        return 1;
    }

    int input_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (
            strcmp(
                argv[i],
                "--debug"
            ) == 0
        ) {
            debug_enabled = 1;
        } else {
            input_files[input_count++] =
                argv[i];
        }
    }

    FT_Library library;
    FT_Face face;

    if (
        FT_Init_FreeType(
            &library
        )
    ) {
        fprintf(
            stderr,
            "FT_Init_FreeType failed\n"
        );

        free(input_files);
        return 1;
    }

    if (
        FT_New_Face(
            library,
            "font.ttf",
            0,
            &face
        )
    ) {
        fprintf(
            stderr,
            "FT_New_Face failed\n"
        );

        FT_Done_FreeType(library);
        free(input_files);

        return 1;
    }

    if (
        FT_Set_Pixel_Sizes(
            face,
            0,
            FONT_SIZE
        )
    ) {
        fprintf(
            stderr,
            "FT_Set_Pixel_Sizes failed\n"
        );

        FT_Done_Face(face);
        FT_Done_FreeType(library);
        free(input_files);

        return 1;
    }

    if (input_count == 0) {
        size_t size = 0;

        char *text =
            read_file(
                stdin,
                &size
            );

        if (!text) {
            fprintf(
                stderr,
                "failed to read stdin\n"
            );

            FT_Done_Face(face);
            FT_Done_FreeType(library);
            free(input_files);

            return 1;
        }

        debug_log(
            "=== STDIN (%zu bytes) ===\n",
            size
        );

        justify_text(
            text,
            face
        );

        free(text);
    } else {
        int processed = 0;

        for (
            int i = 0;
            i < input_count;
            ++i
        ) {
            const char *filename =
                input_files[i];

            FILE *test =
                fopen(
                    filename,
                    "rb"
                );

            if (!test) {
                fprintf(
                    stderr,
                    "cannot open '%s': %s\n",
                    filename,
                    strerror(errno)
                );
                continue;
            }

            fclose(test);

            if (processed) {
                printf(
                    "\n\n----------%s----------\n\n",
                    filename
                );
            }

            if (
                process_file(
                    filename,
                    face
                )
            ) {
                ++processed;
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    free(input_files);

    return 0;
}
