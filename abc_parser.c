#include "abc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

// Key signature accidentals (sharps/flats for each note in the key)
// Index corresponds to NoteName enum
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

// Parser state
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

} ParserState;

// Forward declarations
static void parse_header(ParserState *state, struct sheet *sheet);
static void parse_notes(ParserState *state, struct sheet *sheet);
static struct note *parse_single_note(ParserState *state);
static char peek(ParserState *state);
static char advance(ParserState *state);
static void skip_whitespace(ParserState *state);
static float calculate_duration_ms(ParserState *state, int num, int den);

// Utility function implementations
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

struct note *note_create(NoteName name, int octave, Accidental acc, float duration_ms) {
    struct note *n = malloc(sizeof(struct note));
    if (!n) return NULL;

    n->next = NULL;
    n->note_name = name;
    n->octave = octave;
    n->accidental = acc;
    n->duration_ms = duration_ms;
    n->frequency = note_to_frequency(name, octave, acc);
    n->midi_note = note_to_midi(name, octave, acc);
    n->velocity = 1.0f;

    return n;
}

struct note *note_copy(const struct note *note) {
    if (!note) return NULL;

    struct note *copy = malloc(sizeof(struct note));
    if (!copy) return NULL;

    *copy = *note;
    copy->next = NULL;

    return copy;
}

void note_append(struct sheet *sheet, struct note *note) {
    if (!sheet || !note) return;

    if (!sheet->head) {
        sheet->head = note;
        sheet->tail = note;
    } else {
        sheet->tail->next = note;
        sheet->tail = note;
    }
    sheet->note_count++;
    sheet->total_duration_ms += note->duration_ms;
}

// Parser helper functions
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
    // Calculate duration based on default note length and tempo
    // Default: 1/8 note at 120 BPM
    // At 120 BPM, a quarter note = 500ms, so eighth note = 250ms

    float quarter_note_ms = 60000.0f / state->tempo_bpm;
    float default_note_ms = quarter_note_ms * 4.0f * state->default_num / state->default_den;

    return default_note_ms * num / den;
}

static void set_key_signature(ParserState *state, const char *key) {
    // Find matching key signature
    for (int i = 0; key_signatures[i].name != NULL; i++) {
        if (strcmp(key_signatures[i].name, key) == 0) {
            memcpy(state->key_accidentals, key_signatures[i].accidentals,
                   sizeof(state->key_accidentals));
            return;
        }
    }
    // Default to C major if not found
    memset(state->key_accidentals, ACC_NONE, sizeof(state->key_accidentals));
}

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
                        state->input[end + 1] == ':' && isalpha(c)) {
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

            char *value = malloc(val_end - start + 1);
            if (value) {
                memcpy(value, state->input + start, val_end - start);
                value[val_end - start] = '\0';

                switch (field) {
                    case 'T': // Title
                        free(sheet->title);
                        sheet->title = value;
                        value = NULL;
                        break;

                    case 'C': // Composer
                        free(sheet->composer);
                        sheet->composer = value;
                        value = NULL;
                        break;

                    case 'L': // Default note length
                        if (sscanf(value, "%d/%d", &state->default_num, &state->default_den) == 2) {
                            sheet->default_note_num = state->default_num;
                            sheet->default_note_den = state->default_den;
                        }
                        break;

                    case 'M': // Meter
                        if (sscanf(value, "%d/%d", &state->meter_num, &state->meter_den) == 2) {
                            sheet->meter_num = state->meter_num;
                            sheet->meter_den = state->meter_den;
                        }
                        break;

                    case 'Q': // Tempo
                        state->tempo_bpm = atoi(value);
                        if (state->tempo_bpm <= 0) state->tempo_bpm = 120;
                        sheet->tempo_bpm = state->tempo_bpm;
                        break;

                    case 'K': // Key
                        free(sheet->key);
                        sheet->key = value;
                        value = NULL;
                        set_key_signature(state, sheet->key);
                        state->pos = end;
                        free(value);
                        return; // K: ends the header

                    default:
                        break;
                }
                free(value);
            }

            state->pos = end;
        } else {
            // Not a header field, must be music
            break;
        }
    }
}

static struct note *parse_single_note(ParserState *state) {
    skip_whitespace(state);

    if (state->pos >= state->len) return NULL;

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
    int octave = 4; // Default octave (middle C octave)

    if (c >= 'C' && c <= 'G') {
        name = (NoteName)(c - 'C');
        octave = 4;
        advance(state);
    } else if (c == 'A' || c == 'B') {
        name = (c == 'A') ? NOTE_A : NOTE_B;
        octave = 3; // A and B below middle C
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
        return NULL;
    }

    // Apply key signature if no explicit accidental
    if (!explicit_accidental && name != NOTE_REST) {
        // Check bar accidentals first
        if (state->bar_accidentals[name] != ACC_NONE) {
            acc = state->bar_accidentals[name];
        } else {
            acc = state->key_accidentals[name];
        }
    } else if (explicit_accidental && name != NOTE_REST) {
        // Store explicit accidental for this bar
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
            // Bare / means /2
            den = 2;
            // Multiple slashes: // = /4, /// = /8, etc.
            while (peek(state) == '/') {
                advance(state);
                den *= 2;
            }
        }
    }

    float duration = calculate_duration_ms(state, num, den);
    return note_create(name, octave, acc, duration);
}

static void copy_notes_to_sheet(struct sheet *sheet, struct note *source) {
    struct note *current = source;
    while (current) {
        struct note *copy = note_copy(current);
        if (copy) {
            note_append(sheet, copy);
        }
        current = current->next;
    }
}

static void free_note_list(struct note *head) {
    while (head) {
        struct note *next = head->next;
        free(head);
        head = next;
    }
}

static void parse_notes(ParserState *state, struct sheet *sheet) {
    // Repetition handling
    struct note *repeat_start = NULL;
    struct note *repeat_end = NULL;
    int in_repeat = 0;

    // Temporary storage for repeat section
    struct note *repeat_section_head = NULL;
    struct note *repeat_section_tail = NULL;

    while (state->pos < state->len) {
        skip_whitespace(state);
        if (state->pos >= state->len) break;

        char c = peek(state);

        // Bar line handling
        if (c == '|') {
            advance(state);
            c = peek(state);

            // Reset bar accidentals at bar lines
            memset(state->bar_accidentals, ACC_NONE, sizeof(state->bar_accidentals));

            if (c == ':') {
                // |: Start repeat
                advance(state);
                in_repeat = 1;
                // Clear previous repeat section
                free_note_list(repeat_section_head);
                repeat_section_head = NULL;
                repeat_section_tail = NULL;
            } else if (c == '|') {
                // || Double bar
                advance(state);
            } else if (c == ']') {
                // |] End bar
                advance(state);
            }
            continue;
        }

        if (c == ':') {
            advance(state);
            c = peek(state);
            if (c == '|') {
                // :| End repeat - copy the section again
                advance(state);

                // Check for :|: (repeat both directions)
                if (peek(state) == ':') {
                    advance(state);
                    // Copy repeat section and start new repeat
                    copy_notes_to_sheet(sheet, repeat_section_head);
                } else {
                    // Just end repeat - copy section
                    copy_notes_to_sheet(sheet, repeat_section_head);
                    in_repeat = 0;
                    free_note_list(repeat_section_head);
                    repeat_section_head = NULL;
                    repeat_section_tail = NULL;
                }
            }
            continue;
        }

        // Skip other non-note characters
        if (c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '"' || c == '!' ||
            c == '+' || c == '-' || c == '<' || c == '>' ||
            c == '~' || c == '%') {

            // Skip quoted strings
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
        struct note *note = parse_single_note(state);
        if (note) {
            // Add to main sheet
            note_append(sheet, note);

            // If in repeat section, also store a copy
            if (in_repeat) {
                struct note *copy = note_copy(note);
                if (copy) {
                    if (!repeat_section_head) {
                        repeat_section_head = copy;
                        repeat_section_tail = copy;
                    } else {
                        repeat_section_tail->next = copy;
                        repeat_section_tail = copy;
                    }
                }
            }
        } else {
            // Unknown character, skip
            advance(state);
        }
    }

    // Clean up
    free_note_list(repeat_section_head);
}

struct sheet *abc_parse(const char *abc_string) {
    if (!abc_string) return NULL;

    struct sheet *sheet = calloc(1, sizeof(struct sheet));
    if (!sheet) return NULL;

    // Initialize defaults
    sheet->tempo_bpm = 120;
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;

    ParserState state = {
        .input = abc_string,
        .pos = 0,
        .len = strlen(abc_string),
        .default_num = 1,
        .default_den = 8,
        .tempo_bpm = 120,
        .meter_num = 4,
        .meter_den = 4
    };

    memset(state.key_accidentals, ACC_NONE, sizeof(state.key_accidentals));
    memset(state.bar_accidentals, ACC_NONE, sizeof(state.bar_accidentals));

    // Parse header fields
    parse_header(&state, sheet);

    // Parse notes
    parse_notes(&state, sheet);

    return sheet;
}

void sheet_free(struct sheet *sheet) {
    if (!sheet) return;

    struct note *current = sheet->head;
    while (current) {
        struct note *next = current->next;
        free(current);
        current = next;
    }

    free(sheet->title);
    free(sheet->composer);
    free(sheet->key);
    free(sheet);
}

void sheet_print(const struct sheet *sheet) {
    if (!sheet) {
        printf("(null sheet)\n");
        return;
    }

    printf("=== Sheet Music ===\n");

    if (sheet->title) printf("Title: %s\n", sheet->title);
    if (sheet->composer) printf("Composer: %s\n", sheet->composer);
    if (sheet->key) printf("Key: %s\n", sheet->key);

    printf("Tempo: %d BPM\n", sheet->tempo_bpm);
    printf("Meter: %d/%d\n", sheet->meter_num, sheet->meter_den);
    printf("Default note: %d/%d\n", sheet->default_note_num, sheet->default_note_den);
    printf("Total notes: %d\n", sheet->note_count);
    printf("Total duration: %.2f ms (%.2f seconds)\n",
           sheet->total_duration_ms, sheet->total_duration_ms / 1000.0f);

    printf("\n--- Notes ---\n");
    printf("%-4s %-6s %-4s %-10s %-10s %-6s\n",
           "#", "Note", "Oct", "Freq (Hz)", "Dur (ms)", "MIDI");
    printf("------------------------------------------------\n");

    int i = 1;
    struct note *current = sheet->head;
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

        current = current->next;
        i++;
    }

    printf("================================================\n");
}
