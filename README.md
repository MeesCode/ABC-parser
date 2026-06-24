# ABC Music Notation Parser

A lightweight, embedded-friendly parser for ABC music notation written in C99. Parses ABC notation into note pools with frequency, duration, and MIDI data ready for synthesis.

> **Note:** This project was almost exclusively written by AI (Claude), with human guidance and review.

## Features

- **Zero dynamic allocation** - uses pre-allocated memory pools
- **Runtime configurable** - set note count, voices, and chord size per pool at init time
- **Multi-voice support** - parse multiple voices into separate note pools
- **Chord support** - `[CEG]` notation with configurable simultaneous pitches
- **Tuplet support** - triplets `(3CDE`, duplets `(2CD`, and more (2-9)
- **Repeat unfolding** - `|: ... :|` sections are expanded inline
- **Key signature support** - major and minor keys with correct accidentals
- **Tempo with note value** - `Q:1/4=120` (quarter=120) or `Q:1/8=120` (eighth=120)
- **Embedded-ready** - core needs only `<string.h>`, `<stdint.h>`, `<stddef.h>`; floating-point and `stdio` are isolated and can be compiled out (see [Configuration](#configuration))

## Memory Usage

Memory usage depends on your configuration. With 2 voices, 128 notes per voice, 4 notes per chord:

| Component | Size |
|-----------|------|
| Note struct | 8 bytes |
| Sheet struct | 96 bytes |
| NotePool header | 40 bytes |
| Note storage (128 notes) | 1,024 bytes |
| **Total (2 voices)** | **~2.2 KB** |

Notes store only MIDI note numbers and duration in MIDI ticks (PPQ=48). Frequency, note name, and octave are computed on demand via API functions.

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

// Define your limits
#define MAX_VOICES 2
#define MAX_NOTES 128

// Pre-allocate memory (static or global)
static NotePool g_pools[MAX_VOICES];
static struct note g_storage[MAX_VOICES][MAX_NOTES];
static struct sheet g_sheet;

int main(void) {
    // Initialize pools with storage
    for (int i = 0; i < MAX_VOICES; i++) {
        note_pool_init(&g_pools[i], g_storage[i], MAX_NOTES, ABC_MAX_CHORD_NOTES);
    }
    sheet_init(&g_sheet, g_pools, MAX_VOICES);

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
        uint8_t ticks = n->duration;                         // Duration in MIDI ticks
        uint16_t ms = ticks_to_ms(ticks, g_sheet.tempo_bpm); // Convert to milliseconds

        // Use for synthesis...

        n = note_next(&g_pools[0], n);
    }

    // Reuse memory for another parse
    sheet_reset(&g_sheet);

    return 0;
}
```

### Convenience setup & iteration

The same program with the quality-of-life helpers — one `ABC_DEFINE_SHEET` for
storage, one `abc_sheet_setup` to wire it up, and `ABC_FOR_EACH_NOTE` to walk it:

```c
#include "abc_parser.h"

// Declares g_pools, g_storage and g (one flat note buffer).
ABC_DEFINE_SHEET(g, 2, 128);

int main(void) {
    abc_sheet_setup(&g, g_pools, g_storage, 2, 128, ABC_MAX_CHORD_NOTES);

    if (abc_parse(&g, "L:1/4\nK:C\nC D E F | G A B c |") < 0) return 1;

    ABC_FOR_EACH_NOTE(n, abc_sheet_voice(&g, 0)) {
        if (abc_note_is_rest(n)) continue;
        uint16_t freq_x10 = abc_note_frequency_x10(n, 0);          // 0 for rest/oob
        uint16_t ms       = abc_note_duration_ms(n, g.tempo_bpm);  // duration in ms
        // ... feed your synth ...
        (void)freq_x10; (void)ms;
    }
    return 0;
}
```

Need to size buffers before allocating? `abc_count_notes()` does a dry run
(repeats expanded, no storage written) so you can right-size each voice:

```c
uint16_t per_voice[4];
int voices = abc_count_notes(abc_string, per_voice, 4);  // returns voice count
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

### Different Pool Sizes

You can create pools with different capacities:

```c
// Small pool for simple melodies
static NotePool melody_pool;
static struct note melody_storage[64];
note_pool_init(&melody_pool, melody_storage, 64, 1);  // 64 notes, single notes only

// Large pool for complex compositions
static NotePool complex_pool;
static struct note complex_storage[1024];
note_pool_init(&complex_pool, complex_storage, 1024, 4);  // 1024 notes, up to 4-note chords
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
| Broken rhythm | `>` / `<` between notes | `a>b` (a dotted, b cut), `a<b` (reverse) |
| Grace notes | `{notes}` before a note | `{d}f` (d ornaments f, stealing a little time) |
| Ties | `-` between same pitch | `a-a` (held as one note) |
| Voices | `V:id` | `V:MELODY`, `V:BASS` |
| Repeats | `\|: ... :\|` | `\|:C D E F:\|` |
| Endings | `\|1 ... :\|2 ...` or `[1 ... :\|[2 ...` | first/second-time bars (unfolded) |
| Inline fields | `[X:...]` mid-tune | `[K:G]`, `[M:3/4]`, `[L:1/8]`, `[Q:120]`, `[V:2]` |
| Bar lines | `\|`, `\|\|`, `\|]` | `C D \| E F` |

Broken rhythm and grace notes are tempo-independent and conserve the bar's total
duration (grace notes steal `ABC_GRACE_NOTE_TICKS` from the following note; see
[Configuration](#configuration)). Numbered endings are unfolded inline like
repeats, so the note stream plays straight through.

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
| `K:` | Key signature | `K:G`, `K:Amin`, `K:Bb`, `K:Ador` |
| `V:` | Voice | `V:MELODY` |

Header fields may also appear **inline** mid-tune as `[K:G]`, `[M:3/4]`,
`[L:1/8]`, `[Q:120]` or `[V:2]`.

### Key Signatures

Key signatures are computed from the circle of fifths, so any tonic (`A`–`G`
with optional `#`/`b`) and any church mode is accepted: major/Ionian (default),
minor (`m`/`min`/Aeolian), Dorian (`dor`), Mixolydian (`mix`), Phrygian (`phr`),
Lydian (`lyd`) and Locrian (`loc`) — e.g. `K:Bb`, `K:F#m`, `K:Ador`, `K:Emix`.

### Tempo Note Values

The tempo field supports specifying which note value gets the beat:

- `Q:120` - 120 BPM (quarter note assumed)
- `Q:1/4=120` - quarter note = 120 BPM
- `Q:1/8=120` - eighth note = 120 BPM (half the speed of Q:1/4=120)
- `Q:3/8=120` - dotted quarter = 120 BPM

## Configuration

Compile-time defines in `abc_parser.h` (affects struct sizes):

```c
#define ABC_MAX_CHORD_NOTES   4  // Max simultaneous notes in a chord
#define ABC_MAX_TITLE_LEN    32  // Title string buffer
#define ABC_MAX_COMPOSER_LEN 32  // Composer string buffer
#define ABC_MAX_KEY_LEN       8  // Key string buffer
#define ABC_MAX_VOICE_ID_LEN 16  // Voice ID string buffer
#define ABC_PPQ              48  // Pulses per quarter note (MIDI ticks)
#define ABC_COUNT_MAX_VOICES  8  // Max voices abc_count_notes() can tally
#define ABC_GRACE_MAX         4  // Max grace notes buffered per {..} group
#define ABC_GRACE_NOTE_TICKS  6  // Ticks per grace note (1/32), stolen from the principal
```

Build-mode switches (both default to current behavior, so existing builds are
unaffected):

```c
#define ABC_NO_FLOAT  // Exclude all floating-point helpers (note_to_frequency,
                      // abc_midi_to_frequency, abc_note_frequency). The integer
                      // core (midi_to_frequency_x10, *_x10 helpers) is unaffected.
#define ABC_NO_PRINT  // Exclude sheet_print(). It is the only user of <stdio.h>.
```

Defining **both** yields a freestanding build that needs only `<stdint.h>`,
`<stddef.h>` and `<string.h>` — no FPU, no `stdio`.

Score defaults (applied only when the matching ABC header is **absent**). These
default to ABC-standard behavior; override at build time to match your project's
conventions:

```c
#define ABC_DEFAULT_TEMPO_BPM       120  // Tempo when Q: is absent
#define ABC_DEFAULT_METER_NUM         4  // Meter numerator when M: is absent
#define ABC_DEFAULT_METER_DEN         4  // Meter denominator when M: is absent
#define ABC_DEFAULT_KEY             "C"  // Key signature when K: is absent
#define ABC_DERIVE_LENGTH_FROM_METER  1  // When L: absent: 1 = derive from meter
                                         //   (<3/4 -> 1/16, else 1/8, ABC standard)
                                         //   0 = use the fixed default below
#define ABC_DEFAULT_NOTE_NUM          1  // Unit note length numerator when L: absent
#define ABC_DEFAULT_NOTE_DEN          4  // Unit note length denominator when L: absent
```

For example, to keep a fixed `L:1/4` and `Q:100` for headerless tunes:
`-DABC_DEFAULT_TEMPO_BPM=100 -DABC_DERIVE_LENGTH_FROM_METER=0`.

If you would rather not pass `-D` flags, drop an **`abc_config.h`** anywhere on
the include path with your overrides — `abc_parser.h` auto-includes it (via
`__has_include`) when present, and ignores it otherwise. Standalone builds that
don't ship the file keep the ABC-standard defaults:

```c
// abc_config.h
#define ABC_DEFAULT_TEMPO_BPM 100
#define ABC_DERIVE_LENGTH_FROM_METER 0
```

Runtime parameters (passed to `note_pool_init()`):
- **capacity** - max notes per pool (no compile-time limit)
- **max_chord_notes** - max notes per chord for this pool (clamped to ABC_MAX_CHORD_NOTES)

## Data Structures

### Note Structure

```c
struct note {
    int16_t next_index;                         // Index of next note (-1 = end)
    uint8_t duration;                           // Duration in MIDI ticks (PPQ=48)
    uint8_t chord_size;                         // Number of notes (1 = single note)
    uint8_t midi_note[ABC_MAX_CHORD_NOTES];     // MIDI note numbers (0-127, 0 = rest)
};
```

Note properties (frequency, note name, octave) are computed on demand from MIDI values using the utility functions. Duration is stored as tempo-independent MIDI ticks; use `ticks_to_ms()` to convert to milliseconds.

### Note Pool Structure

```c
typedef struct {
    struct note *notes;       // Pointer to note storage (user-provided)
    char voice_id[ABC_MAX_VOICE_ID_LEN];  // Voice identifier
    int16_t head_index;       // First note index
    int16_t tail_index;       // Last note index
    uint16_t count;           // Notes in use
    uint16_t capacity;        // Max notes (from init)
    uint32_t total_ticks;     // Total duration in MIDI ticks
    uint8_t max_chord_notes;  // Max chord size (from init)
} NotePool;
```

## API Reference

### Initialization

```c
// Initialize a note pool with external storage
void note_pool_init(NotePool *pool, struct note *buffer, uint16_t capacity, uint8_t max_chord_notes);

// Initialize sheet with array of pools
void sheet_init(struct sheet *s, NotePool *pools, uint8_t pool_count);

// Reset for reuse (clears all pools)
void sheet_reset(struct sheet *s);

// One-shot: slice one flat buffer into `voices` equal pools + init the sheet.
// Returns 0 on success, -1 on bad arguments.
int abc_sheet_setup(struct sheet *s, NotePool *pools, struct note *buffer,
                    uint8_t voices, uint16_t notes_per_voice, uint8_t max_chord_notes);

// Bytes of note storage needed to back `voices` pools of `notes_per_voice`.
size_t abc_storage_bytes(uint8_t voices, uint16_t notes_per_voice);

// Declare static g_pools / g_storage / sheet for the common static case:
//   ABC_DEFINE_SHEET(name, VOICES, NOTES_PER_VOICE);
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

NotePool    *abc_sheet_voice(const struct sheet *s, uint8_t i);    // Bounds-checked voice (NULL if oob)

// Zero-cost loop macros
ABC_FOR_EACH_NOTE(n, pool)        // for each note in a pool
ABC_FOR_EACH_CHORD_NOTE(i, n)     // for each chord member index in a note

// Resumable iterator (for suspended cursors such as per-voice playback state)
typedef struct { const NotePool *pool; struct note *cur; uint8_t loop; } AbcNoteIter;
void         abc_note_iter_init(AbcNoteIter *it, const NotePool *pool, uint8_t loop);
struct note *abc_note_iter_next(AbcNoteIter *it);  // returns note then advances; wraps if loop
```

### Note inspection

```c
int      abc_note_is_rest(const struct note *n);                      // 1 if rest/empty
uint16_t abc_note_frequency_x10(const struct note *n, uint8_t chord_idx); // 0 for rest/oob
```

### MIDI Conversion (compute note properties from stored MIDI)

```c
uint16_t midi_to_frequency_x10(uint8_t midi);  // Returns frequency * 10 in Hz (canonical, integer)
NoteName midi_to_note_name(uint8_t midi);       // Returns note name (C, D, E, etc.)
uint8_t midi_to_octave(uint8_t midi);           // Returns octave (0-10)
int midi_is_rest(uint8_t midi);                 // Returns 1 if rest (midi == 0)

// Float conveniences (compiled out under ABC_NO_FLOAT)
float abc_midi_to_frequency(uint8_t midi);                  // Hz
float abc_note_frequency(const struct note *n, uint8_t i);  // Hz, 0.0f for rest/oob
```

### Duration Conversion

```c
uint16_t ticks_to_ms(uint8_t ticks, uint16_t bpm);          // Convert ticks to milliseconds
uint32_t pool_total_ms(const NotePool *pool, uint16_t bpm);   // Total pool duration in ms
uint16_t abc_note_duration_ms(const struct note *n, uint16_t bpm); // Note duration in ms
uint32_t abc_ticks_to_samples(uint8_t ticks, uint16_t bpm, uint32_t sample_rate); // Ticks -> samples
```

### Errors & sizing

```c
const char *abc_strerror(int code);  // Human-readable string for an abc_parse() code

// Dry-run note counter: parses without writing pools so you can size storage.
// Writes up to max_voices counts into per_voice_out, returns the voice count
// (clamped to ABC_COUNT_MAX_VOICES), or negative on error.
int abc_count_notes(const char *abc, uint16_t *per_voice_out, uint8_t max_voices);
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

Tests cover notes, octaves, accidentals, durations, tuplets, rests, key signatures, header fields, repeats, frequencies, MIDI notes, chords, voices, the quality-of-life API (setup, iteration, inspection, duration/sample conversion, error strings, and the `abc_count_notes` dry run), and spec coverage (broken rhythm, grace notes, ties, modal keys, inline fields, numbered endings, plus a real-world strathspey integration test).

## License

MIT
