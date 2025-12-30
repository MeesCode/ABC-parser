#include <stdio.h>
#include "abc_parser.h"

// ============================================================================
// Pre-allocated memory - adjust ABC_MAX_NOTES in abc_parser.h for your needs
// ============================================================================

static NotePool g_note_pool;
static struct sheet g_sheet;

static const char *music =
    "T:Super Mario Bros Theme\n"
    "X:1\n"
    "M:4/4\n"
    "L:1/8\n"
    "K:G\n"
    "Q:1/4=105\n"
    "e/2eec/2e g/2z3z/2|c/2zG/2 zE/2zAB^A/2=A| Geg a=f/2gec/2 d/2B/2z|c/2zG/2 zE/2zAB^A/2=A|\n"
    "Geg a=f/2gec/2 d/2B/2z|zg/2^f/2 =f/2^de^G/2A/2cA/2c/2=d/2|zg/2^f/2 =f/2^dec'c'/2 c'/2z3/2|zg/2^f/2 =f/2^de^G/2A/2cA/2c/2=d/2|\n"
    "z^d/2z=d/2z c/2z3z/2|\n";

int main(void) {
    printf("ABC Music Parser (Embedded Version)\n");
    printf("====================================\n\n");

    note_pool_init(&g_note_pool);
    sheet_init(&g_sheet, &g_note_pool);

    printf("Memory footprint:\n");
    printf("  Note struct:  %3zu bytes\n", sizeof(struct note));
    printf("  Sheet struct: %3zu bytes\n", sizeof(struct sheet));
    printf("  Note pool:    %3zu bytes (%u notes)\n",
           sizeof(g_note_pool), g_note_pool.capacity);
    printf("  Total static: %3zu bytes\n\n",
           sizeof(g_note_pool) + sizeof(g_sheet));

    int result = abc_parse(&g_sheet, music);

    if (result < 0) {
        printf("Error: parse failed (%d)\n", result);
        if (result == -2) printf("  Pool exhausted!\n");
        return 1;
    }

    sheet_print(&g_sheet);

    printf("\nFirst 100 notes:\n");
    struct note *n = sheet_first_note(&g_sheet);
    for (int i = 0; n && i < 100; i++) {
        if (n->note_name != NOTE_REST) {
            printf("  %d: %s%s%u @ %.1f Hz, %u ms\n",
                   i + 1,
                   note_name_to_string((NoteName)n->note_name),
                   accidental_to_string(n->accidental),
                   n->octave,
                   n->frequency_x10 / 10.0f,
                   n->duration_ms);
        } else {
            printf("  %d: REST %u ms\n", i + 1, n->duration_ms);
        }
        n = note_next(&g_sheet, n);
    }

    printf("\n--- Memory reuse test ---\n");
    sheet_reset(&g_sheet);
    printf("After reset: %u/%u notes\n", g_note_pool.count, g_note_pool.capacity);

    const char *simple = "L:1/4\nK:C\nC D E F | G A B c |";
    if (abc_parse(&g_sheet, simple) == 0) {
        printf("Parsed: %u notes, pool: %u/%u\n",
               g_sheet.note_count, g_note_pool.count, g_note_pool.capacity);
    }

    printf("\nDone!\n");
    return 0;
}
