# ABC Music Notation Parser

A lightweight, embedded-friendly parser for ABC music notation written in C99. Parses ABC notation into note pools with frequency, duration, and MIDI data ready for synthesis.

> **Note:** This project was almost exclusively written by AI (Claude), with human guidance and review.

## Features

- **Zero dynamic allocation** - uses pre-allocated memory pools
- **Multi-voice support** - parse multiple voices into separate note pools
- **Chord support** - `[CEG]` notation with up to 4 simultaneous pitches
- **Tuplet support** - triplets `(3CDE`, duplets `(2CD`, and more (2-9)
- **Repeat unfolding** - `|: ... :|` sections are expanded inline
- **Key signature support** - major and minor keys with correct accidentals
- **Tempo with note value** - `Q:1/4=120` (quarter=120) or `Q:1/8=120` (eighth=120)
- **Embedded-ready** - no stdlib dependencies except `<string.h>` and `<stdint.h>`
- **Configurable limits** - adjust note count, voices, and string lengths at compile time

## Memory Usage

With default settings (2 voices, 128 notes per voice, 4 notes per chord):

| Component | Size |
|-----------|------|
| Note struct | 10 bytes |
| Sheet struct | 96 bytes |
| Note pool (128 notes) | 1,308 bytes |
| **Total (2 voice pools)** | **~2.7 KB** |

For single-voice applications with `ABC_MAX_VOICES=1`: **~1.4 KB**

Notes store only MIDI note numbers; frequency, note name, and octave are computed on demand via API functions.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Or compile directly:

```bash
gcc -O2 -o abcparser main.c abc_parser.c
```

## Usage

### Basic (Single Voice)

```c
#include "abc_parser.h"

// Pre-allocate memory (static or global)
static NotePool g_pools[ABC_MAX_VOICES];
static struct sheet g_sheet;

int main(void) {
    // Initialize pools and sheet
    for (int i = 0; i < ABC_MAX_VOICES; i++) {
        note_pool_init(&g_pools[i]);
    }
    sheet_init(&g_sheet, g_pools, ABC_MAX_VOICES);

    // Parse ABC notation
    const char *music = "L:1/4\nK:C\nC D E F | G A B c |";
    int result = abc_parse(&g_sheet, music);

    if (result < 0) {
        // -1: invalid input, -2: pool exhausted
        return 1;
    }

    // Iterate through notes (first voice)
    struct note *n = sheet_first_note(&g_sheet);
    while (n) {
        uint8_t midi = n->midi_note[0];                      // MIDI note number
        float freq = midi_to_frequency_x10(midi) / 10.0f;    // Hz (computed from MIDI)
        uint16_t dur = n->duration_ms;                       // milliseconds

        // Use for synthesis...

        n = note_next(&g_pools[0], n);
    }

    // Reuse memory for another parse
    sheet_reset(&g_sheet);

    return 0;
}
```

### Multi-Voice

```c
const char *music =
    "L:1/8\nK:C\n"
    "V:MELODY\n"
    "C D E F | G A B c |\n"
    "V:BASS\n"
    "C,4 G,4 | C,4 G,4 |";

abc_parse(&g_sheet, music);

// Access each voice
for (int v = 0; v < g_sheet.voice_count; v++) {
    NotePool *pool = &g_pools[v];
    printf("Voice: %s\n", pool->voice_id);

    struct note *n = pool_first_note(pool);
    while (n) {
        // Process notes...
        n = note_next(pool, n);
    }
}
```

### Chords

```c
const char *music = "K:C\n[CEG] [FAc] [GBd]";  // C major, F major, G major chords

abc_parse(&g_sheet, music);

struct note *n = sheet_first_note(&g_sheet);
while (n) {
    printf("Chord with %d notes:\n", n->chord_size);
    for (int i = 0; i < n->chord_size; i++) {
        uint8_t midi = n->midi_note[i];
        printf("  %s%d @ %.1f Hz\n",
               note_name_to_string(midi_to_note_name(midi)),
               midi_to_octave(midi),
               midi_to_frequency_x10(midi) / 10.0f);
    }
    n = note_next(&g_pools[0], n);
}
```

## Supported ABC Notation

| Element | Syntax | Example |
|---------|--------|---------|
| Notes | `C D E F G A B` (octave 4), `c d e f g a b` (octave 5) | `C D E F` |
| Octave up/down | `'` / `,` | `c'` (octave 6), `C,` (octave 3) |
| Sharps | `^` | `^F` (F#) |
| Flats | `_` | `_B` (Bb) |
| Naturals | `=` | `=F` (F natural) |
| Double sharp/flat | `^^` / `__` | `^^C` (C##) |
| Duration multiplier | number after note | `C2` (double), `C4` (quadruple) |
| Duration fraction | `/` after note | `C/2` (half), `C/` (half), `C//` (quarter) |
| Rests | `z` or `Z` | `z2` (rest, double length) |
| Chords | `[notes]` | `[CEG]` (C major chord) |
| Tuplets | `(n` before notes | `(3CDE` (triplet), `(2CD` (duplet) |
| Voices | `V:id` | `V:MELODY`, `V:BASS` |
| Repeats | `\|: ... :\|` | `\|:C D E F:\|` |
| Bar lines | `\|`, `\|\|`, `\|]` | `C D \| E F` |

### Tuplet Reference

| Syntax | Effect | Duration |
|--------|--------|----------|
| `(2CD` | Duplet: 2 notes in time of 3 | 1.5x normal |
| `(3CDE` | Triplet: 3 notes in time of 2 | 0.67x normal |
| `(4CDEF` | Quadruplet: 4 notes in time of 3 | 0.75x normal |
| `(5...` | Quintuplet: 5 notes in time of 4 | 0.8x normal |
| `(6...` | Sextuplet: 6 notes in time of 2 | 0.33x normal |
| `(7...`-`(9...` | 7-9 notes in time of n-1 | varies |

### Header Fields

| Field | Description | Example |
|-------|-------------|---------|
| `X:` | Reference number | `X:1` |
| `T:` | Title | `T:Greensleeves` |
| `C:` | Composer | `C:Traditional` |
| `M:` | Meter | `M:4/4` |
| `L:` | Default note length | `L:1/8` |
| `Q:` | Tempo (BPM) | `Q:120` or `Q:1/4=120` |
| `K:` | Key signature | `K:G`, `K:Amin`, `K:Bb` |
| `V:` | Voice | `V:MELODY` |

### Tempo Note Values

The tempo field supports specifying which note value gets the beat:

- `Q:120` - 120 BPM (quarter note assumed)
- `Q:1/4=120` - quarter note = 120 BPM
- `Q:1/8=120` - eighth note = 120 BPM (half the speed of Q:1/4=120)
- `Q:3/8=120` - dotted quarter = 120 BPM

## Configuration

Adjust limits in `abc_parser.h` or via compile flags:

```c
#define ABC_MAX_NOTES       128  // Max notes per voice
#define ABC_MAX_VOICES        2  // Max number of voices
#define ABC_MAX_CHORD_NOTES   4  // Max simultaneous notes in a chord
#define ABC_MAX_TITLE_LEN    32  // Title string buffer
#define ABC_MAX_COMPOSER_LEN 32  // Composer string buffer
#define ABC_MAX_KEY_LEN       8  // Key string buffer
#define ABC_MAX_VOICE_ID_LEN 16  // Voice ID string buffer
```

Or compile with:

```bash
gcc -DABC_MAX_NOTES=256 -DABC_MAX_VOICES=4 -o abcparser main.c abc_parser.c
```

## Data Structures

### Note Structure

```c
struct note {
    int16_t next_index;                         // Index of next note (-1 = end)
    uint16_t duration_ms;                       // Duration in milliseconds
    uint8_t chord_size;                         // Number of notes (1 = single note)
    uint8_t midi_note[ABC_MAX_CHORD_NOTES];     // MIDI note numbers (0-127, 0 = rest)
};
```

Note properties (frequency, note name, octave) are computed on demand from MIDI values using the utility functions.

### Note Pool Structure

```c
typedef struct {
    struct note notes[ABC_MAX_NOTES];
    char voice_id[ABC_MAX_VOICE_ID_LEN];  // Voice identifier
    int16_t head_index;                    // First note index
    int16_t tail_index;                    // Last note index
    uint16_t count;                        // Notes in use
    uint16_t capacity;                     // Always ABC_MAX_NOTES
    uint32_t total_duration_ms;            // Total duration for this voice
} NotePool;
```

## API Reference

### Initialization

```c
void note_pool_init(NotePool *pool);                              // Initialize pool
void sheet_init(struct sheet *s, NotePool *pools, uint8_t count); // Initialize sheet
void sheet_reset(struct sheet *s);                                // Reset for reuse
```

### Parsing

```c
int abc_parse(struct sheet *s, const char *abc);
// Returns: 0 = success, -1 = invalid input, -2 = pool exhausted
```

### Iteration

```c
struct note *sheet_first_note(const struct sheet *s);              // First note (voice 0)
struct note *pool_first_note(const NotePool *pool);                // First note in pool
struct note *note_next(const NotePool *pool, const struct note *n); // Next note
struct note *note_get(const NotePool *pool, int index);            // Note by index
```

### MIDI Conversion (compute note properties from stored MIDI)

```c
uint16_t midi_to_frequency_x10(uint8_t midi);  // Returns frequency * 10 in Hz
NoteName midi_to_note_name(uint8_t midi);       // Returns note name (C, D, E, etc.)
uint8_t midi_to_octave(uint8_t midi);           // Returns octave (0-10)
int midi_is_rest(uint8_t midi);                 // Returns 1 if rest (midi == 0)
```

### Other Utilities

```c
float note_to_frequency(NoteName name, int octave, int8_t acc);
int note_to_midi(NoteName name, int octave, int8_t acc);
const char *note_name_to_string(NoteName name);
const char *accidental_to_string(int8_t acc);
int note_pool_available(const NotePool *pool);
```

### Debug

```c
void sheet_print(const struct sheet *s);  // Print sheet to stdout
```

## Testing

Run the test suite:

```bash
cd build
ctest
# or
./test_parser
```

70 tests covering notes, octaves, accidentals, durations, tuplets, rests, key signatures, header fields, repeats, frequencies, MIDI notes, chords, and voices.

## License

MIT
