#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <stdint.h>
#include <stddef.h>   // size_t

// Optional project configuration. A host project can drop an "abc_config.h" on
// the include path to override any of the ABC_* defaults below without editing
// this header or passing -D flags. Standalone/upstream builds have no such file
// and use the ABC-standard defaults. Guarded so compilers that lack
// __has_include simply skip it (and keep the defaults).
#if defined(__has_include)
#  if __has_include("abc_config.h")
#    include "abc_config.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Version
// ============================================================================

#define ABC_PARSER_VERSION_MAJOR 1
#define ABC_PARSER_VERSION_MINOR 1
#define ABC_PARSER_VERSION_PATCH 0

// ============================================================================
// Build-mode switches (both default to current behavior)
// ============================================================================
//
//   ABC_NO_FLOAT  - exclude all floating-point helpers (note_to_frequency and
//                   the abc_*_frequency() inline conveniences). The integer
//                   core (midi_to_frequency_x10, *_x10 helpers) is unaffected.
//   ABC_NO_PRINT  - exclude sheet_print(). It is the only user of <stdio.h>
//                   and the only remaining float user in the core, so defining
//                   both ABC_NO_FLOAT and ABC_NO_PRINT yields a freestanding
//                   build that needs only <stdint.h>, <stddef.h>, <string.h>.

// ============================================================================
// Configuration - adjust these for your embedded system
// ============================================================================

#ifndef ABC_MAX_TITLE_LEN
#define ABC_MAX_TITLE_LEN 32       // Maximum title string length
#endif

#ifndef ABC_MAX_COMPOSER_LEN
#define ABC_MAX_COMPOSER_LEN 32    // Maximum composer string length
#endif

#ifndef ABC_MAX_KEY_LEN
#define ABC_MAX_KEY_LEN 8          // Maximum key string length
#endif

#ifndef ABC_MAX_CHORD_NOTES
#define ABC_MAX_CHORD_NOTES 4      // Maximum notes in a chord (affects struct note size)
#endif

#ifndef ABC_MAX_VOICE_ID_LEN
#define ABC_MAX_VOICE_ID_LEN 16    // Maximum voice ID length
#endif

#ifndef ABC_PPQ
#define ABC_PPQ 48                 // Pulses per quarter note (MIDI-style timing)
#endif

#ifndef ABC_COUNT_MAX_VOICES
#define ABC_COUNT_MAX_VOICES 8     // Max voices abc_count_notes() can tally (stack pools)
#endif

#ifndef ABC_GRACE_MAX
#define ABC_GRACE_MAX 4            // Max grace notes buffered per {..} group
#endif

#ifndef ABC_GRACE_NOTE_TICKS
#define ABC_GRACE_NOTE_TICKS (ABC_PPQ / 8)  // Length of one grace note (1/32), stolen from the principal
#endif

// ----------------------------------------------------------------------------
// Score defaults - values applied when the matching ABC header is absent.
// Override at build time (e.g. -DABC_DEFAULT_TEMPO_BPM=100) to match a host
// project's conventions without diverging from the parsed-header behavior.
// ----------------------------------------------------------------------------

#ifndef ABC_DEFAULT_TEMPO_BPM
#define ABC_DEFAULT_TEMPO_BPM 120  // Tempo when Q: is absent (in default-note-length beats)
#endif

#ifndef ABC_DEFAULT_METER_NUM
#define ABC_DEFAULT_METER_NUM 4    // Meter numerator when M: is absent
#endif
#ifndef ABC_DEFAULT_METER_DEN
#define ABC_DEFAULT_METER_DEN 4    // Meter denominator when M: is absent
#endif

#ifndef ABC_DEFAULT_KEY
#define ABC_DEFAULT_KEY "C"        // Key signature when K: is absent
#endif

// When L: is absent: 1 = derive the unit note length from the meter (ABC
// standard: meter < 3/4 -> 1/16, otherwise 1/8); 0 = use the fixed
// ABC_DEFAULT_NOTE_NUM/DEN below regardless of meter.
#ifndef ABC_DERIVE_LENGTH_FROM_METER
#define ABC_DERIVE_LENGTH_FROM_METER 1
#endif
#ifndef ABC_DEFAULT_NOTE_NUM
#define ABC_DEFAULT_NOTE_NUM 1     // Unit note length numerator when L: is absent
#endif
#ifndef ABC_DEFAULT_NOTE_DEN
#define ABC_DEFAULT_NOTE_DEN 4     // Unit note length denominator when L: is absent
#endif

// ============================================================================
// Types
// ============================================================================

// Note names (C=0, D=1, E=2, F=3, G=4, A=5, B=6)
typedef enum {
    NOTE_C = 0,
    NOTE_D,
    NOTE_E,
    NOTE_F,
    NOTE_G,
    NOTE_A,
    NOTE_B,
    NOTE_REST
} NoteName;

// Accidentals (stored as int8_t: -2 to +3)
#define ACC_DOUBLE_FLAT  -2
#define ACC_FLAT         -1
#define ACC_NONE          0
#define ACC_SHARP         1
#define ACC_NATURAL       2  // Explicit natural, cancels key sig
#define ACC_DOUBLE_SHARP  3

// Note structure - supports chords (multiple pitches with same duration)
// Uses index-based linking instead of pointers for relocatable memory
// Only stores MIDI notes - frequency/name/octave computed via API functions
// Duration stored as MIDI ticks (PPQ=48 means 48 ticks = quarter note)
struct note {
    int16_t next_index;         // Index of next note (-1 = end of list)
    uint8_t duration;           // Duration in MIDI ticks (PPQ-based, max 255 ticks)
    uint8_t chord_size;         // Number of notes in chord (1 = single note)
    uint8_t midi_note[ABC_MAX_CHORD_NOTES];  // MIDI note numbers (0-127, 0 = rest)
};

// Note pool structure (one per voice)
// Initialize with note_pool_init() before use
typedef struct {
    struct note *notes;      // Pointer to notes array (user provides storage)
    char voice_id[ABC_MAX_VOICE_ID_LEN];  // Voice identifier (e.g., "SINE", "SQUARE")
    int16_t head_index;      // Index of first note (-1 = empty)
    int16_t tail_index;      // Index of last note (-1 = empty)
    uint16_t count;          // Number of notes currently in use
    uint16_t capacity;       // Max notes this pool can hold
    uint32_t total_ticks;    // Total duration in MIDI ticks for this voice
    uint8_t max_chord_notes; // Max notes per chord (for validation)
} NotePool;

// Sheet structure - contains the parsed music (all statically allocated)
struct sheet {
    NotePool *pools;            // Pointer to array of note pools (one per voice)
    uint8_t pool_count;         // Number of pools provided
    uint8_t voice_count;        // Number of voices actually used

    uint16_t tempo_bpm;         // Q: field (beats per minute)

    // Metadata from ABC header (statically allocated)
    char title[ABC_MAX_TITLE_LEN];
    char composer[ABC_MAX_COMPOSER_LEN];
    char key[ABC_MAX_KEY_LEN];
    uint8_t default_note_num;   // L: numerator (e.g., 1 in 1/8)
    uint8_t default_note_den;   // L: denominator (e.g., 8 in 1/8)
    uint8_t meter_num;          // M: numerator (e.g., 4 in 4/4)
    uint8_t meter_den;          // M: denominator (e.g., 4 in 4/4)
    uint8_t tempo_note_num;     // Q: note numerator (e.g., 1 in Q:1/4=120)
    uint8_t tempo_note_den;     // Q: note denominator (e.g., 4 in Q:1/4=120)
};

// Frequency lookup table indexed by MIDI note (0-127), stored as freq * 10
// Index directly with MIDI note number for O(1) lookup
// Covers MIDI notes 12-95 (C0-B6), values outside range return 0 or clamped
extern const uint16_t midi_frequencies_x10[128];

// ============================================================================
// Memory Pool Functions
// ============================================================================

// Initialize a note pool with external buffer
// buffer: pre-allocated array of struct note (user provides storage)
// capacity: number of notes the buffer can hold
// max_chord_notes: maximum simultaneous notes per chord (clamped to ABC_MAX_CHORD_NOTES)
void note_pool_init(NotePool *pool, struct note *buffer, uint16_t capacity, uint8_t max_chord_notes);

// Reset pool (reuse memory for new parse)
void note_pool_reset(NotePool *pool);

// Get remaining capacity
int note_pool_available(const NotePool *pool);

// ============================================================================
// Parser Functions
// ============================================================================

// Initialize a sheet structure with an array of note pools
// pools: array of NotePool structs (must be initialized with note_pool_init first)
// pool_count: number of pools in the array (determines max voices)
void sheet_init(struct sheet *sheet, NotePool *pools, uint8_t pool_count);

// Parse ABC notation into pre-allocated sheet
// Returns 0 on success, negative on error
//   -1: NULL input
//   -2: Note pool exhausted
int abc_parse(struct sheet *sheet, const char *abc_string);

// Reset sheet for reuse (also resets all note pools)
void sheet_reset(struct sheet *sheet);

// Print the parsed sheet (for debugging)
#ifndef ABC_NO_PRINT
void sheet_print(const struct sheet *sheet);
#endif

// ============================================================================
// Note Access Functions
// ============================================================================

// Get note by index from a specific pool (NULL if invalid)
struct note *note_get(const NotePool *pool, int index);

// Get first note in a pool (NULL if empty)
struct note *pool_first_note(const NotePool *pool);

// Get next note after given note in a pool (NULL if end)
struct note *note_next(const NotePool *pool, const struct note *current);

// Legacy functions for single-voice compatibility (uses first pool)
struct note *sheet_first_note(const struct sheet *sheet);

// ============================================================================
// Utility Functions
// ============================================================================

// Convert note properties to MIDI/frequency (used internally by parser)
#ifndef ABC_NO_FLOAT
float note_to_frequency(NoteName name, int octave, int8_t acc);
#endif
int note_to_midi(NoteName name, int octave, int8_t acc);

// Get note properties from MIDI number (use these to decode stored notes)
uint16_t midi_to_frequency_x10(uint8_t midi);  // Returns frequency * 10 (direct table lookup)
NoteName midi_to_note_name(uint8_t midi);       // Returns note name (C, D, E, etc.)
uint8_t midi_to_octave(uint8_t midi);           // Returns octave (0-10)
int midi_is_rest(uint8_t midi);                 // Returns 1 if rest (midi == 0)

// Duration conversion (MIDI ticks to milliseconds)
// ticks_to_ms(ticks, bpm) = ticks * 60000 / (bpm * PPQ)
uint16_t ticks_to_ms(uint8_t ticks, uint16_t bpm);  // Convert ticks to milliseconds
uint32_t pool_total_ms(const NotePool *pool, uint16_t bpm);  // Total duration in ms

// String conversion helpers
const char *note_name_to_string(NoteName name);
const char *accidental_to_string(int8_t acc);

// ============================================================================
// Quality-of-life API (all additive; see DESIGN_QOL.md)
// ============================================================================

// --- Setup ------------------------------------------------------------------

// One-shot init: slice one contiguous note buffer into `voices` equal pools and
// initialize the sheet. Replaces the manual pointer-array + per-voice loop.
// Returns 0 on success, -1 on bad arguments.
int abc_sheet_setup(struct sheet *sheet, NotePool *pools, struct note *buffer,
                    uint8_t voices, uint16_t notes_per_voice, uint8_t max_chord_notes);

// Bytes of note storage required to back `voices` pools of `notes_per_voice`.
size_t abc_storage_bytes(uint8_t voices, uint16_t notes_per_voice);

// Declare static pools + storage + sheet for the common static case.
// Produces <name>_pools, <name>_storage and <name>. Pair with abc_sheet_setup:
//   ABC_DEFINE_SHEET(g, 2, 256);
//   abc_sheet_setup(&g, g_pools, g_storage, 2, 256, ABC_MAX_CHORD_NOTES);
#define ABC_DEFINE_SHEET(name, VOICES, NOTES_PER_VOICE)                       \
    static NotePool    name##_pools[(VOICES)];                                \
    static struct note name##_storage[(size_t)(VOICES) * (NOTES_PER_VOICE)];  \
    static struct sheet name

// --- Iteration --------------------------------------------------------------

// Zero-cost for-each over a pool's notes:
//   ABC_FOR_EACH_NOTE(n, pool) { ... use n ... }
#define ABC_FOR_EACH_NOTE(n, pool) \
    for (struct note *n = pool_first_note(pool); n; n = note_next((pool), n))

// Zero-cost for-each over a note's chord members:
//   ABC_FOR_EACH_CHORD_NOTE(i, n) { ... use n->midi_note[i] ... }
#define ABC_FOR_EACH_CHORD_NOTE(i, n) \
    for (uint8_t i = 0; i < (n)->chord_size; i++)

// Resumable iterator (for suspended cursors such as per-voice playback state).
typedef struct {
    const NotePool *pool;
    struct note    *cur;   // next note to return (NULL = exhausted)
    uint8_t         loop;  // if set, next() wraps to the first note instead of ending
} AbcNoteIter;

void         abc_note_iter_init(AbcNoteIter *it, const NotePool *pool, uint8_t loop);
struct note *abc_note_iter_next(AbcNoteIter *it);   // returns note then advances

// Bounds-checked voice accessor (NULL if i >= voice_count).
NotePool    *abc_sheet_voice(const struct sheet *sheet, uint8_t i);

// --- Note inspection --------------------------------------------------------

// 1 if the note is a rest (or empty chord), 0 otherwise.
int abc_note_is_rest(const struct note *n);

// Frequency of a chord member, rest- and bounds-safe (returns 0 for rest/oob).
uint16_t abc_note_frequency_x10(const struct note *n, uint8_t chord_idx);

// --- Duration ---------------------------------------------------------------

// Note duration in output samples at the given tempo and sample rate.
uint32_t abc_ticks_to_samples(uint8_t ticks, uint16_t bpm, uint32_t sample_rate);

// Convenience over ticks_to_ms(n->duration, bpm).
uint16_t abc_note_duration_ms(const struct note *n, uint16_t bpm);

// --- Errors / sizing --------------------------------------------------------

// Human-readable string for an abc_parse() return code.
const char *abc_strerror(int code);

// Dry-run note counter: parses without writing pools so callers can size
// storage up front. Writes up to max_voices counts into per_voice_out and
// returns the voice count (clamped to ABC_COUNT_MAX_VOICES), or negative on
// error (same codes as abc_parse).
int abc_count_notes(const char *abc, uint16_t *per_voice_out, uint8_t max_voices);

// --- Float conveniences (excluded under ABC_NO_FLOAT) -----------------------

#ifndef ABC_NO_FLOAT
// MIDI note -> frequency in Hz.
static inline float abc_midi_to_frequency(uint8_t midi) {
    return midi_to_frequency_x10(midi) / 10.0f;
}
// Chord-member frequency in Hz, rest- and bounds-safe (0.0f for rest/oob).
static inline float abc_note_frequency(const struct note *n, uint8_t chord_idx) {
    return abc_note_frequency_x10(n, chord_idx) / 10.0f;
}
#endif

#ifdef __cplusplus
}
#endif

#endif // ABC_PARSER_H
