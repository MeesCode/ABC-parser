# ABC Music Notation Parser

A lightweight, embedded-friendly parser for ABC music notation written in C99. Parses ABC notation into a linked list of notes with frequency, duration, and MIDI data ready for synthesis.

> **Note:** This project was almost exclusively written by AI (Claude), with human guidance and review.

## Features

- **Zero dynamic allocation** - uses pre-allocated memory pools
- **Minimal footprint** - ~1.4 KB RAM with default settings
- **Embedded-ready** - no stdlib dependencies except `<string.h>` and `<stdint.h>`
- **Repeat unfolding** - `|: ... :|` sections are expanded inline
- **Key signature support** - major and minor keys with correct accidentals
- **Configurable limits** - adjust note count and string lengths at compile time

## Memory Usage

| Component | Size |
|-----------|------|
| Note struct | 10 bytes |
| Sheet struct | 96 bytes |
| Note pool (128 notes) | 1,284 bytes |
| **Total static** | **~1.4 KB** |

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

```c
#include "abc_parser.h"

// Pre-allocate memory (static or global)
static NotePool g_pool;
static struct sheet g_sheet;

int main(void) {
    // Initialize once at startup
    note_pool_init(&g_pool);
    sheet_init(&g_sheet, &g_pool);

    // Parse ABC notation
    const char *music = "L:1/4\nK:C\nC D E F | G A B c |";
    int result = abc_parse(&g_sheet, music);

    if (result < 0) {
        // -1: invalid input, -2: pool exhausted
        return 1;
    }

    // Iterate through notes
    struct note *n = sheet_first_note(&g_sheet);
    while (n) {
        float freq = n->frequency_x10 / 10.0f;  // Hz
        uint16_t dur = n->duration_ms;           // milliseconds
        uint8_t midi = n->midi_note;             // MIDI note number

        // Use for synthesis...

        n = note_next(&g_sheet, n);
    }

    // Reuse memory for another parse
    sheet_reset(&g_sheet);

    return 0;
}
```

## Supported ABC Notation

| Element | Syntax | Example |
|---------|--------|---------|
| Notes | `C D E F G A B` (octave 4/3), `c d e f g a b` (octave 5/4) | `C D E F` |
| Octave up/down | `'` / `,` | `c'` (octave 6), `C,` (octave 3) |
| Sharps | `^` | `^F` (F#) |
| Flats | `_` | `_B` (Bb) |
| Naturals | `=` | `=F` (F natural) |
| Double sharp/flat | `^^` / `__` | `^^C` (C##) |
| Duration multiplier | number after note | `C2` (double), `C4` (quadruple) |
| Duration fraction | `/` after note | `C/2` (half), `C/` (half), `C//` (quarter) |
| Rests | `z` or `Z` | `z2` (rest, double length) |
| Repeats | `\|: ... :\|` | `\|:C D E F:\|` |
| Bar lines | `\|`, `\|\|`, `\|]` | `C D \| E F` |

### Header Fields

| Field | Description | Example |
|-------|-------------|---------|
| `T:` | Title | `T:Greensleeves` |
| `C:` | Composer | `C:Traditional` |
| `M:` | Meter | `M:4/4` |
| `L:` | Default note length | `L:1/8` |
| `Q:` | Tempo (BPM) | `Q:120` |
| `K:` | Key signature | `K:G`, `K:Amin`, `K:Bb` |

## Configuration

Adjust limits in `abc_parser.h` or via compile flags:

```c
#define ABC_MAX_NOTES      128   // Max notes per sheet
#define ABC_MAX_TITLE_LEN   32   // Title string buffer
#define ABC_MAX_COMPOSER_LEN 32  // Composer string buffer
#define ABC_MAX_KEY_LEN      8   // Key string buffer
```

Or compile with:

```bash
gcc -DABC_MAX_NOTES=256 -o abcparser main.c abc_parser.c
```

## API Reference

### Initialization

```c
void note_pool_init(NotePool *pool);           // Initialize pool (once)
void sheet_init(struct sheet *s, NotePool *p); // Initialize sheet with pool
void sheet_reset(struct sheet *s);             // Reset for reuse
```

### Parsing

```c
int abc_parse(struct sheet *s, const char *abc);
// Returns: 0 = success, -1 = invalid input, -2 = pool exhausted
```

### Iteration

```c
struct note *sheet_first_note(const struct sheet *s);
struct note *note_next(const struct sheet *s, const struct note *n);
struct note *note_get(const struct sheet *s, int index);
```

### Utilities

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

## License

MIT
