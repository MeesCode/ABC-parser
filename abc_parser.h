#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <stdint.h>

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
void sheet_print(const struct sheet *sheet);

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
float note_to_frequency(NoteName name, int octave, int8_t acc);
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

#endif // ABC_PARSER_H
