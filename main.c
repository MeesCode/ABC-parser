#include <stdio.h>
#include "abc_parser.h"

// ============================================================================
// Pre-allocated memory - adjust ABC_MAX_NOTES in abc_parser.h for your needs
// ============================================================================

static NotePool g_note_pools[ABC_MAX_VOICES];
static struct sheet g_sheet;

static const char *music =
    "X:1\n"
    "T:Super Mario Theme\n"
    "M:4/4\n"
    "L:1/8\n"
    "Q:1/4=105\n"
    "K:G\n"
    "V:SINE\n"
    "[e/2c/2][ce][ec][c/2A/2][ce] g/2z3z/2|c/2zG/2 zE/2zAB^A/2=A| (3Geg a=f/2gec/2 d/2B/2z|c/2zG/2 zE/2zAB^A/2=A|\n"
    "V:SQUARE\n"
    "E4 G4 | C4 z4 | G4 D4 | C4 z4 | \n"
    "V:SINE\n"
    "(3Geg a=f/2gec/2 d/2B/2z|zg/2^f/2 =f/2^de^G/2A/2cA/2c/2=d/2|zg/2^f/2 =f/2^dec'c'/2 c'/2z3/2|zg/2^f/2 =f/2^de^G/2A/2cA/2c/2=d/2|\n"
    "V:SQUARE\n"
    "G4 D4 | z G3 E4 | z G3 c4 | z G3 E4 | \n"
    "V:SINE\n"
    "z^d/2z=d/2z c/2z3z/2|]\n"
    "V:SQUARE\n"
    "z ^D3 C/z3z/2|]\n";

int main(void) {
    printf("ABC Music Parser (Embedded Version)\n");
    printf("====================================\n\n");

    for (int i = 0; i < ABC_MAX_VOICES; i++) {
        note_pool_init(&g_note_pools[i]);
    }
    sheet_init(&g_sheet, g_note_pools, ABC_MAX_VOICES);

    printf("Memory footprint:\n");
    printf("  Note struct:  %3zu bytes\n", sizeof(struct note));
    printf("  Sheet struct: %3zu bytes\n", sizeof(struct sheet));
    printf("  Note pool:    %3zu bytes (%u notes)\n",
           sizeof(g_note_pools[0]), g_note_pools[0].capacity);
    printf("  Total pools:  %3zu bytes (%d pools)\n",
           sizeof(g_note_pools), ABC_MAX_VOICES);
    printf("  Total static: %3zu bytes\n\n",
           sizeof(g_note_pools) + sizeof(g_sheet));

    int result = abc_parse(&g_sheet, music);

    if (result < 0) {
        printf("Error: parse failed (%d)\n", result);
        if (result == -2) printf("  Pool exhausted!\n");
        return 1;
    }

    sheet_print(&g_sheet);

    printf("\nFirst 100 notes from each voice:\n");
    for (uint8_t v = 0; v < g_sheet.voice_count; v++) {
        NotePool *pool = &g_note_pools[v];
        printf("\n--- Voice %u: %s ---\n", v + 1, pool->voice_id);
        struct note *n = pool_first_note(pool);
        for (int i = 0; n && i < 100; i++) {
            uint16_t ms = ticks_to_ms(n->duration, g_sheet.tempo_bpm);
            if (n->chord_size == 1 && midi_is_rest(n->midi_note[0])) {
                printf("  %d: REST %u ticks (%u ms)\n", i + 1, n->duration, ms);
            } else if (n->chord_size == 1) {
                printf("  %d: %s%u @ %.1f Hz, %u ticks (%u ms)\n",
                       i + 1,
                       note_name_to_string(midi_to_note_name(n->midi_note[0])),
                       midi_to_octave(n->midi_note[0]),
                       midi_to_frequency_x10(n->midi_note[0]) / 10.0f,
                       n->duration, ms);
            } else {
                printf("  %d: [", i + 1);
                for (uint8_t j = 0; j < n->chord_size; j++) {
                    if (j > 0) printf("+");
                    printf("%s%u",
                           note_name_to_string(midi_to_note_name(n->midi_note[j])),
                           midi_to_octave(n->midi_note[j]));
                }
                printf("] @ %.1f Hz, %u ticks (%u ms)\n", midi_to_frequency_x10(n->midi_note[0]) / 10.0f, n->duration, ms);
            }
            n = note_next(pool, n);
        }
    }

    printf("\n--- Memory reuse test ---\n");
    sheet_reset(&g_sheet);
    printf("After reset: %u/%u notes in pool 0\n", g_note_pools[0].count, g_note_pools[0].capacity);

    const char *simple = "L:1/4\nK:C\nC D E F | G A B c |";
    if (abc_parse(&g_sheet, simple) == 0) {
        printf("Parsed: %u notes in %u voices, pool 0: %u/%u\n",
               g_note_pools[0].count, g_sheet.voice_count,
               g_note_pools[0].count, g_note_pools[0].capacity);
    }

    printf("\nDone!\n");
    return 0;
}
