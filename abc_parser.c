#include "abc_parser.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Frequency lookup table (frequency * 10, stored as uint16_t)
// ============================================================================

// Octaves 0-6, where octave 4 contains A440
const uint16_t frequencies_x10[12][7] = {
    {  164,   327,   654,  1308,  2616,  5233, 10465 }, // C
    {  173,   347,   693,  1386,  2772,  5544, 11087 }, // C#/Db
    {  184,   367,   734,  1468,  2937,  5873, 11747 }, // D
    {  195,   389,   778,  1556,  3111,  6223, 12445 }, // D#/Eb
    {  206,   412,   824,  1648,  3296,  6593, 13185 }, // E
    {  218,   437,   873,  1746,  3492,  6985, 13969 }, // F
    {  231,   463,   925,  1850,  3700,  7400, 14800 }, // F#/Gb
    {  245,   490,   980,  1960,  3920,  7840, 15680 }, // G
    {  260,   519,  1038,  2077,  4153,  8306, 16612 }, // G#/Ab
    {  275,   550,  1100,  2200,  4400,  8800, 17600 }, // A
    {  291,   583,  1165,  2331,  4662,  9323, 18647 }, // A#/Bb
    {  309,   617,  1235,  2469,  4939,  9878, 19756 }, // B
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
    int8_t key_accidentals[7];
    int8_t bar_accidentals[7];
    int16_t repeat_start_index;
    int16_t repeat_end_index;
    uint8_t in_repeat;
} ParserState;

// ============================================================================
// Utility functions
// ============================================================================

float note_to_frequency(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0.0f;

    int semitone = note_to_semitone[name] + acc;
    if (acc == ACC_NATURAL) semitone = note_to_semitone[name];

    while (semitone < 0) { semitone += 12; octave--; }
    while (semitone >= 12) { semitone -= 12; octave++; }
    if (octave < 0) octave = 0;
    if (octave > 6) octave = 6;

    return frequencies_x10[semitone][octave] / 10.0f;
}

static uint16_t note_to_frequency_x10(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0;

    int semitone = note_to_semitone[name] + acc;
    if (acc == ACC_NATURAL) semitone = note_to_semitone[name];

    while (semitone < 0) { semitone += 12; octave--; }
    while (semitone >= 12) { semitone -= 12; octave++; }
    if (octave < 0) octave = 0;
    if (octave > 6) octave = 6;

    return frequencies_x10[semitone][octave];
}

int note_to_midi(NoteName name, int octave, int8_t acc) {
    if (name == NOTE_REST) return 0;
    int semitone = note_to_semitone[name] + acc;
    if (acc == ACC_NATURAL) semitone = note_to_semitone[name];
    return 12 + (octave * 12) + semitone;
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
}

void note_pool_reset(NotePool *pool) {
    if (pool) pool->count = 0;
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
    n->frequency_x10 = 0;
    n->duration_ms = 0;
    n->note_name = NOTE_C;
    n->octave = 4;
    n->accidental = ACC_NONE;
    n->midi_note = 0;
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
    sheet->tempo_bpm = 120;
    sheet->total_duration_ms = 0;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;
}

void sheet_reset(struct sheet *sheet) {
    if (!sheet) return;
    if (sheet->note_pool) note_pool_reset(sheet->note_pool);
    sheet->head_index = -1;
    sheet->tail_index = -1;
    sheet->note_count = 0;
    sheet->total_duration_ms = 0;
    sheet->tempo_bpm = 120;
    sheet->default_note_num = 1;
    sheet->default_note_den = 8;
    sheet->meter_num = 4;
    sheet->meter_den = 4;
    sheet->title[0] = '\0';
    sheet->composer[0] = '\0';
    sheet->key[0] = '\0';
}

struct note *note_get(const struct sheet *sheet, int index) {
    if (!sheet || !sheet->note_pool) return NULL;
    if (index < 0 || index >= (int)sheet->note_pool->count) return NULL;
    return &sheet->note_pool->notes[index];
}

struct note *sheet_first_note(const struct sheet *sheet) {
    return sheet ? note_get(sheet, sheet->head_index) : NULL;
}

struct note *note_next(const struct sheet *sheet, const struct note *current) {
    return (sheet && current) ? note_get(sheet, current->next_index) : NULL;
}

static int sheet_append_note(struct sheet *sheet, NoteName name, int octave,
                              int8_t acc, uint16_t duration_ms) {
    if (!sheet || !sheet->note_pool) return -1;

    int16_t index = note_pool_alloc(sheet->note_pool);
    if (index < 0) return -1;

    struct note *n = &sheet->note_pool->notes[index];
    n->note_name = name;
    n->octave = (uint8_t)octave;
    n->accidental = acc;
    n->duration_ms = duration_ms;
    n->frequency_x10 = note_to_frequency_x10(name, octave, acc);
    n->midi_note = (uint8_t)note_to_midi(name, octave, acc);
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

static uint16_t calculate_duration_ms(ParserState *s, int num, int den) {
    uint32_t quarter_ms = 60000 / s->tempo_bpm;
    uint32_t default_ms = quarter_ms * 4 * s->default_num / s->default_den;
    return (uint16_t)(default_ms * num / den);
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

        // Find end of line
        while (end < s->len && s->input[end] != '\n' && s->input[end] != '\r') end++;
        uint16_t line_end = end;
        if (end < s->len) end++; // skip newline

        // Trim whitespace
        while (start < line_end && s->input[start] == ' ') start++;
        while (line_end > start && (s->input[line_end-1] == ' ' || s->input[line_end-1] == '\t')) line_end--;

        uint8_t vlen = (uint8_t)(line_end - start);
        const char *val = s->input + start;

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
                int tempo = 0;
                for (uint8_t i = 0; i < vlen; i++) {
                    if (val[i] >= '0' && val[i] <= '9') tempo = tempo * 10 + (val[i] - '0');
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
        }
        s->pos = end;
    }
}

// ============================================================================
// Note parsing
// ============================================================================

static int parse_single_note(ParserState *s, struct sheet *sheet) {
    skip_whitespace(s);
    if (s->pos >= s->len) return 1;

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

    if (c >= 'C' && c <= 'G') { name = (NoteName)(c - 'C'); octave = 4; advance(s); }
    else if (c == 'A') { name = NOTE_A; octave = 3; advance(s); }
    else if (c == 'B') { name = NOTE_B; octave = 3; advance(s); }
    else if (c >= 'c' && c <= 'g') { name = (NoteName)(c - 'c'); octave = 5; advance(s); }
    else if (c == 'a') { name = NOTE_A; octave = 5; advance(s); }
    else if (c == 'b') { name = NOTE_B; octave = 5; advance(s); }
    else if (c == 'z' || c == 'Z') { name = NOTE_REST; advance(s); }
    else return 1;

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

    uint16_t duration = calculate_duration_ms(s, num, den);
    return sheet_append_note(sheet, name, octave, acc, duration);
}

static int copy_repeat_section(struct sheet *sheet, int16_t start_idx, int16_t end_idx) {
    if (!sheet || !sheet->note_pool || start_idx < 0) return 0;

    int16_t cur = start_idx;
    while (cur >= 0 && cur <= end_idx && cur < (int16_t)sheet->note_pool->count) {
        struct note *src = &sheet->note_pool->notes[cur];
        if (sheet_append_note(sheet, (NoteName)src->note_name, src->octave,
                               src->accidental, src->duration_ms) < 0) return -1;
        cur = src->next_index;
        if (cur < 0) break;
    }
    return 0;
}

static int parse_notes(ParserState *s, struct sheet *sheet) {
    s->repeat_start_index = -1;
    s->repeat_end_index = -1;
    s->in_repeat = 0;

    while (s->pos < s->len) {
        skip_whitespace(s);
        if (s->pos >= s->len) break;

        char c = peek(s);

        if (c == '|') {
            advance(s);
            memset(s->bar_accidentals, 0, 7);
            c = peek(s);
            if (c == ':') {
                advance(s);
                s->in_repeat = 1;
                s->repeat_start_index = sheet->note_pool ? (int16_t)sheet->note_pool->count : 0;
            } else if (c == '|' || c == ']') {
                advance(s);
            }
            continue;
        }

        if (c == ':') {
            advance(s);
            if (peek(s) == '|') {
                advance(s);
                s->repeat_end_index = sheet->note_pool ? (int16_t)(sheet->note_pool->count - 1) : -1;
                if (peek(s) == ':') {
                    advance(s);
                    if (copy_repeat_section(sheet, s->repeat_start_index, s->repeat_end_index) < 0) return -2;
                    s->repeat_start_index = sheet->note_pool ? (int16_t)sheet->note_pool->count : 0;
                } else {
                    if (copy_repeat_section(sheet, s->repeat_start_index, s->repeat_end_index) < 0) return -2;
                    s->in_repeat = 0;
                    s->repeat_start_index = -1;
                }
            }
            continue;
        }

        if (c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '!' || c == '+' ||
            c == '-' || c == '<' || c == '>' || c == '~' || c == '%') {
            advance(s);
            continue;
        }

        if (c == '"') {
            advance(s);
            while (s->pos < s->len && peek(s) != '"') advance(s);
            if (peek(s) == '"') advance(s);
            continue;
        }

        int result = parse_single_note(s, sheet);
        if (result < 0) return -2;
        if (result > 0) advance(s);
    }
    return 0;
}

// ============================================================================
// Main parse function
// ============================================================================

int abc_parse(struct sheet *sheet, const char *abc_string) {
    if (!sheet || !abc_string || !sheet->note_pool) return -1;

    uint16_t len = 0;
    while (abc_string[len] && len < 0xFFFF) len++;

    ParserState s = {
        .input = abc_string,
        .pos = 0,
        .len = len,
        .default_num = sheet->default_note_num,
        .default_den = sheet->default_note_den,
        .tempo_bpm = sheet->tempo_bpm,
        .meter_num = sheet->meter_num,
        .meter_den = sheet->meter_den,
        .repeat_start_index = -1,
        .repeat_end_index = -1,
        .in_repeat = 0
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
    printf("Tempo: %u BPM\n", sheet->tempo_bpm);
    printf("Meter: %u/%u\n", sheet->meter_num, sheet->meter_den);
    printf("Default note: %u/%u\n", sheet->default_note_num, sheet->default_note_den);
    printf("Total notes: %u\n", sheet->note_count);
    printf("Total duration: %lu ms (%.2f s)\n",
           (unsigned long)sheet->total_duration_ms, sheet->total_duration_ms / 1000.0f);
    if (sheet->note_pool) {
        printf("Pool usage: %u/%u notes\n", sheet->note_pool->count, sheet->note_pool->capacity);
    }

    printf("\n--- Notes ---\n");
    printf("%-4s %-6s %-4s %-10s %-8s %-5s\n", "#", "Note", "Oct", "Freq", "Dur", "MIDI");
    printf("----------------------------------------------\n");

    int i = 1;
    struct note *n = sheet_first_note(sheet);
    while (n) {
        if (n->note_name == NOTE_REST) {
            printf("%-4d %-6s %-4s %-10s %-8u %-5s\n", i, "rest", "-", "-", n->duration_ms, "-");
        } else {
            char ns[8];
            snprintf(ns, sizeof(ns), "%s%s", note_name_to_string((NoteName)n->note_name),
                     accidental_to_string(n->accidental));
            printf("%-4d %-6s %-4u %-10.2f %-8u %-5u\n",
                   i, ns, n->octave, n->frequency_x10 / 10.0f, n->duration_ms, n->midi_note);
        }
        n = note_next(sheet, n);
        i++;
    }
    printf("==============================================\n");
}
