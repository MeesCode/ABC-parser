#include <stdio.h>
#include <stdlib.h>
#include "abc_parser.h"

// Example ABC notation piece - Greensleeves-like melody
const char *music =
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

int main(int argc, char *argv[]) {
    printf("ABC Music Parser\n");
    printf("================\n\n");

    // Parse the music
    struct sheet *sheet = abc_parse(music);

    if (!sheet) {
        fprintf(stderr, "Error: Failed to parse ABC notation\n");
        return 1;
    }

    // Print the parsed sheet
    sheet_print(sheet);

    // Example: iterate through notes manually
    printf("\nFirst 10 notes (iteration example):\n");
    struct note *note = sheet->head;
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
        note = note->next;
        count++;
    }

    // Clean up
    sheet_free(sheet);

    printf("\nDone!\n");
    return 0;
}
