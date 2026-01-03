#include <stdio.h>
#include "abc_parser.h"

// ============================================================================
// Pre-allocated memory - adjust ABC_MAX_NOTES in abc_parser.h for your needs
// ============================================================================

static NotePool g_note_pools[ABC_MAX_VOICES];
static struct sheet g_sheet;

static const char *music =
    "X:1\n"
    "T:Super Mario Bros\n"
    "L:1/4\n"
    "Q:1/4=100\n"
    "M:4/4\n"
    "K:C\n"
    "V:1\n"
    "e/4 e/ e/4 z/4 c/4 e/ g G | c/ z/4 G/4 z/ E/ z/4 A/4 z/4 B/4 z/4 _B/4 A/ | (3G/ e/ g/ a/ f/4 g/4 z/4 e/4 z/4 c/4 d/4 B/4 z/ | c/ z/4 G/4 z/ E/ z/4 A/4 z/4 B/4 z/4 _B/4 A/ |\n"
    "(3G/ e/ g/ a/ f/4 g/4 z/4 e/4 z/4 c/4 d/4 B/4 z/ | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 _A/4 =A/4 c/4 z/4 A/4 c/4 d/4 | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 c'/4 z/4 c'/4 c' | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 _A/4 =A/4 c/4 z/4 A/4 c/4 d/4 |\n"
    "z/ _e/ z/4 d/4 z/ c z | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 _A/4 =A/4 c/4 z/4 A/4 c/4 d/4 | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 c'/4 z/4 c'/4 c' | z/ g/4 _g/4 f/4 _e/ =e/4 z/4 _A/4 =A/4 c/4 z/4 A/4 c/4 d/4 |\n"
    "z/ _e/ z/4 d/4 z/ c z | c/4 c/ c/4 z/4 c/4 d/ e/4 c/ A/4 G | c/4 c/ c/4 z/4 c/4 d/4 e/4 z2 | c/4 c/ c/4 z/4 c/4 d/ e/4 c/ A/4 G |\n"
    "e/4 e/ e/4 z/4 c/4 e/ g G | c/ z/4 G/4 z/ E/ z/4 A/4 z/4 B/4 z/4 _B/4 A/ | (3G/ e/ g/ a/ f/4 g/4 z/4 e/4 z/4 c/4 d/4 B/4 z/ | c/ z/4 G/4 z/ E/ z/4 A/4 z/4 B/4 z/4 _B/4 A/ |\n"
    "(3G/ e/ g/ a/ f/4 g/4 z/4 e/4 z/4 c/4 d/4 B/4 z/ | e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A | (3B/ a/ a/ (3a/ g/ f/ e/4 c/ A/4 G | e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A |\n"
    "B/4 f/ f/4 (3f/ e/ d/ c/4 G/ G/4 C | e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A | (3B/ a/ a/ (3a/ g/ f/ e/4 c/ A/4 G | e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A | B/4 f/ f/4 (3f/ e/ d/ c/4 G/ G/4 C |\n"
    "c/4 c/ c/4 z/4 c/4 d/ e/4 c/ A/4 G | c/4 c/ c/4 z/4 c/4 d/4 e/4 z2 | c/4 c/ c/4 z/4 c/4 d/ e/4 c/ A/4 G | e/4 e/ e/4 z/4 c/4 e/ g G |\n"
    "e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A | (3B/ a/ a/ (3a/ g/ f/ e/4 c/ A/4 G | e/4 c/ G/4 z/ ^G/ A/4 f/ f/4 A | B/4 f/ f/4 (3f/ e/ d/ c/4 G/ G/4 C |\n"
    "G4 |]\n"
    "V:2\n"
    "D/4 D/ D/4 z/4 D/4 D/ G G, | G/ z/4 E/4 z/ C/ z/4 F/4 z/4 G/4 z/4 _G/4 F/ | (3E/ c/ e/ f/ d/4 e/4 z/4 c/4 z/4 A/4 B/4 G/4 z/ | G/ z/4 E/4 z/ C/ z/4 F/4 z/4 G/4 z/4 _G/4 F/ |\n"
    "(3E/ c/ e/ f/ d/4 e/4 z/4 c/4 z/4 A/4 B/4 G/4 z/ | C/ z/4 G/4 z/ c/ F/ z/4 c/4 c/4 c/4 F/ | C/ z/4 E/4 z/ G/4 c/4 z/4 f/4 z/4 f/4 f/ A/ | C/ z/4 G/4 z/ c/ F/ z/4 c/4 c/4 c/4 F/ |\n"
    "C/ _A/ z/4 _B/4 z/ c/ z/4 G/4 G/ C/ | C/ z/4 G/4 z/ c/ F/ z/4 c/4 c/4 c/4 F/ | C/ z/4 E/4 z/ G/4 c/4 z/4 f/4 z/4 f/4 f/ A/ | C/ z/4 G/4 z/ c/ F/ z/4 c/4 c/4 c/4 F/ |\n"
    "C/ _A/ z/4 _B/4 z/ c/ z/4 G/4 G/ C/ | _A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ | _A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ | _A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ |\n"
    "D/4 D/ D/4 z/4 D/4 D/ G G, | G/ z/4 E/4 z/ C/ z/4 F/4 z/4 G/4 z/4 _G/4 F/ | (3E/ c/ e/ f/ d/4 e/4 z/4 c/4 z/4 A/4 B/4 G/4 z/ | G/ z/4 E/4 z/ C/ z/4 F/4 z/4 G/4 z/4 _G/4 F/ |\n"
    "(3E/ c/ e/ f/ d/4 e/4 z/4 c/4 z/4 A/4 B/4 G/4 z/ | C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ | D/ z/4 F/4 G/ B/ G/ G/ B/4 B/4 G/ | C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ |\n"
    "G/ z/4 G/4 (3G/ A/ B/ c/ G/ C | C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ | D/ z/4 F/4 G/ B/ G/ G/ B/4 B/4 G/ | C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ | G/ z/4 G/4 (3G/ A/ B/ c/ G/ C |\n"
    "_A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ | _A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ | _A,/ z/4 _E/4 z/ _B/ A/ z/4 C/4 z/ G,/ | D/4 D/ D/4 z/4 D/4 D/ G G, |\n"
    "C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ | D/ z/4 F/4 G/ B/ G/ G/ B/4 B/4 G/ | C/ z/4 G/4 G/ c/ F/ F/ c/4 c/4 F/ | G/ z/4 G/4 (3G/ A/ B/ c/ G/ C |\n"
    "C4 |]\n";

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
