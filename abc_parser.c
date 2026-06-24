#include "abc_parser.h"
#include <string.h>
#ifndef ABC_NO_PRINT
#include <stdio.h>   // only sheet_print() needs stdio
#endif

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
//
// Key signatures are computed from the circle of fifths rather than tabulated,
// so every tonic, accidental and church mode is supported (major/Ionian, minor/
// Aeolian, Dorian, Mixolydian, Phrygian, Lydian, Locrian).
//
//   fifths = sharps in the *major* key of the tonic, then offset by the mode.
//   A positive count adds sharps in the order F C G D A E B; a negative count
//   adds flats in the order B E A D G C F.

// Sharps in the major key of each natural tonic (C D E F G A B order).
static const int8_t major_fifths[7] = { 0, 2, 4, -1, 1, 3, 5 };

// Note indices (NoteName order) for applying accidentals along the circle.
static const uint8_t sharp_order[7] = { 3, 0, 4, 1, 5, 2, 6 }; // F C G D A E B
static const uint8_t flat_order[7]  = { 6, 2, 5, 1, 4, 0, 3 }; // B E A D G C F

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
    int16_t ending_start_index;  // pool index where the 1st numbered ending began (-1 = none)
    uint8_t in_repeat;
    uint8_t tuplet_remaining;
    uint8_t tuplet_num;
    uint8_t tuplet_in_time;
    uint8_t current_voice;       // Current voice index
    int8_t  broken_rhythm;       // pending broken rhythm: +n for n '>', -n for n '<' (0 = none)
    uint8_t tie_pending;         // a '-' tie awaits the next note
    uint8_t grace_count;         // grace notes buffered from a {..} group
    NoteName grace_names[ABC_GRACE_MAX];
    int      grace_octaves[ABC_GRACE_MAX];
    int8_t   grace_accs[ABC_GRACE_MAX];
    uint8_t has_explicit_L;      // L: field was explicitly set
    uint8_t has_explicit_K;      // K: field was explicitly set
    uint8_t has_explicit_Q_note; // Q: field had explicit note value (e.g. Q:1/4=120 vs Q:120)
} ParserState;

// ============================================================================
// Utility functions
// ============================================================================

#ifndef ABC_NO_FLOAT
float note_to_frequency(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0.0f;
    int midi = note_to_midi(name, octave, acc);
    return midi_frequencies_x10[midi] / 10.0f;
}
#endif

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

void note_pool_init(NotePool *pool, struct note *buffer, uint16_t capacity, uint8_t max_chord_notes) {
    if (!pool) return;
    pool->notes = buffer;
    pool->count = 0;
    pool->capacity = capacity;
    pool->max_chord_notes = max_chord_notes > 0 ? max_chord_notes : ABC_MAX_CHORD_NOTES;
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
    if (index < 0) return -1;  // count exceeds int16_t index space
    pool->count++;
    // Count-only pool (notes == NULL): used by abc_count_notes(). Tally only.
    if (!pool->notes) return index;
    struct note *n = &pool->notes[index];
    n->next_index = -1;
    n->duration = 0;
    n->chord_size = 0;
    // Clear midi_note array (use compile-time size since struct is fixed)
    for (int i = 0; i < ABC_MAX_CHORD_NOTES; i++) {
        n->midi_note[i] = 0;
    }
    return index;
}

// ============================================================================
// Sheet functions
// ============================================================================

// Forward declaration (defined with the parser helpers below).
static void safe_strcpy(char *dest, uint8_t dest_size, const char *src, uint8_t src_len);

// Score defaults applied when the corresponding header is absent are taken from
// the ABC_DEFAULT_* macros (see abc_parser.h): Q:ABC_DEFAULT_TEMPO_BPM,
// M:ABC_DEFAULT_METER_NUM/DEN, K:ABC_DEFAULT_KEY, and L: either derived from the
// meter or fixed to ABC_DEFAULT_NOTE_NUM/DEN (per ABC_DERIVE_LENGTH_FROM_METER).
void sheet_init(struct sheet *sheet, NotePool *pools, uint8_t pool_count) {
    if (!sheet) return;
    sheet->pools = pools;
    sheet->pool_count = pool_count;
    sheet->voice_count = 0;
    sheet->tempo_bpm = ABC_DEFAULT_TEMPO_BPM;
    sheet->tempo_note_num = ABC_DEFAULT_NOTE_NUM;
    sheet->tempo_note_den = ABC_DEFAULT_NOTE_DEN;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, ABC_DEFAULT_KEY, (uint8_t)sizeof(ABC_DEFAULT_KEY) - 1);
    sheet->default_note_num = ABC_DEFAULT_NOTE_NUM;
    sheet->default_note_den = ABC_DEFAULT_NOTE_DEN;
    sheet->meter_num = ABC_DEFAULT_METER_NUM;
    sheet->meter_den = ABC_DEFAULT_METER_DEN;
    // Note: pools should already be initialized by caller via note_pool_init_ext()
}

void sheet_reset(struct sheet *sheet) {
    if (!sheet) return;
    for (uint8_t i = 0; i < sheet->pool_count; i++) {
        note_pool_reset(&sheet->pools[i]);
    }
    sheet->voice_count = 0;
    sheet->tempo_bpm = ABC_DEFAULT_TEMPO_BPM;
    sheet->tempo_note_num = ABC_DEFAULT_NOTE_NUM;
    sheet->tempo_note_den = ABC_DEFAULT_NOTE_DEN;
    sheet->default_note_num = ABC_DEFAULT_NOTE_NUM;
    sheet->default_note_den = ABC_DEFAULT_NOTE_DEN;
    sheet->meter_num = ABC_DEFAULT_METER_NUM;
    sheet->meter_den = ABC_DEFAULT_METER_DEN;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, ABC_DEFAULT_KEY, (uint8_t)sizeof(ABC_DEFAULT_KEY) - 1);
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
    if (!pool->notes) return 0;  // count-only pool: allocation tallied, nothing to store

    struct note *n = &pool->notes[index];
    // Clamp chord size to pool's max (and struct's compile-time max)
    uint8_t max_chord = pool->max_chord_notes;
    if (max_chord > ABC_MAX_CHORD_NOTES) max_chord = ABC_MAX_CHORD_NOTES;
    if (chord_size > max_chord) chord_size = max_chord;

    n->chord_size = chord_size;
    n->duration = duration_ticks;
    n->next_index = -1;

    for (uint8_t i = 0; i < chord_size; i++) {
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

static inline char abc_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Parse a K: value (tonic [#/b] [mode]) and fill key_accidentals from the
// circle of fifths. Unknown/none keys yield no accidentals.
static void set_key_signature(ParserState *s, const char *key) {
    memset(s->key_accidentals, 0, 7);
    if (!key || !key[0]) return;

    const char *p = key;
    while (*p == ' ' || *p == '\t') p++;

    int tonic = -1;
    if (*p >= 'A' && *p <= 'G') tonic = (*p - 'A' + 5) % 7;       // -> NoteName index
    else if (*p >= 'a' && *p <= 'g') tonic = (*p - 'a' + 5) % 7;
    if (tonic < 0) return;  // K:none, K:HP, etc.
    p++;

    int fifths = major_fifths[tonic];
    if (*p == '#') { fifths += 7; p++; }
    else if (*p == 'b') { fifths -= 7; p++; }

    while (*p == ' ') p++;

    // Mode suffix (case-insensitive, first 3 letters; bare 'm' = minor).
    char m0 = p[0] ? abc_lower(p[0]) : 0;
    char m1 = (m0 && p[1]) ? abc_lower(p[1]) : 0;
    char m2 = (m1 && p[2]) ? abc_lower(p[2]) : 0;
    if      (m0=='m' && m1=='a' && m2=='j') fifths += 0;   // major
    else if (m0=='i' && m1=='o' && m2=='n') fifths += 0;   // Ionian
    else if (m0=='m' && m1=='i' && m2=='x') fifths += -1;  // Mixolydian
    else if (m0=='m' && m1=='i' && m2=='n') fifths += -3;  // minor
    else if (m0=='a' && m1=='e' && m2=='o') fifths += -3;  // Aeolian
    else if (m0=='d' && m1=='o' && m2=='r') fifths += -2;  // Dorian
    else if (m0=='p' && m1=='h' && m2=='r') fifths += -4;  // Phrygian
    else if (m0=='l' && m1=='y' && m2=='d') fifths += +1;  // Lydian
    else if (m0=='l' && m1=='o' && m2=='c') fifths += -5;  // Locrian
    else if (m0=='m')                       fifths += -3;  // bare 'm' = minor

    if (fifths > 7) fifths = 7;
    if (fifths < -7) fifths = -7;
    if (fifths > 0) for (int i = 0; i < fifths; i++) s->key_accidentals[sharp_order[i]] = ACC_SHARP;
    else for (int i = 0; i < -fifths; i++) s->key_accidentals[flat_order[i]] = ACC_FLAT;
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

// Forward declaration (defined with the note-parsing helpers below).
static int copy_repeat_section(NotePool *pool, int16_t start_idx, int16_t end_idx);

// Apply one information field (used by both the header parser and inline
// [X:...] fields). Handles T, C, L, M, Q, K; ignores X and unknown fields.
// Voice (V) is handled by the callers since it affects parse control flow.
static void apply_field_value(ParserState *s, struct sheet *sheet, char field,
                              const char *val, uint8_t vlen) {
    switch (field) {
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
                s->has_explicit_L = 1;
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
                    s->has_explicit_Q_note = 1;
                }
            }
            i = eq_pos;
            while (i < vlen && val[i] >= '0' && val[i] <= '9') {
                tempo = tempo * 10 + (val[i++] - '0');
            }
            if (tempo > 0) s->tempo_bpm = sheet->tempo_bpm = (uint16_t)tempo;
            break;
        }
        case 'K':
            safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, val, vlen);
            set_key_signature(s, sheet->key);
            s->has_explicit_K = 1;
            break;
        default: break;  // X and unknown fields ignored
    }
}

// Replay the common part of a repeat on entering a numbered ending.
//   '1' : mark where the 1st ending begins (the common part precedes it).
//   '2'+: a later ending begins; replay [repeat_start, ending_start) once so the
//         unfolded stream reads common, 1st-ending, common, 2nd-ending.
static int handle_ending(ParserState *s, NotePool *pool, char digit) {
    if (digit == '1') {
        s->ending_start_index = (int16_t)pool->count;
        return 0;
    }
    if (s->in_repeat && s->ending_start_index >= 0 && s->repeat_start_index >= 0) {
        if (copy_repeat_section(pool, s->repeat_start_index,
                                (int16_t)(s->ending_start_index - 1)) < 0) return -1;
    }
    s->ending_start_index = -1;
    s->in_repeat = 0;
    s->repeat_start_index = -1;
    return 0;
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

        if (field == 'V') {
            // V: marks start of body - don't consume it, let body parser handle it
            return;
        }
        apply_field_value(s, sheet, field, val, vlen);
        s->pos = end;
        if (field == 'K') return;  // K: ends the header
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
// Ornament / rhythm modifiers
// ============================================================================

// Apply a pending broken-rhythm modifier (s->broken_rhythm) to the previous
// (tail) note and return the adjusted duration for the current note.
//   '>' : previous note dotted (x3/2), current note cut (x1/2)
//   '<' : previous note cut,           current note dotted
// n consecutive signs scale the dotted side by (2^(n+1)-1)/2^n, the cut side
// by 1/2^n (so '>>' is x7/4 and x1/4). Fixes up pool->total_ticks.
static uint8_t apply_broken_rhythm(ParserState *s, NotePool *pool, uint8_t cur_dur) {
    int8_t b = s->broken_rhythm;
    s->broken_rhythm = 0;
    if (b == 0) return cur_dur;

    uint8_t n = (b > 0) ? (uint8_t)b : (uint8_t)(-b);
    if (n > 4) n = 4;
    uint16_t den = (uint16_t)(1u << n);
    uint16_t dot = (uint16_t)((1u << (n + 1)) - 1);
    uint16_t prev_num = (b > 0) ? dot : 1;
    uint16_t cur_num  = (b > 0) ? 1   : dot;

    if (pool && pool->notes && pool->tail_index >= 0) {
        struct note *prev = &pool->notes[pool->tail_index];
        uint32_t nd = (uint32_t)prev->duration * prev_num / den;
        if (nd > 255) nd = 255;
        if (nd < 1) nd = 1;
        pool->total_ticks += (uint8_t)nd - prev->duration;
        prev->duration = (uint8_t)nd;
    }
    uint32_t cd = (uint32_t)cur_dur * cur_num / den;
    if (cd > 255) cd = 255;
    if (cd < 1) cd = 1;
    return (uint8_t)cd;
}

// Emit any grace notes buffered from a {..} group ahead of a principal note of
// length principal_dur. Each grace note steals ABC_GRACE_NOTE_TICKS from the
// principal (so the bar length is unchanged); returns the total ticks stolen.
static uint8_t emit_grace_notes(ParserState *s, NotePool *pool, uint8_t principal_dur) {
    uint8_t cnt = s->grace_count;
    s->grace_count = 0;
    if (cnt == 0 || !pool) return 0;

    uint16_t per = ABC_GRACE_NOTE_TICKS;
    if (per < 1) per = 1;
    uint16_t total = (uint16_t)(per * cnt);
    if (total >= principal_dur) {            // keep >= 1 tick for the principal
        if (principal_dur <= cnt) return 0;  // too short to ornament; drop grace
        per = (uint16_t)((principal_dur - 1) / cnt);
        if (per == 0) return 0;
        total = (uint16_t)(per * cnt);
    }
    for (uint8_t i = 0; i < cnt; i++) {
        NoteName nm[1] = { s->grace_names[i] };
        int oc[1] = { s->grace_octaves[i] };
        int8_t ac[1] = { s->grace_accs[i] };
        if (pool_append_note(pool, 1, nm, oc, ac, (uint8_t)per) < 0) break;
    }
    return (uint8_t)total;
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
            s->tie_pending = 0;  // chord ties not modelled
            duration = apply_broken_rhythm(s, pool, duration);
            duration -= emit_grace_notes(s, pool, duration);
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

        // Tie: fold this note into the previous one if it is the same pitch.
        if (s->tie_pending) {
            s->tie_pending = 0;
            if (pitch.name != NOTE_REST && pool->notes && pool->tail_index >= 0) {
                struct note *prev = &pool->notes[pool->tail_index];
                int midi = note_to_midi(pitch.name, pitch.octave, pitch.accidental);
                if (prev->chord_size == 1 && prev->midi_note[0] == (uint8_t)midi) {
                    uint32_t nd = (uint32_t)prev->duration + duration;
                    if (nd > 255) nd = 255;
                    pool->total_ticks += (uint8_t)nd - prev->duration;
                    prev->duration = (uint8_t)nd;
                    return 0;  // merged into previous note
                }
            }
        }

        duration = apply_broken_rhythm(s, pool, duration);
        duration -= emit_grace_notes(s, pool, duration);
        return pool_append_note(pool, 1, names, octaves, accs, duration);
    }

    return 1; // Not a note
}

static int copy_repeat_section(NotePool *pool, int16_t start_idx, int16_t end_idx) {
    if (!pool || start_idx < 0) return 0;

    // Count-only pool: notes are contiguous and unlinked, so the repeated span
    // is simply [start_idx, end_idx]. Tally one allocation per source note.
    if (!pool->notes) {
        for (int16_t i = start_idx; i <= end_idx && i < (int16_t)pool->count; i++) {
            if (note_pool_alloc(pool) < 0) return -1;
        }
        return 0;
    }

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
    s->ending_start_index = -1;
    s->in_repeat = 0;
    s->current_voice = 0;
    s->broken_rhythm = 0;
    s->tie_pending = 0;
    s->grace_count = 0;

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
            if (c == ':') {                       // |:  start repeat
                advance(s);
                s->in_repeat = 1;
                s->repeat_start_index = (int16_t)pool->count;
                s->ending_start_index = -1;
            } else if (c >= '1' && c <= '9') {    // |1 / |2  numbered ending
                advance(s);
                if (handle_ending(s, pool, c) < 0) return -2;
            } else if (c == '|' || c == ']') {    // || or |]
                advance(s);
            }
            continue;
        }

        if (c == ':') {
            advance(s);
            if (peek(s) == '|') {                 // :|  end repeat
                advance(s);
                // Copy only the common part if a 1st ending was open.
                int16_t copy_end = (s->ending_start_index >= 0)
                    ? (int16_t)(s->ending_start_index - 1)
                    : (int16_t)(pool->count - 1);
                s->repeat_end_index = copy_end;
                if (copy_repeat_section(pool, s->repeat_start_index, copy_end) < 0) return -2;
                if (peek(s) == ':') {             // :|:  start another repeat
                    advance(s);
                    s->repeat_start_index = (int16_t)pool->count;
                    s->ending_start_index = -1;
                } else {                          // :|2 / :|[2  -> consume ending marker
                    if (peek(s) == '[') {
                        char d = (s->pos + 1 < s->len) ? s->input[s->pos + 1] : 0;
                        if (d >= '1' && d <= '9') { advance(s); advance(s); }
                    } else if (peek(s) >= '1' && peek(s) <= '9') {
                        advance(s);
                    }
                    s->in_repeat = 0;
                    s->repeat_start_index = -1;
                    s->ending_start_index = -1;
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

        // Broken rhythm: '>' dots the previous note and cuts the next, '<' vice
        // versa. Applied when the next note is appended (see apply_broken_rhythm).
        if (c == '>' || c == '<') {
            char bc = c;
            int8_t cnt = 0;
            while (peek(s) == bc) { advance(s); if (cnt < 8) cnt++; }
            s->broken_rhythm = (bc == '>') ? cnt : (int8_t)(-cnt);
            continue;
        }

        // Tie: fold the next same-pitch note into the previous one.
        if (c == '-') {
            advance(s);
            s->tie_pending = 1;
            continue;
        }

        // Grace notes {..}: buffer their pitches; they steal a little time from
        // the following principal note (see emit_grace_notes).
        if (c == '{') {
            advance(s);                       // '{'
            if (peek(s) == '/') advance(s);   // acciaccatura slash
            s->grace_count = 0;
            while (s->pos < s->len && peek(s) != '}') {
                char gc = peek(s);
                if ((gc >= 'A' && gc <= 'G') || (gc >= 'a' && gc <= 'g') ||
                    gc == 'z' || gc == 'Z' || gc == '^' || gc == '_' || gc == '=') {
                    ParsedPitch gp;
                    if (parse_pitch(s, &gp) == 0) {
                        if (gp.name != NOTE_REST && s->grace_count < ABC_GRACE_MAX) {
                            s->grace_names[s->grace_count] = gp.name;
                            s->grace_octaves[s->grace_count] = gp.octave;
                            s->grace_accs[s->grace_count] = gp.accidental;
                            s->grace_count++;
                        }
                    } else {
                        advance(s);
                    }
                } else {
                    advance(s);
                }
            }
            if (peek(s) == '}') advance(s);
            continue;
        }

        // Skip remaining decorations (staccato dot, slurs, ornaments, etc.)
        if (c == ')' || c == '!' || c == '+' || c == '~' || c == '%' || c == '.') {
            advance(s);
            continue;
        }

        if (c == '"') {
            advance(s);
            while (s->pos < s->len && peek(s) != '"') advance(s);
            if (peek(s) == '"') advance(s);
            continue;
        }

        // '[' may begin a numbered ending ([1, [2), an inline field ([K:G]),
        // or a chord. Endings and fields are handled here; chords fall through
        // to parse_note_or_chord below.
        if (c == '[') {
            char n1 = (s->pos + 1 < s->len) ? s->input[s->pos + 1] : 0;
            char n2 = (s->pos + 2 < s->len) ? s->input[s->pos + 2] : 0;
            if (n1 >= '1' && n1 <= '9') {                 // [1 / [2  numbered ending
                advance(s);                                // '['
                advance(s);                                // digit
                if (handle_ending(s, pool, n1) < 0) return -2;
                continue;
            }
            if (((n1 >= 'A' && n1 <= 'Z') || (n1 >= 'a' && n1 <= 'z')) && n2 == ':') {
                advance(s);                                // '['
                char field = advance(s);                   // field letter
                advance(s);                                // ':'
                uint16_t vstart = s->pos;
                while (s->pos < s->len && peek(s) != ']' &&
                       peek(s) != '\n' && peek(s) != '\r') advance(s);
                uint16_t vend = s->pos;
                if (peek(s) == ']') advance(s);
                while (vend > vstart && (s->input[vend-1] == ' ' || s->input[vend-1] == '\t')) vend--;
                uint8_t vlen = (uint8_t)(vend - vstart);
                if (field == 'V') {
                    int vi = find_or_create_voice(sheet, s->input + vstart, vlen);
                    if (vi >= 0) s->current_voice = (uint8_t)vi;
                } else {
                    apply_field_value(s, sheet, field, s->input + vstart, vlen);
                }
                continue;
            }
            // else: a chord — handled by parse_note_or_chord below.
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
    s.has_explicit_L = 0;
    s.has_explicit_K = 0;
    s.has_explicit_Q_note = 0;

    parse_header(&s, sheet);

    // When K: is absent, apply the default key signature (ABC_DEFAULT_KEY).
    if (!s.has_explicit_K) {
        safe_strcpy(sheet->key, ABC_MAX_KEY_LEN, ABC_DEFAULT_KEY, (uint8_t)sizeof(ABC_DEFAULT_KEY) - 1);
        set_key_signature(&s, sheet->key);
    }

#if ABC_DERIVE_LENGTH_FROM_METER
    // When L: is absent, derive the default note length from the meter (ABC
    // standard): meter < 3/4 -> 1/16, otherwise 1/8. (meter defaults to 4/4.)
    if (!s.has_explicit_L) {
        uint8_t den = ((uint16_t)s.meter_num * 4 < (uint16_t)s.meter_den * 3) ? 16 : 8;
        s.default_num = sheet->default_note_num = 1;
        s.default_den = sheet->default_note_den = den;
    }
#endif
    // When ABC_DERIVE_LENGTH_FROM_METER is 0 and L: is absent, the fixed
    // ABC_DEFAULT_NOTE_NUM/DEN baseline from sheet_init persists.

    // When Q: has no explicit note value (bare "Q:120" or no Q: at all),
    // the tempo refers to the default note length, not quarter notes
    if (!s.has_explicit_Q_note) {
        s.tempo_note_num = sheet->tempo_note_num = s.default_num;
        s.tempo_note_den = sheet->tempo_note_den = s.default_den;
    }

    // Normalize BPM to quarter-note equivalent so ticks_to_ms works correctly.
    // tempo_note tells us what note value the BPM refers to.
    // Quarter-note BPM = BPM * (tempo_note / quarter_note) = BPM * 4 * tempo_note_num / tempo_note_den
    if (s.tempo_note_den > 0) {
        uint32_t effective_bpm = (uint32_t)s.tempo_bpm * 4 * s.tempo_note_num / s.tempo_note_den;
        if (effective_bpm > 0 && effective_bpm <= 0xFFFF) {
            s.tempo_bpm = sheet->tempo_bpm = (uint16_t)effective_bpm;
        }
    }

    return parse_notes(&s, sheet);
}

// ============================================================================
// Quality-of-life API (see DESIGN_QOL.md)
// ============================================================================

size_t abc_storage_bytes(uint8_t voices, uint16_t notes_per_voice) {
    return (size_t)voices * (size_t)notes_per_voice * sizeof(struct note);
}

int abc_sheet_setup(struct sheet *sheet, NotePool *pools, struct note *buffer,
                    uint8_t voices, uint16_t notes_per_voice, uint8_t max_chord_notes) {
    if (!sheet || !pools || !buffer || voices == 0 || notes_per_voice == 0) return -1;
    for (uint8_t v = 0; v < voices; v++) {
        note_pool_init(&pools[v], buffer + (size_t)v * notes_per_voice,
                       notes_per_voice, max_chord_notes);
    }
    sheet_init(sheet, pools, voices);
    return 0;
}

void abc_note_iter_init(AbcNoteIter *it, const NotePool *pool, uint8_t loop) {
    if (!it) return;
    it->pool = pool;
    it->cur = pool ? pool_first_note(pool) : NULL;
    it->loop = loop;
}

struct note *abc_note_iter_next(AbcNoteIter *it) {
    if (!it || !it->cur) return NULL;
    struct note *result = it->cur;
    struct note *nxt = note_next(it->pool, it->cur);
    if (!nxt && it->loop) nxt = pool_first_note(it->pool);
    it->cur = nxt;
    return result;
}

NotePool *abc_sheet_voice(const struct sheet *sheet, uint8_t i) {
    if (!sheet || !sheet->pools || i >= sheet->voice_count) return NULL;
    return &sheet->pools[i];
}

int abc_note_is_rest(const struct note *n) {
    if (!n || n->chord_size == 0) return 1;
    return (n->chord_size == 1 && midi_is_rest(n->midi_note[0]));
}

uint16_t abc_note_frequency_x10(const struct note *n, uint8_t chord_idx) {
    if (!n || chord_idx >= n->chord_size) return 0;
    if (midi_is_rest(n->midi_note[chord_idx])) return 0;
    return midi_to_frequency_x10(n->midi_note[chord_idx]);
}

uint32_t abc_ticks_to_samples(uint8_t ticks, uint16_t bpm, uint32_t sample_rate) {
    if (bpm == 0) bpm = 120;  // Default BPM (matches ticks_to_ms)
    // samples = ticks * 60 * sample_rate / (bpm * PPQ)
    // (derived from ms = ticks*60000/(bpm*PPQ), samples = ms*rate/1000)
    return (uint32_t)((uint64_t)ticks * 60u * sample_rate /
                      ((uint32_t)bpm * ABC_PPQ));
}

uint16_t abc_note_duration_ms(const struct note *n, uint16_t bpm) {
    return n ? ticks_to_ms(n->duration, bpm) : 0;
}

const char *abc_strerror(int code) {
    switch (code) {
        case  0: return "success";
        case -1: return "invalid input";
        case -2: return "note pool exhausted";
        default: return "unknown error";
    }
}

int abc_count_notes(const char *abc, uint16_t *per_voice_out, uint8_t max_voices) {
    if (!abc || !per_voice_out || max_voices == 0) return -1;
    if (max_voices > ABC_COUNT_MAX_VOICES) max_voices = ABC_COUNT_MAX_VOICES;

    NotePool pools[ABC_COUNT_MAX_VOICES];
    struct sheet sheet;

    // NULL storage => count-only pools; capacity caps at the int16_t index space.
    for (uint8_t i = 0; i < max_voices; i++) {
        note_pool_init(&pools[i], NULL, 32767, ABC_MAX_CHORD_NOTES);
    }
    sheet_init(&sheet, pools, max_voices);

    int rc = abc_parse(&sheet, abc);
    if (rc < 0) return rc;

    for (uint8_t i = 0; i < sheet.voice_count; i++) {
        per_voice_out[i] = pools[i].count;
    }
    return sheet.voice_count;
}

// ============================================================================
// Debug printing
// ============================================================================

#ifndef ABC_NO_PRINT
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
#endif // ABC_NO_PRINT
