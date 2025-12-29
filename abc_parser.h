#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <stddef.h>

// ============================================================================
// Configuration - adjust these for your embedded system
// ============================================================================

#ifndef ABC_MAX_NOTES
#define ABC_MAX_NOTES 512          // Maximum notes in a sheet
#endif

#ifndef ABC_MAX_TITLE_LEN
#define ABC_MAX_TITLE_LEN 64       // Maximum title string length
#endif

#ifndef ABC_MAX_COMPOSER_LEN
#define ABC_MAX_COMPOSER_LEN 64    // Maximum composer string length
#endif

#ifndef ABC_MAX_KEY_LEN
#define ABC_MAX_KEY_LEN 16         // Maximum key string length
#endif

// ============================================================================
// Types
// ============================================================================

// Note names (C=0, D=1, E=2, F=3, G=4, A=5, B=6)
typedef enum {
    NOTE_C = 0,
    NOTE_D = 1,
    NOTE_E = 2,
    NOTE_F = 3,
    NOTE_G = 4,
    NOTE_A = 5,
    NOTE_B = 6,
    NOTE_REST = 7
} NoteName;

// Accidentals
typedef enum {
    ACC_NONE = 0,
    ACC_SHARP = 1,      // ^
    ACC_FLAT = -1,      // _
    ACC_NATURAL = 2,    // =
    ACC_DOUBLE_SHARP = 3,
    ACC_DOUBLE_FLAT = -2
} Accidental;

// Note structure - contains technical data for synthesis
// Uses index-based linking instead of pointers for relocatable memory
struct note {
    int next_index;         // Index of next note (-1 = end of list)
    float frequency;        // Frequency in Hz
    float duration_ms;      // Duration in milliseconds
    NoteName note_name;     // The note name (C, D, E, etc.)
    int octave;             // Octave number (4 = middle octave)
    Accidental accidental;  // Sharp, flat, natural
    int midi_note;          // MIDI note number for convenience
    float velocity;         // Note velocity/volume (0.0 - 1.0)
};

// Pre-allocated note pool
typedef struct {
    struct note notes[ABC_MAX_NOTES];
    int count;              // Number of notes currently in use
    int capacity;           // Always ABC_MAX_NOTES
} NotePool;

// Sheet structure - contains the parsed music (all statically allocated)
struct sheet {
    NotePool *note_pool;    // Pointer to external note pool
    int head_index;         // Index of first note (-1 = empty)
    int tail_index;         // Index of last note (-1 = empty)
    int note_count;         // Number of notes in this sheet

    // Metadata from ABC header (statically allocated)
    char title[ABC_MAX_TITLE_LEN];
    char composer[ABC_MAX_COMPOSER_LEN];
    char key[ABC_MAX_KEY_LEN];
    int tempo_bpm;          // Q: field (beats per minute)
    int default_note_num;   // L: numerator (e.g., 1 in 1/8)
    int default_note_den;   // L: denominator (e.g., 8 in 1/8)
    int meter_num;          // M: numerator (e.g., 4 in 4/4)
    int meter_den;          // M: denominator (e.g., 4 in 4/4)
    float total_duration_ms;// Total duration of the sheet
};

// Frequency lookup table [semitone][octave]
// semitone: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
// octave: 0-6 (octave 4 is middle C)
extern const float frequencies[12][7];

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

float note_to_frequency(NoteName name, int octave, Accidental acc);
int note_to_midi(NoteName name, int octave, Accidental acc);
const char *note_name_to_string(NoteName name);
const char *accidental_to_string(Accidental acc);

#endif // ABC_PARSER_H
