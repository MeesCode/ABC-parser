#include "abc_parser.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Frequency lookup table
// ============================================================================

// Frequency lookup table [semitone][octave]
// Octaves 0-6, where octave 4 contains A440
const float frequencies[12][7] = {
    { 16.35f,  32.70f,  65.41f, 130.81f, 261.63f,  523.25f, 1046.50f }, // C
    { 17.32f,  34.65f,  69.30f, 138.59f, 277.18f,  554.37f, 1108.73f }, // C#/Db
    { 18.35f,  36.71f,  73.42f, 146.83f, 293.66f,  587.33f, 1174.66f }, // D
    { 19.45f,  38.89f,  77.78f, 155.56f, 311.13f,  622.25f, 1244.51f }, // D#/Eb
    { 20.60f,  41.20f,  82.41f, 164.81f, 329.63f,  659.26f, 1318.51f }, // E
    { 21.83f,  43.65f,  87.31f, 174.61f, 349.23f,  698.46f, 1396.91f }, // F
    { 23.12f,  46.25f,  92.50f, 185.00f, 369.99f,  739.99f, 1479.98f }, // F#/Gb
    { 24.50f,  49.00f,  98.00f, 196.00f, 392.00f,  783.99f, 1567.98f }, // G
    { 25.96f,  51.91f, 103.83f, 207.65f, 415.30f,  830.61f, 1661.22f }, // G#/Ab
    { 27.50f,  55.00f, 110.00f, 220.00f, 440.00f,  880.00f, 1760.00f }, // A
    { 29.14f,  58.27f, 116.54f, 233.08f, 466.16f,  932.33f, 1864.66f }, // A#/Bb
    { 30.87f,  61.74f, 123.47f, 246.94f, 493.88f,  987.77f, 1975.53f }, // B
};

// Map note names to semitone offsets from C
static const int note_to_semitone[7] = {
    0,  // C
    2,  // D
    4,  // E
    5,  // F
    7,  // G
    9,  // A
    11  // B
};

// ============================================================================
// Key signature data
// ============================================================================

typedef struct {
    const char *name;
    Accidental accidentals[7]; // Accidentals for C, D, E, F, G, A, B
} KeySignature;

static const KeySignature key_signatures[] = {
    // Major keys
    {"C",    {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"G",    {ACC_NONE, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"D",    {ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"A",    {ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"E",    {ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"B",    {ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_NONE}},
    {"F#",   {ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_NONE}},
    {"F",    {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Bb",   {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Eb",   {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {"Ab",   {ACC_NONE, ACC_FLAT, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {"Db",   {ACC_NONE, ACC_FLAT, ACC_FLAT, ACC_NONE, ACC_FLAT, ACC_FLAT, ACC_FLAT}},
    // Minor keys
    {"Am",   {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"Amin", {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"Em",   {ACC_NONE, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"Emin", {ACC_NONE, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"Bm",   {ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"Bmin", {ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_NONE, ACC_NONE, ACC_NONE}},
    {"F#m",  {ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"F#min",{ACC_SHARP, ACC_NONE, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"C#m",  {ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"C#min",{ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_NONE}},
    {"G#m",  {ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_NONE}},
    {"G#min",{ACC_SHARP, ACC_SHARP, ACC_NONE, ACC_SHARP, ACC_SHARP, ACC_SHARP, ACC_NONE}},
    {"Dm",   {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Dmin", {ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Gm",   {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Gmin", {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_NONE, ACC_FLAT}},
    {"Cm",   {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {"Cmin", {ACC_NONE, ACC_NONE, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {"Fm",   {ACC_NONE, ACC_FLAT, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {"Fmin", {ACC_NONE, ACC_FLAT, ACC_FLAT, ACC_NONE, ACC_NONE, ACC_FLAT, ACC_FLAT}},
    {NULL, {0}}
};

// ============================================================================
// Parser state (stack allocated during parsing)
// ============================================================================

typedef struct {
    const char *input;
    size_t pos;
    size_t len;

    // Default note length
    int default_num;
    int default_den;

    // Tempo
    int tempo_bpm;

    // Key signature
    Accidental key_accidentals[7];

    // Per-bar accidentals (reset at bar lines)
    Accidental bar_accidentals[7];

    // Meter
    int meter_num;
    int meter_den;

    // Repeat section tracking (indices into note pool)
    int repeat_start_index;
    int repeat_end_index;
    int in_repeat;

} ParserState;

// ============================================================================
// Utility functions
// ============================================================================

float note_to_frequency(NoteName name, int octave, Accidental acc) {
    if (name == NOTE_REST) {
        return 0.0f;
    }

    int semitone = note_to_semitone[name];

    // Apply accidental
    if (acc == ACC_SHARP) semitone += 1;
    else if (acc == ACC_FLAT) semitone -= 1;
    else if (acc == ACC_DOUBLE_SHARP) semitone += 2;
    else if (acc == ACC_DOUBLE_FLAT) semitone -= 2;

    // Handle octave wraparound
    while (semitone < 0) {
        semitone += 12;
        octave--;
    }
    while (semitone >= 12) {
        semitone -= 12;
        octave++;
    }

    // Clamp octave to valid range
    if (octave < 0) octave = 0;
    if (octave > 6) octave = 6;

    return frequencies[semitone][octave];
}

int note_to_midi(NoteName name, int octave, Accidental acc) {
    if (name == NOTE_REST) {
        return 0;
    }

    int semitone = note_to_semitone[name];

    if (acc == ACC_SHARP) semitone += 1;
    else if (acc == ACC_FLAT) semitone -= 1;
    else if (acc == ACC_DOUBLE_SHARP) semitone += 2;
    else if (acc == ACC_DOUBLE_FLAT) semitone -= 2;

    // MIDI note: C4 = 60
    return 12 + (octave * 12) + semitone;
}

const char *note_name_to_string(NoteName name) {
    static const char *names[] = {"C", "D", "E", "F", "G", "A", "B", "z"};
    if (name >= 0 && name <= NOTE_REST) {
        return names[name];
    }
    return "?";
}

const char *accidental_to_string(Accidental acc) {
    switch (acc) {
        case ACC_SHARP: return "#";
        case ACC_FLAT: return "b";
        case ACC_NATURAL: return "=";
        case ACC_DOUBLE_SHARP: return "##";
        case ACC_DOUBLE_FLAT: return "bb";
        default: return "";
    }
}

// ============================================================================
// Memory pool functions
// ============================================================================

void note_pool_init(NotePool *pool) {
    if (!pool) return;
    pool->count = 0;
    pool->capacity = ABC_MAX_NOTES;
}

void note_pool_reset(NotePool *pool) {
    if (!pool) return;
    pool->count = 0;
}

int note_pool_available(const NotePool *pool) {
    if (!pool) return 0;
    return pool->capacity - pool->count;
}

// Allocate a note from the pool, returns index or -1 if full
static int note_pool_alloc(NotePool *pool) {
    if (!pool || pool->count >= pool->capacity) {
        return -1;
    }
    int index = pool->count;
    pool->count++;

    // Initialize the note
    struct note *n = &pool->notes[index];
    n->next_index = -1;
    n->frequency = 0.0f;
    n->duration_ms = 0.0f;
    n->note_name = NOTE_C;
    n->octave = 4;
    n->accidental = ACC_NONE;
    n->midi_note = 0;
    n->velocity = 1.0f;

    return index;
}

// ============================================================================
// Sheet functions
// ============================================================================

void sheet_init(struct sheet *sheet, NotePool *pool) {
    if (!sheet) return;

    sheet->note_pool = pool;
    sheet->head_index = -1;
    sheet->tail_index = -1;
    sheet->note_count = 0;

    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';

    sheet->tempo_bpm = 120;
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;
    sheet->total_duration_ms = 0.0f;
}

void sheet_reset(struct sheet *sheet) {
    if (!sheet) return;

    if (sheet->note_pool) {
        note_pool_reset(sheet->note_pool);
    }

    sheet->head_index = -1;
    sheet->tail_index = -1;
    sheet->note_count = 0;
    sheet->total_duration_ms = 0.0f;

    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';
}

struct note *note_get(const struct sheet *sheet, int index) {
    if (!sheet || !sheet->note_pool) return NULL;
    if (index < 0 || index >= sheet->note_pool->count) return NULL;
    return &sheet->note_pool->notes[index];
}

struct note *sheet_first_note(const struct sheet *sheet) {
    if (!sheet) return NULL;
    return note_get(sheet, sheet->head_index);
}

struct note *note_next(const struct sheet *sheet, const struct note *current) {
    if (!sheet || !current) return NULL;
    return note_get(sheet, current->next_index);
}

// Append a note to the sheet, returns 0 on success, -1 on pool exhausted
static int sheet_append_note(struct sheet *sheet, NoteName name, int octave,
                              Accidental acc, float duration_ms) {
    if (!sheet || !sheet->note_pool) return -1;

    int index = note_pool_alloc(sheet->note_pool);
    if (index < 0) return -1;

    struct note *n = &sheet->note_pool->notes[index];
    n->note_name = name;
    n->octave = octave;
    n->accidental = acc;
    n->duration_ms = duration_ms;
    n->frequency = note_to_frequency(name, octave, acc);
    n->midi_note = note_to_midi(name, octave, acc);
    n->velocity = 1.0f;
    n->next_index = -1;

    if (sheet->head_index < 0) {
        sheet->head_index = index;
        sheet->tail_index = index;
    } else {
        sheet->note_pool->notes[sheet->tail_index].next_index = index;
        sheet->tail_index = index;
    }

    sheet->note_count++;
    sheet->total_duration_ms += duration_ms;

    return 0;
}

// ============================================================================
// Parser helper functions
// ============================================================================

static char peek(ParserState *state) {
    if (state->pos >= state->len) return '\0';
    return state->input[state->pos];
}

static char advance(ParserState *state) {
    if (state->pos >= state->len) return '\0';
    return state->input[state->pos++];
}

static void skip_whitespace(ParserState *state) {
    while (state->pos < state->len) {
        char c = peek(state);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(state);
        } else {
            break;
        }
    }
}

static float calculate_duration_ms(ParserState *state, int num, int den) {
    float quarter_note_ms = 60000.0f / (float)state->tempo_bpm;
    float default_note_ms = quarter_note_ms * 4.0f * (float)state->default_num / (float)state->default_den;
    return default_note_ms * (float)num / (float)den;
}

static void set_key_signature(ParserState *state, const char *key) {
    for (int i = 0; key_signatures[i].name != NULL; i++) {
        if (strcmp(key_signatures[i].name, key) == 0) {
            memcpy(state->key_accidentals, key_signatures[i].accidentals,
                   sizeof(state->key_accidentals));
            return;
        }
    }
    memset(state->key_accidentals, ACC_NONE, sizeof(state->key_accidentals));
}

static int is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Safe string copy with length limit
static void safe_strcpy(char *dest, size_t dest_size, const char *src, size_t src_len) {
    size_t copy_len = src_len;
    if (copy_len >= dest_size) {
        copy_len = dest_size - 1;
    }
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

// ============================================================================
// Header parsing
// ============================================================================

static void parse_header(ParserState *state, struct sheet *sheet) {
    while (state->pos < state->len) {
        skip_whitespace(state);

        // Check for header field (X: format)
        if (state->pos + 1 < state->len && state->input[state->pos + 1] == ':') {
            char field = state->input[state->pos];

            // Find end of header value
            size_t start = state->pos + 2;
            size_t end = start;

            // Skip to end of line or next header/note
            // For K: field, just read until newline or end of string
            if (field == 'K') {
                while (end < state->len && state->input[end] != '\n' && state->input[end] != '\r') {
                    end++;
                }
                // Skip the newline
                if (end < state->len && (state->input[end] == '\n' || state->input[end] == '\r')) {
                    end++;
                    // Handle \r\n
                    if (end < state->len && state->input[end-1] == '\r' && state->input[end] == '\n') {
                        end++;
                    }
                }
            } else {
                while (end < state->len) {
                    char c = state->input[end];
                    // Check for newline (end of header field)
                    if (c == '\n' || c == '\r') {
                        end++;
                        // Handle \r\n
                        if (end < state->len && state->input[end-1] == '\r' && state->input[end] == '\n') {
                            end++;
                        }
                        break;
                    }
                    // Check for next header field
                    if (end > start && end + 1 < state->len &&
                        state->input[end + 1] == ':' && is_alpha(c)) {
                        break;
                    }
                    end++;
                }
            }

            // Extract value (trim whitespace)
            while (start < end && (state->input[start] == ' ' || state->input[start] == '\t')) {
                start++;
            }
            size_t val_end = end;
            while (val_end > start && (state->input[val_end-1] == ' ' ||
                                        state->input[val_end-1] == '\t' ||
                                        state->input[val_end-1] == '\n' ||
                                        state->input[val_end-1] == '\r')) {
                val_end--;
            }

            size_t value_len = val_end - start;
            const char *value = state->input + start;

            switch (field) {
                case 'T': // Title
                    safe_strcpy(sheet->title, ABC_MAX_TITLE_LEN, value, value_len);
                    break;

                case 'C': // Composer
                    safe_strcpy(sheet->composer, ABC_MAX_COMPOSER_LEN, value, value_len);
                    break;

                case 'L': { // Default note length
                    // Simple parsing without sscanf
                    int num = 0, den = 0;
                    size_t i = 0;
                    while (i < value_len && value[i] >= '0' && value[i] <= '9') {
                        num = num * 10 + (value[i] - '0');
                        i++;
                    }
                    if (i < value_len && value[i] == '/') {
                        i++;
                        while (i < value_len && value[i] >= '0' && value[i] <= '9') {
                            den = den * 10 + (value[i] - '0');
                            i++;
                        }
                    }
                    if (num > 0 && den > 0) {
                        state->default_num = num;
                        state->default_den = den;
                        sheet->default_note_num = num;
                        sheet->default_note_den = den;
                    }
                    break;
                }

                case 'M': { // Meter
                    int num = 0, den = 0;
                    size_t i = 0;
                    while (i < value_len && value[i] >= '0' && value[i] <= '9') {
                        num = num * 10 + (value[i] - '0');
                        i++;
                    }
                    if (i < value_len && value[i] == '/') {
                        i++;
                        while (i < value_len && value[i] >= '0' && value[i] <= '9') {
                            den = den * 10 + (value[i] - '0');
                            i++;
                        }
                    }
                    if (num > 0 && den > 0) {
                        state->meter_num = num;
                        state->meter_den = den;
                        sheet->meter_num = num;
                        sheet->meter_den = den;
                    }
                    break;
                }

                case 'Q': { // Tempo
                    int tempo = 0;
                    for (size_t i = 0; i < value_len; i++) {
                        if (value[i] >= '0' && value[i] <= '9') {
                            tempo = tempo * 10 + (value[i] - '0');
                        }
                    }
                    if (tempo > 0) {
                        state->tempo_bpm = tempo;
                        sheet->tempo_bpm = tempo;
                    }
                    break;
                }

                case 'K': { // Key
                    safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, value, value_len);
                    set_key_signature(state, sheet->key);
                    state->pos = end;
                    return; // K: ends the header
                }

                default:
                    break;
            }

            state->pos = end;
        } else {
            // Not a header field, must be music
            break;
        }
    }
}

// ============================================================================
// Note parsing
// ============================================================================

// Parse a single note, append to sheet. Returns 0 on success, -1 on pool full, 1 if no note parsed
static int parse_single_note(ParserState *state, struct sheet *sheet) {
    skip_whitespace(state);

    if (state->pos >= state->len) return 1;

    char c = peek(state);

    // Parse accidental (before note)
    Accidental acc = ACC_NONE;
    int explicit_accidental = 0;

    while (c == '^' || c == '_' || c == '=') {
        explicit_accidental = 1;
        if (c == '^') {
            if (acc == ACC_SHARP) acc = ACC_DOUBLE_SHARP;
            else acc = ACC_SHARP;
        } else if (c == '_') {
            if (acc == ACC_FLAT) acc = ACC_DOUBLE_FLAT;
            else acc = ACC_FLAT;
        } else if (c == '=') {
            acc = ACC_NATURAL;
        }
        advance(state);
        c = peek(state);
    }

    // Parse note name
    NoteName name;
    int octave = 4;

    if (c >= 'C' && c <= 'G') {
        name = (NoteName)(c - 'C');
        octave = 4;
        advance(state);
    } else if (c == 'A' || c == 'B') {
        name = (c == 'A') ? NOTE_A : NOTE_B;
        octave = 3;
        advance(state);
    } else if (c >= 'c' && c <= 'g') {
        name = (NoteName)(c - 'c');
        octave = 5;
        advance(state);
    } else if (c == 'a' || c == 'b') {
        name = (c == 'a') ? NOTE_A : NOTE_B;
        octave = 4;
        advance(state);
    } else if (c == 'z' || c == 'Z') {
        name = NOTE_REST;
        advance(state);
    } else {
        return 1; // No note found
    }

    // Apply key signature if no explicit accidental
    if (!explicit_accidental && name != NOTE_REST) {
        if (state->bar_accidentals[name] != ACC_NONE) {
            acc = state->bar_accidentals[name];
        } else {
            acc = state->key_accidentals[name];
        }
    } else if (explicit_accidental && name != NOTE_REST) {
        state->bar_accidentals[name] = (acc == ACC_NATURAL) ? ACC_NONE : acc;
        if (acc == ACC_NATURAL) acc = ACC_NONE;
    }

    // Parse octave modifiers
    c = peek(state);
    while (c == '\'' || c == ',') {
        if (c == '\'') octave++;
        else if (c == ',') octave--;
        advance(state);
        c = peek(state);
    }

    // Clamp octave
    if (octave < 0) octave = 0;
    if (octave > 6) octave = 6;

    // Parse duration
    int num = 1;
    int den = 1;
    c = peek(state);

    // Parse numerator
    if (c >= '0' && c <= '9') {
        num = 0;
        while (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
            advance(state);
            c = peek(state);
        }
    }

    // Parse denominator (after /)
    if (c == '/') {
        advance(state);
        c = peek(state);
        if (c >= '0' && c <= '9') {
            den = 0;
            while (c >= '0' && c <= '9') {
                den = den * 10 + (c - '0');
                advance(state);
                c = peek(state);
            }
        } else {
            den = 2;
            while (peek(state) == '/') {
                advance(state);
                den *= 2;
            }
        }
    }

    float duration = calculate_duration_ms(state, num, den);
    return sheet_append_note(sheet, name, octave, acc, duration);
}

// Copy notes from repeat section
static int copy_repeat_section(struct sheet *sheet, int start_index, int end_index) {
    if (!sheet || !sheet->note_pool) return -1;
    if (start_index < 0) return 0;

    int current = start_index;
    while (current >= 0 && current <= end_index && current < sheet->note_pool->count) {
        struct note *src = &sheet->note_pool->notes[current];

        int result = sheet_append_note(sheet, src->note_name, src->octave,
                                        src->accidental, src->duration_ms);
        if (result < 0) return -1;

        current = src->next_index;
        if (current < 0) break;
    }

    return 0;
}

static int parse_notes(ParserState *state, struct sheet *sheet) {
    // Track repeat section by indices
    state->repeat_start_index = -1;
    state->repeat_end_index = -1;
    state->in_repeat = 0;

    while (state->pos < state->len) {
        skip_whitespace(state);
        if (state->pos >= state->len) break;

        char c = peek(state);

        // Bar line handling
        if (c == '|') {
            advance(state);
            c = peek(state);

            // Reset bar accidentals
            memset(state->bar_accidentals, ACC_NONE, sizeof(state->bar_accidentals));

            if (c == ':') {
                // |: Start repeat
                advance(state);
                state->in_repeat = 1;
                state->repeat_start_index = sheet->note_pool ? sheet->note_pool->count : 0;
            } else if (c == '|') {
                advance(state);
            } else if (c == ']') {
                advance(state);
            }
            continue;
        }

        if (c == ':') {
            advance(state);
            c = peek(state);
            if (c == '|') {
                // :| End repeat
                advance(state);

                // Record end of repeat section (before copying)
                state->repeat_end_index = sheet->note_pool ? sheet->note_pool->count - 1 : -1;

                // Check for :|:
                if (peek(state) == ':') {
                    advance(state);
                    // Copy and start new repeat
                    if (copy_repeat_section(sheet, state->repeat_start_index, state->repeat_end_index) < 0) {
                        return -2;
                    }
                    state->repeat_start_index = sheet->note_pool ? sheet->note_pool->count : 0;
                } else {
                    // Copy repeat section
                    if (copy_repeat_section(sheet, state->repeat_start_index, state->repeat_end_index) < 0) {
                        return -2;
                    }
                    state->in_repeat = 0;
                    state->repeat_start_index = -1;
                    state->repeat_end_index = -1;
                }
            }
            continue;
        }

        // Skip non-note characters
        if (c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '"' || c == '!' ||
            c == '+' || c == '-' || c == '<' || c == '>' ||
            c == '~' || c == '%') {

            if (c == '"') {
                advance(state);
                while (state->pos < state->len && peek(state) != '"') {
                    advance(state);
                }
                if (peek(state) == '"') advance(state);
            } else {
                advance(state);
            }
            continue;
        }

        // Try to parse a note
        int result = parse_single_note(state, sheet);
        if (result < 0) {
            return -2; // Pool exhausted
        } else if (result > 0) {
            // Not a note, skip character
            advance(state);
        }
    }

    return 0;
}

// ============================================================================
// Main parse function
// ============================================================================

int abc_parse(struct sheet *sheet, const char *abc_string) {
    if (!sheet || !abc_string) return -1;
    if (!sheet->note_pool) return -1;

    ParserState state = {
        .input = abc_string,
        .pos = 0,
        .len = strlen(abc_string),
        .default_num = sheet->default_note_num,
        .default_den = sheet->default_note_den,
        .tempo_bpm = sheet->tempo_bpm,
        .meter_num = sheet->meter_num,
        .meter_den = sheet->meter_den,
        .repeat_start_index = -1,
        .repeat_end_index = -1,
        .in_repeat = 0
    };

    memset(state.key_accidentals, ACC_NONE, sizeof(state.key_accidentals));
    memset(state.bar_accidentals, ACC_NONE, sizeof(state.bar_accidentals));

    parse_header(&state, sheet);

    int result = parse_notes(&state, sheet);
    if (result < 0) return result;

    return 0;
}

// ============================================================================
// Debug printing
// ============================================================================

void sheet_print(const struct sheet *sheet) {
    if (!sheet) {
        printf("(null sheet)\n");
        return;
    }

    printf("=== Sheet Music ===\n");

    if (sheet->title[0]) printf("Title: %s\n", sheet->title);
    if (sheet->composer[0]) printf("Composer: %s\n", sheet->composer);
    if (sheet->key[0]) printf("Key: %s\n", sheet->key);

    printf("Tempo: %d BPM\n", sheet->tempo_bpm);
    printf("Meter: %d/%d\n", sheet->meter_num, sheet->meter_den);
    printf("Default note: %d/%d\n", sheet->default_note_num, sheet->default_note_den);
    printf("Total notes: %d\n", sheet->note_count);
    printf("Total duration: %.2f ms (%.2f seconds)\n",
           sheet->total_duration_ms, sheet->total_duration_ms / 1000.0f);

    if (sheet->note_pool) {
        printf("Pool usage: %d/%d notes\n", sheet->note_pool->count, sheet->note_pool->capacity);
    }

    printf("\n--- Notes ---\n");
    printf("%-4s %-6s %-4s %-10s %-10s %-6s\n",
           "#", "Note", "Oct", "Freq (Hz)", "Dur (ms)", "MIDI");
    printf("------------------------------------------------\n");

    int i = 1;
    struct note *current = sheet_first_note(sheet);
    while (current) {
        if (current->note_name == NOTE_REST) {
            printf("%-4d %-6s %-4s %-10s %-10.1f %-6s\n",
                   i, "rest", "-", "-", current->duration_ms, "-");
        } else {
            char note_str[8];
            snprintf(note_str, sizeof(note_str), "%s%s",
                     note_name_to_string(current->note_name),
                     accidental_to_string(current->accidental));

            printf("%-4d %-6s %-4d %-10.2f %-10.1f %-6d\n",
                   i, note_str, current->octave,
                   current->frequency, current->duration_ms, current->midi_note);
        }

        current = note_next(sheet, current);
        i++;
    }

    printf("================================================\n");
}
