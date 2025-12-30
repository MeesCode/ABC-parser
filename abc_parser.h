#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <stdint.h>

// ============================================================================
// Configuration - adjust these for your embedded system
// ============================================================================

#ifndef ABC_MAX_NOTES
#define ABC_MAX_NOTES 128          // Maximum notes in a sheet
#endif

#ifndef ABC_MAX_TITLE_LEN
#define ABC_MAX_TITLE_LEN 32       // Maximum title string length
#endif

#ifndef ABC_MAX_COMPOSER_LEN
#define ABC_MAX_COMPOSER_LEN 32    // Maximum composer string length
#endif

#ifndef ABC_MAX_KEY_LEN
#define ABC_MAX_KEY_LEN 8          // Maximum key string length
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

// Note structure - optimized for minimal memory
// Uses index-based linking instead of pointers for relocatable memory
struct note {
    int16_t next_index;     // Index of next note (-1 = end of list)
    uint16_t duration_ms;   // Duration in milliseconds (max ~65 seconds)
    uint16_t frequency_x10; // Frequency * 10 in Hz (e.g., 4400 = 440.0 Hz)
    uint8_t midi_note;      // MIDI note number (0-127)
    uint8_t note_name : 4;  // NoteName (0-7)
    uint8_t octave : 4;     // Octave (0-6)
    int8_t accidental;      // Accidental (-2 to +3)
};

// Pre-allocated note pool
typedef struct {
    struct note notes[ABC_MAX_NOTES];
    uint16_t count;         // Number of notes currently in use
    uint16_t capacity;      // Always ABC_MAX_NOTES
} NotePool;

// Sheet structure - contains the parsed music (all statically allocated)
struct sheet {
    NotePool *note_pool;    // Pointer to external note pool
    int16_t head_index;     // Index of first note (-1 = empty)
    int16_t tail_index;     // Index of last note (-1 = empty)
    uint16_t note_count;    // Number of notes in this sheet
    uint16_t tempo_bpm;     // Q: field (beats per minute)
    uint32_t total_duration_ms; // Total duration of the sheet

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

// Frequency lookup table [semitone][octave] stored as freq * 10
// semitone: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
// octave: 0-6 (octave 4 is middle C)
extern const uint16_t frequencies_x10[12][7];

// ============================================================================
// Memory Pool Functions
// ============================================================================

// Initialize a note pool (call once at startup)
void note_pool_init(NotePool *pool);

// Reset pool (reuse memory for new parse)
void note_pool_reset(NotePool *pool);

// Get remaining capacity
int note_pool_available(const NotePool *pool);

// ============================================================================
// Parser Functions
// ============================================================================

// Initialize a sheet structure (call before parsing)
void sheet_init(struct sheet *sheet, NotePool *pool);

// Parse ABC notation into pre-allocated sheet
// Returns 0 on success, negative on error
//   -1: NULL input
//   -2: Note pool exhausted
int abc_parse(struct sheet *sheet, const char *abc_string);

// Reset sheet for reuse (also resets the note pool)
void sheet_reset(struct sheet *sheet);

// Print the parsed sheet (for debugging)
void sheet_print(const struct sheet *sheet);

// ============================================================================
// Note Access Functions
// ============================================================================

// Get note by index from pool (NULL if invalid)
struct note *note_get(const struct sheet *sheet, int index);

// Get first note in sheet (NULL if empty)
struct note *sheet_first_note(const struct sheet *sheet);

// Get next note after given note (NULL if end)
struct note *note_next(const struct sheet *sheet, const struct note *current);

// ============================================================================
// Utility Functions
// ============================================================================

float note_to_frequency(NoteName name, int octave, int8_t acc);
int note_to_midi(NoteName name, int octave, int8_t acc);
const char *note_name_to_string(NoteName name);
const char *accidental_to_string(int8_t acc);

#endif // ABC_PARSER_H
