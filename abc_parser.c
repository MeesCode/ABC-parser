#include "abc_parser.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Frequency lookup table indexed by MIDI note (frequency * 10, stored as uint16_t)
// ============================================================================

// Direct MIDI note to frequency*10 lookup for O(1) access
// MIDI 0 = rest, MIDI 12-95 = C0-B6, values outside usable range are 0 or clamped
const uint16_t midi_frequencies_x10[128] = {
    // MIDI 0-11: Below C0, not typically used (0 = rest)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // MIDI 12-23: Octave 0 (C0-B0)
    164, 173, 184, 195, 206, 218, 231, 245, 260, 275, 291, 309,
    // MIDI 24-35: Octave 1 (C1-B1)
    327, 347, 367, 389, 412, 437, 463, 490, 519, 550, 583, 617,
    // MIDI 36-47: Octave 2 (C2-B2)
    654, 693, 734, 778, 824, 873, 925, 980, 1038, 1100, 1165, 1235,
    // MIDI 48-59: Octave 3 (C3-B3)
    1308, 1386, 1468, 1556, 1648, 1746, 1850, 1960, 2077, 2200, 2331, 2469,
    // MIDI 60-71: Octave 4 (C4-B4) - Middle C is MIDI 60
    2616, 2772, 2937, 3111, 3296, 3492, 3700, 3920, 4153, 4400, 4662, 4939,
    // MIDI 72-83: Octave 5 (C5-B5)
    5233, 5544, 5873, 6223, 6593, 6985, 7400, 7840, 8306, 8800, 9323, 9878,
    // MIDI 84-95: Octave 6 (C6-B6)
    10465, 11087, 11747, 12445, 13185, 13969, 14800, 15680, 16612, 17600, 18647, 19756,
    // MIDI 96-127: Above B6, clamp to highest values (repeat B6 freq)
    19756, 19756, 19756, 19756, 19756, 19756, 19756, 19756,
    19756, 19756, 19756, 19756, 19756, 19756, 19756, 19756,
    19756, 19756, 19756, 19756, 19756, 19756, 19756, 19756,
    19756, 19756, 19756, 19756, 19756, 19756, 19756, 19756
};

// Map note names to semitone offsets from C
static const int8_t note_to_semitone[7] = { 0, 2, 4, 5, 7, 9, 11 };

// ============================================================================
// Key signature data
// ============================================================================

typedef struct {
    const char *name;
    int8_t accidentals[7]; // Accidentals for C, D, E, F, G, A, B
} KeySignature;

static const KeySignature key_signatures[] = {
    {"C",    {0, 0, 0, 0, 0, 0, 0}},
    {"G",    {0, 0, 0, 1, 0, 0, 0}},
    {"D",    {1, 0, 0, 1, 0, 0, 0}},
    {"A",    {1, 0, 0, 1, 1, 0, 0}},
    {"E",    {1, 1, 0, 1, 1, 0, 0}},
    {"B",    {1, 1, 0, 1, 1, 1, 0}},
    {"F#",   {1, 1, 1, 1, 1, 1, 0}},
    {"F",    {0, 0, 0, 0, 0, 0,-1}},
    {"Bb",   {0, 0,-1, 0, 0, 0,-1}},
    {"Eb",   {0, 0,-1, 0, 0,-1,-1}},
    {"Ab",   {0,-1,-1, 0, 0,-1,-1}},
    {"Db",   {0,-1,-1, 0,-1,-1,-1}},
    {"Am",   {0, 0, 0, 0, 0, 0, 0}},
    {"Amin", {0, 0, 0, 0, 0, 0, 0}},
    {"Em",   {0, 0, 0, 1, 0, 0, 0}},
    {"Emin", {0, 0, 0, 1, 0, 0, 0}},
    {"Bm",   {1, 0, 0, 1, 0, 0, 0}},
    {"Bmin", {1, 0, 0, 1, 0, 0, 0}},
    {"F#m",  {1, 0, 0, 1, 1, 0, 0}},
    {"F#min",{1, 0, 0, 1, 1, 0, 0}},
    {"Dm",   {0, 0, 0, 0, 0, 0,-1}},
    {"Dmin", {0, 0, 0, 0, 0, 0,-1}},
    {"Gm",   {0, 0,-1, 0, 0, 0,-1}},
    {"Gmin", {0, 0,-1, 0, 0, 0,-1}},
    {"Cm",   {0, 0,-1, 0, 0,-1,-1}},
    {"Cmin", {0, 0,-1, 0, 0,-1,-1}},
    {"Fm",   {0,-1,-1, 0, 0,-1,-1}},
    {"Fmin", {0,-1,-1, 0, 0,-1,-1}},
    {NULL, {0}}
};

// ============================================================================
// Parser state (stack allocated during parsing)
// ============================================================================

typedef struct {
    const char *input;
    uint16_t pos;
    uint16_t len;
    uint16_t tempo_bpm;
    uint8_t default_num;
    uint8_t default_den;
    uint8_t meter_num;
    uint8_t meter_den;
    uint8_t tempo_note_num;
    uint8_t tempo_note_den;
    int8_t key_accidentals[7];
    int8_t bar_accidentals[7];
    int16_t repeat_start_index;
    int16_t repeat_end_index;
    uint8_t in_repeat;
    uint8_t tuplet_remaining;
    uint8_t tuplet_num;
    uint8_t tuplet_in_time;
    uint8_t current_voice;       // Current voice index
} ParserState;

// ============================================================================
// Utility functions
// ============================================================================

float note_to_frequency(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0.0f;
    int midi = note_to_midi(name, octave, acc);
    return midi_frequencies_x10[midi] / 10.0f;
}

int note_to_midi(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0;
    int semitone = note_to_semitone[name];
    // Handle accidentals: ACC_NATURAL (2) means no modification
    // ACC_DOUBLE_SHARP (3) should add 2 semitones
    if (acc == ACC_DOUBLE_SHARP) semitone += 2;
    else if (acc != ACC_NATURAL) semitone += acc;
    return 12 + (octave * 12) + semitone;
}

// MIDI to frequency lookup - direct table access O(1)
uint16_t midi_to_frequency_x10(uint8_t midi) {
    return midi_frequencies_x10[midi];
}

// Map semitone (0-11) to NoteName
static const NoteName semitone_to_note[12] = {
    NOTE_C, NOTE_C, NOTE_D, NOTE_D, NOTE_E, NOTE_F,
    NOTE_F, NOTE_G, NOTE_G, NOTE_A, NOTE_A, NOTE_B
};

NoteName midi_to_note_name(uint8_t midi) {
    if (midi == 0) return NOTE_REST;
    return semitone_to_note[midi % 12];
}

uint8_t midi_to_octave(uint8_t midi) {
    if (midi == 0) return 0;
    return (midi / 12) - 1;
}

int midi_is_rest(uint8_t midi) {
    return midi == 0;
}

// Convert MIDI ticks to milliseconds: ms = ticks * 60000 / (bpm * PPQ)
uint16_t ticks_to_ms(uint8_t ticks, uint16_t bpm) {
    if (bpm == 0) bpm = 120;  // Default BPM
    return (uint16_t)((uint32_t)ticks * 60000 / ((uint32_t)bpm * ABC_PPQ));
}

// Get total duration in milliseconds for a pool
uint32_t pool_total_ms(const NotePool *pool, uint16_t bpm) {
    if (!pool || bpm == 0) return 0;
    return (uint32_t)pool->total_ticks * 60000 / ((uint32_t)bpm * ABC_PPQ);
}

const char *note_name_to_string(NoteName name) {
    static const char *names[] = {"C", "D", "E", "F", "G", "A", "B", "z"};
    return (name <= NOTE_REST) ? names[name] : "?";
}

const char *accidental_to_string(int8_t acc) {
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
    pool->head_index = -1;
    pool->tail_index = -1;
    pool->total_ticks = 0;
    pool->voice_id[0] = '\0';
}

void note_pool_reset(NotePool *pool) {
    if (!pool) return;
    pool->count = 0;
    pool->head_index = -1;
    pool->tail_index = -1;
    pool->total_ticks = 0;
    pool->voice_id[0] = '\0';
}

int note_pool_available(const NotePool *pool) {
    return pool ? (pool->capacity - pool->count) : 0;
}

static int16_t note_pool_alloc(NotePool *pool) {
    if (!pool || pool->count >= pool->capacity) return -1;
    int16_t index = (int16_t)pool->count;
    pool->count++;
    struct note *n = &pool->notes[index];
    n->next_index = -1;
    n->duration = 0;
    n->chord_size = 0;
    for (int i = 0; i < ABC_MAX_CHORD_NOTES; i++) {
        n->midi_note[i] = 0;
    }
    return index;
}

// ============================================================================
// Sheet functions
// ============================================================================

void sheet_init(struct sheet *sheet, NotePool *pools, uint8_t pool_count) {
    if (!sheet) return;
    sheet->pools = pools;
    sheet->pool_count = pool_count;
    sheet->voice_count = 0;
    sheet->tempo_bpm = 120;
    sheet->tempo_note_num = 1;
    sheet->tempo_note_den = 4;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;

    // Initialize all pools
    for (uint8_t i = 0; i < pool_count; i++) {
        note_pool_init(&pools[i]);
    }
}

void sheet_reset(struct sheet *sheet) {
    if (!sheet) return;
    for (uint8_t i = 0; i < sheet->pool_count; i++) {
        note_pool_reset(&sheet->pools[i]);
    }
    sheet->voice_count = 0;
    sheet->tempo_bpm = 120;
    sheet->tempo_note_num = 1;
    sheet->tempo_note_den = 4;
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';
}

struct note *note_get(const NotePool *pool, int index) {
    if (!pool) return NULL;
    if (index < 0 || index >= (int)pool->count) return NULL;
    return (struct note *)&pool->notes[index];
}

struct note *pool_first_note(const NotePool *pool) {
    return pool ? note_get(pool, pool->head_index) : NULL;
}

struct note *note_next(const NotePool *pool, const struct note *current) {
    return (pool && current) ? note_get(pool, current->next_index) : NULL;
}

// Legacy compatibility - uses first pool
struct note *sheet_first_note(const struct sheet *sheet) {
    if (!sheet || !sheet->pools || sheet->pool_count == 0) return NULL;
    return pool_first_note(&sheet->pools[0]);
}

// Append a note/chord to a specific pool (stores only MIDI notes)
static int pool_append_note(NotePool *pool, uint8_t chord_size,
                            NoteName *names, int *octaves,
                            int8_t *accs, uint8_t duration_ticks) {
    if (!pool) return -1;

    int16_t index = note_pool_alloc(pool);
    if (index < 0) return -1;

    struct note *n = &pool->notes[index];
    n->chord_size = chord_size;
    n->duration = duration_ticks;
    n->next_index = -1;

    for (uint8_t i = 0; i < chord_size && i < ABC_MAX_CHORD_NOTES; i++) {
        // Only store MIDI note - other properties derived on demand
        n->midi_note[i] = (uint8_t)note_to_midi(names[i], octaves[i], accs[i]);
    }

    if (pool->head_index < 0) {
        pool->head_index = index;
        pool->tail_index = index;
    } else {
        pool->notes[pool->tail_index].next_index = index;
        pool->tail_index = index;
    }

    pool->total_ticks += duration_ticks;
    return 0;
}

// ============================================================================
// Parser helper functions
// ============================================================================

static inline char peek(ParserState *s) {
    return (s->pos < s->len) ? s->input[s->pos] : '\0';
}

static inline char advance(ParserState *s) {
    return (s->pos < s->len) ? s->input[s->pos++] : '\0';
}

static void skip_whitespace(ParserState *s) {
    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') s->pos++;
        else break;
    }
}

// Calculate duration in MIDI ticks (PPQ-based)
// Quarter note = ABC_PPQ ticks, so whole note = 4 * ABC_PPQ ticks
static uint8_t calculate_duration_ticks(ParserState *s, int num, int den) {
    // Whole note = 4 * PPQ ticks (PPQ = ticks per quarter note)
    // Ticks are tempo-independent; tempo_note only affects ticks_to_ms conversion
    uint32_t whole_ticks = 4 * ABC_PPQ;
    // Default note duration in ticks
    uint32_t default_ticks = whole_ticks * s->default_num / s->default_den;
    // Apply note multiplier/divisor
    uint32_t duration = default_ticks * num / den;

    if (s->tuplet_remaining > 0) {
        duration = duration * s->tuplet_in_time / s->tuplet_num;
        s->tuplet_remaining--;
    }

    // Clamp to uint8_t max (255)
    if (duration > 255) duration = 255;
    return (uint8_t)duration;
}

static void set_key_signature(ParserState *s, const char *key) {
    for (int i = 0; key_signatures[i].name; i++) {
        if (strcmp(key_signatures[i].name, key) == 0) {
            memcpy(s->key_accidentals, key_signatures[i].accidentals, 7);
            return;
        }
    }
    memset(s->key_accidentals, 0, 7);
}

static void safe_strcpy(char *dest, uint8_t dest_size, const char *src, uint8_t src_len) {
    uint8_t len = (src_len < dest_size) ? src_len : (dest_size - 1);
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// Find or create a voice by ID, returns voice index
static int find_or_create_voice(struct sheet *sheet, const char *voice_id, uint8_t id_len) {
    // Search existing voices
    for (uint8_t i = 0; i < sheet->voice_count; i++) {
        if (strncmp(sheet->pools[i].voice_id, voice_id, id_len) == 0 &&
            sheet->pools[i].voice_id[id_len] == '\0') {
            return i;
        }
    }

    // Create new voice if space available
    if (sheet->voice_count < sheet->pool_count) {
        uint8_t idx = sheet->voice_count;
        safe_strcpy(sheet->pools[idx].voice_id, ABC_MAX_VOICE_ID_LEN, voice_id, id_len);
        sheet->voice_count++;
        return idx;
    }

    return -1; // No space for more voices
}

// ============================================================================
// Header parsing
// ============================================================================

static void parse_header(ParserState *s, struct sheet *sheet) {
    while (s->pos < s->len) {
        skip_whitespace(s);
        if (s->pos + 1 >= s->len || s->input[s->pos + 1] != ':') break;

        char field = s->input[s->pos];
        uint16_t start = s->pos + 2;
        uint16_t end = start;

        while (end < s->len && s->input[end] != '\n' && s->input[end] != '\r') end++;
        uint16_t line_end = end;
        if (end < s->len) end++;

        while (start < line_end && s->input[start] == ' ') start++;
        while (line_end > start && (s->input[line_end-1] == ' ' || s->input[line_end-1] == '\t')) line_end--;

        uint8_t vlen = (uint8_t)(line_end - start);
        const char *val = s->input + start;

        switch (field) {
            case 'X': break; // Reference number, ignore
            case 'T': safe_strcpy(sheet->title, ABC_MAX_TITLE_LEN, val, vlen); break;
            case 'C': safe_strcpy(sheet->composer, ABC_MAX_COMPOSER_LEN, val, vlen); break;
            case 'L': {
                int num = 0, den = 0;
                uint8_t i = 0;
                while (i < vlen && val[i] >= '0' && val[i] <= '9') num = num * 10 + (val[i++] - '0');
                if (i < vlen && val[i] == '/') {
                    i++;
                    while (i < vlen && val[i] >= '0' && val[i] <= '9') den = den * 10 + (val[i++] - '0');
                }
                if (num > 0 && den > 0) {
                    s->default_num = sheet->default_note_num = (uint8_t)num;
                    s->default_den = sheet->default_note_den = (uint8_t)den;
                }
                break;
            }
            case 'M': {
                int num = 0, den = 0;
                uint8_t i = 0;
                while (i < vlen && val[i] >= '0' && val[i] <= '9') num = num * 10 + (val[i++] - '0');
                if (i < vlen && val[i] == '/') {
                    i++;
                    while (i < vlen && val[i] >= '0' && val[i] <= '9') den = den * 10 + (val[i++] - '0');
                }
                if (num > 0 && den > 0) {
                    s->meter_num = sheet->meter_num = (uint8_t)num;
                    s->meter_den = sheet->meter_den = (uint8_t)den;
                }
                break;
            }
            case 'Q': {
                int tempo = 0, note_num = 0, note_den = 0;
                uint8_t i = 0;
                uint8_t eq_pos = 0;
                for (uint8_t j = 0; j < vlen; j++) {
                    if (val[j] == '=') { eq_pos = j + 1; break; }
                }
                if (eq_pos > 0) {
                    while (i < eq_pos - 1 && val[i] >= '0' && val[i] <= '9') {
                        note_num = note_num * 10 + (val[i++] - '0');
                    }
                    if (i < eq_pos - 1 && val[i] == '/') {
                        i++;
                        while (i < eq_pos - 1 && val[i] >= '0' && val[i] <= '9') {
                            note_den = note_den * 10 + (val[i++] - '0');
                        }
                    }
                    if (note_num > 0 && note_den > 0) {
                        s->tempo_note_num = sheet->tempo_note_num = (uint8_t)note_num;
                        s->tempo_note_den = sheet->tempo_note_den = (uint8_t)note_den;
                    }
                }
                i = eq_pos;
                while (i < vlen && val[i] >= '0' && val[i] <= '9') {
                    tempo = tempo * 10 + (val[i++] - '0');
                }
                if (tempo > 0) s->tempo_bpm = sheet->tempo_bpm = (uint16_t)tempo;
                break;
            }
            case 'K': {
                safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, val, vlen);
                set_key_signature(s, sheet->key);
                s->pos = end;
                return;
            }
            case 'V':
                // V: marks start of body - don't consume it, let body parser handle it
                return;
        }
        s->pos = end;
    }
}

// ============================================================================
// Single pitch parsing (used for both single notes and chord members)
// ============================================================================

typedef struct {
    NoteName name;
    int octave;
    int8_t accidental;
    int dur_num;
    int dur_den;
} ParsedPitch;

static int parse_pitch(ParserState *s, ParsedPitch *pitch) {
    char c = peek(s);
    int8_t acc = ACC_NONE;
    int explicit_acc = 0;

    while (c == '^' || c == '_' || c == '=') {
        explicit_acc = 1;
        if (c == '^') acc = (acc == ACC_SHARP) ? ACC_DOUBLE_SHARP : ACC_SHARP;
        else if (c == '_') acc = (acc == ACC_FLAT) ? ACC_DOUBLE_FLAT : ACC_FLAT;
        else acc = ACC_NATURAL;
        advance(s);
        c = peek(s);
    }

    NoteName name;
    int octave = 4;

    if (c >= 'A' && c <= 'G') { name = (NoteName)((c - 'A' + 5) % 7); octave = 4; advance(s); }
    else if (c >= 'a' && c <= 'g') { name = (NoteName)((c - 'a' + 5) % 7); octave = 5; advance(s); }
    else if (c == 'z' || c == 'Z') { name = NOTE_REST; advance(s); }
    else return -1;

    if (!explicit_acc && name != NOTE_REST) {
        acc = s->bar_accidentals[name] ? s->bar_accidentals[name] : s->key_accidentals[name];
    } else if (explicit_acc && name != NOTE_REST) {
        s->bar_accidentals[name] = (acc == ACC_NATURAL) ? ACC_NONE : acc;
        if (acc == ACC_NATURAL) acc = ACC_NONE;
    }

    c = peek(s);
    while (c == '\'' || c == ',') {
        if (c == '\'') octave++; else octave--;
        advance(s);
        c = peek(s);
    }
    if (octave < 0) octave = 0;
    if (octave > 6) octave = 6;

    // Parse duration modifiers
    int num = 1, den = 1;
    c = peek(s);
    if (c >= '0' && c <= '9') {
        num = 0;
        while (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); advance(s); c = peek(s); }
    }
    if (c == '/') {
        advance(s);
        c = peek(s);
        if (c >= '0' && c <= '9') {
            den = 0;
            while (c >= '0' && c <= '9') { den = den * 10 + (c - '0'); advance(s); c = peek(s); }
        } else {
            den = 2;
            while (peek(s) == '/') { advance(s); den *= 2; }
        }
    }

    pitch->name = name;
    pitch->octave = octave;
    pitch->accidental = acc;
    pitch->dur_num = num;
    pitch->dur_den = den;
    return 0;
}

// ============================================================================
// Note/Chord parsing
// ============================================================================

static int parse_note_or_chord(ParserState *s, struct sheet *sheet) {
    skip_whitespace(s);
    if (s->pos >= s->len) return 1;

    char c = peek(s);
    NotePool *pool = &sheet->pools[s->current_voice];

    // Handle chord [...]
    if (c == '[') {
        advance(s); // skip '['

        NoteName names[ABC_MAX_CHORD_NOTES];
        int octaves[ABC_MAX_CHORD_NOTES];
        int8_t accs[ABC_MAX_CHORD_NOTES];
        uint8_t chord_size = 0;
        int total_dur_num = 1, total_dur_den = 1;

        while (peek(s) != ']' && s->pos < s->len && chord_size < ABC_MAX_CHORD_NOTES) {
            skip_whitespace(s);
            c = peek(s);

            // Skip if not a note character
            if (!((c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g') ||
                  c == 'z' || c == 'Z' || c == '^' || c == '_' || c == '=')) {
                if (c == ']') break;
                advance(s);
                continue;
            }

            ParsedPitch pitch;
            if (parse_pitch(s, &pitch) == 0) {
                names[chord_size] = pitch.name;
                octaves[chord_size] = pitch.octave;
                accs[chord_size] = pitch.accidental;
                // Use last pitch's duration for the chord
                total_dur_num = pitch.dur_num;
                total_dur_den = pitch.dur_den;
                chord_size++;
            }
        }

        if (peek(s) == ']') advance(s); // skip ']'

        // Check for duration after chord
        c = peek(s);
        if (c >= '0' && c <= '9') {
            total_dur_num = 0;
            while (c >= '0' && c <= '9') { total_dur_num = total_dur_num * 10 + (c - '0'); advance(s); c = peek(s); }
        }
        if (c == '/') {
            advance(s);
            c = peek(s);
            if (c >= '0' && c <= '9') {
                total_dur_den = 0;
                while (c >= '0' && c <= '9') { total_dur_den = total_dur_den * 10 + (c - '0'); advance(s); c = peek(s); }
            } else {
                total_dur_den = 2;
                while (peek(s) == '/') { advance(s); total_dur_den *= 2; }
            }
        }

        if (chord_size > 0) {
            uint8_t duration = calculate_duration_ticks(s, total_dur_num, total_dur_den);
            return pool_append_note(pool, chord_size, names, octaves, accs, duration);
        }
        return 0;
    }

    // Handle single note
    ParsedPitch pitch;
    if (parse_pitch(s, &pitch) == 0) {
        NoteName names[1] = { pitch.name };
        int octaves[1] = { pitch.octave };
        int8_t accs[1] = { pitch.accidental };
        uint8_t duration = calculate_duration_ticks(s, pitch.dur_num, pitch.dur_den);
        return pool_append_note(pool, 1, names, octaves, accs, duration);
    }

    return 1; // Not a note
}

static int copy_repeat_section(NotePool *pool, int16_t start_idx, int16_t end_idx) {
    if (!pool || start_idx < 0) return 0;

    int16_t cur = start_idx;
    while (cur >= 0 && cur <= end_idx && cur < (int16_t)pool->count) {
        struct note *src = &pool->notes[cur];

        // Extract note properties from stored MIDI values
        NoteName names[ABC_MAX_CHORD_NOTES];
        int octaves[ABC_MAX_CHORD_NOTES];
        int8_t accs[ABC_MAX_CHORD_NOTES];

        for (uint8_t i = 0; i < src->chord_size && i < ABC_MAX_CHORD_NOTES; i++) {
            names[i] = midi_to_note_name(src->midi_note[i]);
            octaves[i] = midi_to_octave(src->midi_note[i]);
            accs[i] = ACC_NONE;  // Accidentals not preserved in repeat copies
        }

        if (pool_append_note(pool, src->chord_size, names, octaves, accs, src->duration) < 0) {
            return -1;
        }
        cur = src->next_index;
        if (cur < 0) break;
    }
    return 0;
}

static int parse_notes(ParserState *s, struct sheet *sheet) {
    s->repeat_start_index = -1;
    s->repeat_end_index = -1;
    s->in_repeat = 0;
    s->current_voice = 0;

    // Don't create default voice yet - wait to see if V: line comes first

    while (s->pos < s->len) {
        skip_whitespace(s);
        if (s->pos >= s->len) break;

        char c = peek(s);

        // Handle V: voice change (inline) BEFORE getting pool reference
        if (c == 'V' && s->pos + 1 < s->len && s->input[s->pos + 1] == ':') {
            advance(s); // V
            advance(s); // :

            // Skip leading whitespace
            while (s->pos < s->len && (s->input[s->pos] == ' ' || s->input[s->pos] == '\t')) {
                s->pos++;
            }

            // Read voice ID (alphanumeric characters only)
            uint16_t id_start = s->pos;
            while (s->pos < s->len) {
                char ch = s->input[s->pos];
                // Voice ID is alphanumeric, stop at whitespace or any other character
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
                    s->pos++;
                } else {
                    break;
                }
            }
            uint8_t id_len = (uint8_t)(s->pos - id_start);

            if (id_len > 0) {
                int voice_idx = find_or_create_voice(sheet, s->input + id_start, id_len);
                if (voice_idx >= 0) {
                    s->current_voice = (uint8_t)voice_idx;
                }
            }
            continue;
        }

        // Create default voice if none exists and we're about to parse notes
        if (sheet->voice_count == 0 && sheet->pool_count > 0) {
            sheet->voice_count = 1;
            safe_strcpy(sheet->pools[0].voice_id, ABC_MAX_VOICE_ID_LEN, "default", 7);
        }

        NotePool *pool = &sheet->pools[s->current_voice];

        if (c == '|') {
            advance(s);
            memset(s->bar_accidentals, 0, 7);
            c = peek(s);
            if (c == ':') {
                advance(s);
                s->in_repeat = 1;
                s->repeat_start_index = (int16_t)pool->count;
            } else if (c == '|' || c == ']') {
                advance(s);
            }
            continue;
        }

        if (c == ':') {
            advance(s);
            if (peek(s) == '|') {
                advance(s);
                s->repeat_end_index = (int16_t)(pool->count - 1);
                if (peek(s) == ':') {
                    advance(s);
                    if (copy_repeat_section(pool, s->repeat_start_index, s->repeat_end_index) < 0) return -2;
                    s->repeat_start_index = (int16_t)pool->count;
                } else {
                    if (copy_repeat_section(pool, s->repeat_start_index, s->repeat_end_index) < 0) return -2;
                    s->in_repeat = 0;
                    s->repeat_start_index = -1;
                }
            }
            continue;
        }

        // Handle tuplet markers
        if (c == '(') {
            advance(s);
            c = peek(s);
            if (c >= '2' && c <= '9') {
                uint8_t n = (uint8_t)(c - '0');
                advance(s);
                s->tuplet_num = n;
                s->tuplet_remaining = n;
                if (n == 2) s->tuplet_in_time = 3;
                else if (n == 3) s->tuplet_in_time = 2;
                else if (n == 4) s->tuplet_in_time = 3;
                else if (n == 6) s->tuplet_in_time = 2;
                else s->tuplet_in_time = n - 1;
            }
            continue;
        }

        // Skip decorations (staccato dots, ties, slurs, etc.)
        if (c == ')' || c == '{' || c == '}' || c == '!' || c == '+' ||
            c == '-' || c == '<' || c == '>' || c == '~' || c == '%' || c == '.') {
            advance(s);
            continue;
        }

        if (c == '"') {
            advance(s);
            while (s->pos < s->len && peek(s) != '"') advance(s);
            if (peek(s) == '"') advance(s);
            continue;
        }

        int result = parse_note_or_chord(s, sheet);
        if (result < 0) return -2;
        if (result > 0) advance(s);
    }
    return 0;
}

// ============================================================================
// Main parse function
// ============================================================================

int abc_parse(struct sheet *sheet, const char *abc_string) {
    if (!sheet || !abc_string || !sheet->pools || sheet->pool_count == 0) return -1;

    uint16_t len = 0;
    while (abc_string[len] && len < 0xFFFF) len++;

    ParserState s = {
        .input = abc_string,
        .pos = 0,
        .len = len,
        .default_num = sheet->default_note_num,
        .default_den = sheet->default_note_den,
        .tempo_bpm = sheet->tempo_bpm,
        .tempo_note_num = sheet->tempo_note_num,
        .tempo_note_den = sheet->tempo_note_den,
        .meter_num = sheet->meter_num,
        .meter_den = sheet->meter_den,
        .repeat_start_index = -1,
        .repeat_end_index = -1,
        .in_repeat = 0,
        .tuplet_remaining = 0,
        .tuplet_num = 0,
        .tuplet_in_time = 0,
        .current_voice = 0
    };
    memset(s.key_accidentals, 0, 7);
    memset(s.bar_accidentals, 0, 7);

    parse_header(&s, sheet);
    return parse_notes(&s, sheet);
}

// ============================================================================
// Debug printing
// ============================================================================

void sheet_print(const struct sheet *sheet) {
    if (!sheet) { printf("(null sheet)\n"); return; }

    printf("=== Sheet Music ===\n");
    if (sheet->title[0]) printf("Title: %s\n", sheet->title);
    if (sheet->composer[0]) printf("Composer: %s\n", sheet->composer);
    if (sheet->key[0]) printf("Key: %s\n", sheet->key);
    printf("Tempo: %u BPM (PPQ=%u)\n", sheet->tempo_bpm, ABC_PPQ);
    printf("Meter: %u/%u\n", sheet->meter_num, sheet->meter_den);
    printf("Default note: %u/%u\n", sheet->default_note_num, sheet->default_note_den);
    printf("Voices: %u\n", sheet->voice_count);

    for (uint8_t v = 0; v < sheet->voice_count; v++) {
        NotePool *pool = &sheet->pools[v];
        uint32_t total_ms = pool_total_ms(pool, sheet->tempo_bpm);
        printf("\n--- Voice %u: %s ---\n", v + 1, pool->voice_id[0] ? pool->voice_id : "(unnamed)");
        printf("Notes: %u, Duration: %lu ticks (%lu ms, %.2f s)\n",
               pool->count, (unsigned long)pool->total_ticks, (unsigned long)total_ms, total_ms / 1000.0f);

        printf("%-4s %-12s %-10s %-8s %-5s\n", "#", "Notes", "Freq", "Ticks", "MIDI");
        printf("--------------------------------------------------\n");

        int i = 1;
        struct note *n = pool_first_note(pool);
        while (n) {
            if (n->chord_size == 1 && midi_is_rest(n->midi_note[0])) {
                printf("%-4d %-12s %-10s %-8u %-5s\n", i, "rest", "-", n->duration, "-");
            } else {
                char notes_str[32] = "";
                char freq_str[16] = "";
                char midi_str[16] = "";

                for (uint8_t j = 0; j < n->chord_size && j < ABC_MAX_CHORD_NOTES; j++) {
                    char note_buf[8];
                    snprintf(note_buf, sizeof(note_buf), "%s%u",
                             note_name_to_string(midi_to_note_name(n->midi_note[j])),
                             midi_to_octave(n->midi_note[j]));
                    if (j > 0) strcat(notes_str, "+");
                    strcat(notes_str, note_buf);
                }

                snprintf(freq_str, sizeof(freq_str), "%.1f", midi_to_frequency_x10(n->midi_note[0]) / 10.0f);
                snprintf(midi_str, sizeof(midi_str), "%u", n->midi_note[0]);

                printf("%-4d %-12s %-10s %-8u %-5s\n", i, notes_str, freq_str, n->duration, midi_str);
            }
            n = note_next(pool, n);
            i++;
        }
    }
    printf("==================================================\n");
}
