#ifndef ABC_PARSER_H
#define ABC_PARSER_H

#include <stddef.h>

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
struct note {
    struct note *next;
    float frequency;        // Frequency in Hz
    float duration_ms;      // Duration in milliseconds
    NoteName note_name;     // The note name (C, D, E, etc.)
    int octave;             // Octave number (4 = middle octave)
    Accidental accidental;  // Sharp, flat, natural
    int midi_note;          // MIDI note number for convenience
    float velocity;         // Note velocity/volume (0.0 - 1.0)
};

// Sheet structure - contains the parsed music
struct sheet {
    struct note *head;
    struct note *tail;      // For efficient appending
    int note_count;

    // Metadata from ABC header
    char *title;            // T: field
    char *composer;         // C: field
    char *key;              // K: field (e.g., "Amin", "G", "D")
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

// Parser functions
struct sheet *abc_parse(const char *abc_string);
void sheet_free(struct sheet *sheet);
void sheet_print(const struct sheet *sheet);

// Note manipulation
struct note *note_create(NoteName name, int octave, Accidental acc, float duration_ms);
void note_append(struct sheet *sheet, struct note *note);
struct note *note_copy(const struct note *note);

// Utility functions
float note_to_frequency(NoteName name, int octave, Accidental acc);
int note_to_midi(NoteName name, int octave, Accidental acc);
const char *note_name_to_string(NoteName name);
const char *accidental_to_string(Accidental acc);

#endif // ABC_PARSER_H
