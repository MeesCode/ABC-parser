#include <stdio.h>
#include "abc_parser.h"

// ============================================================================
// Pre-allocated memory - adjust ABC_MAX_NOTES in abc_parser.h for your needs
// ============================================================================

// Static allocation of note pool and sheet
static NotePool g_note_pool;
static struct sheet g_sheet;

// Example ABC notation piece
static const char *music =
    "T:Greensleeves\n"
    "C:Traditional\n"
    "M:6/8\n"
    "L:1/8\n"
    "Q:120\n"
    "K:Amin\n"
    "AG |:E2 A2 A2 B2 :|c4 c2 dc |B3 A G2 ^F2 |G6 AG |"
    "E2 A2 A2 B2 |c4 c2 d2 |e3 d c2 d2 |e6 g3/2f/ |"
    "e3 d c2 d2 |e3 f g2 fe |d3 c B2 c2 |d6 cd |"
    "e2 c2 d2 cB |c2 BA B2 AG |E2 A2 A2 G2 |A6 |";

int main(void) {
    printf("ABC Music Parser (Embedded Version)\n");
    printf("====================================\n\n");

    // Initialize the memory pool (do this once at startup)
    note_pool_init(&g_note_pool);

    // Initialize the sheet with the pool
    sheet_init(&g_sheet, &g_note_pool);

    printf("Memory pool capacity: %d notes\n", g_note_pool.capacity);
    printf("Sheet struct size: %zu bytes\n", sizeof(struct sheet));
    printf("Note struct size: %zu bytes\n", sizeof(struct note));
    printf("Total static memory: %zu bytes\n\n",
           sizeof(g_note_pool) + sizeof(g_sheet));

    // Parse the music
    int result = abc_parse(&g_sheet, music);

    if (result < 0) {
        printf("Error: Failed to parse ABC notation (error code: %d)\n", result);
        if (result == -2) {
            printf("  Note pool exhausted! Increase ABC_MAX_NOTES.\n");
        }
        return 1;
    }

    // Print the parsed sheet
    sheet_print(&g_sheet);

    // Example: iterate through notes
    printf("\nFirst 10 notes (iteration example):\n");
    struct note *note = sheet_first_note(&g_sheet);
    int count = 0;
    while (note && count < 10) {
        if (note->note_name != NOTE_REST) {
            printf("  Note %d: %s%s%d @ %.2f Hz for %.1f ms\n",
                   count + 1,
                   note_name_to_string(note->note_name),
                   accidental_to_string(note->accidental),
                   note->octave,
                   note->frequency,
                   note->duration_ms);
        } else {
            printf("  Note %d: REST for %.1f ms\n",
                   count + 1, note->duration_ms);
        }
        note = note_next(&g_sheet, note);
        count++;
    }

    // Demonstrate reuse - reset and parse again
    printf("\n--- Demonstrating memory reuse ---\n");
    sheet_reset(&g_sheet);
    printf("After reset - pool usage: %d/%d\n",
           g_note_pool.count, g_note_pool.capacity);

    // Parse a simpler tune
    const char *simple_tune =
        "L:1/4\n"
        "K:C\n"
        "C D E F | G A B c |";

    result = abc_parse(&g_sheet, simple_tune);
    if (result == 0) {
        printf("Parsed simple tune: %d notes\n", g_sheet.note_count);
        printf("Pool usage: %d/%d\n", g_note_pool.count, g_note_pool.capacity);
    }

    printf("\nDone!\n");
    return 0;
}
