#include <stdio.h>
#include <string.h>
#include <math.h>
#include "abc_parser.h"

// Test infrastructure
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_MAX_VOICES 2
#define TEST_MAX_NOTES 512

static NotePool g_pools[TEST_MAX_VOICES];
static struct note g_note_storage[TEST_MAX_VOICES][TEST_MAX_NOTES];
static struct sheet g_sheet;

// Convenience macro to get note count from first pool
#define NOTE_COUNT() (g_pools[0].count)
#define TOTAL_TICKS() (g_pools[0].total_ticks)

#define TEST(name) static int test_##name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    sheet_reset(&g_sheet); \
    printf("  %-50s ", #name); \
    if (test_##name()) { \
        tests_passed++; \
        printf("[PASS]\n"); \
    } else { \
        printf("[FAIL]\n"); \
    } \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("ASSERT FAILED: %s (line %d) ", #cond, __LINE__); return 0; } } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { printf("ASSERT_EQ FAILED: %d != %d (line %d) ", (int)(a), (int)(b), __LINE__); return 0; } } while(0)
#define ASSERT_FLOAT_EQ(a, b, tol) do { if (fabs((a) - (b)) > (tol)) { printf("ASSERT_FLOAT_EQ FAILED: %.2f != %.2f (line %d) ", (float)(a), (float)(b), __LINE__); return 0; } } while(0)

// ============================================================================
// Basic Parsing Tests
// ============================================================================

TEST(empty_input) {
    int result = abc_parse(&g_sheet, "");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 0);
    return 1;
}

TEST(null_input) {
    int result = abc_parse(&g_sheet, NULL);
    ASSERT_EQ(result, -1);
    return 1;
}

TEST(null_sheet) {
    int result = abc_parse(NULL, "C D E");
    ASSERT_EQ(result, -1);
    return 1;
}

TEST(single_note_c) {
    int result = abc_parse(&g_sheet, "K:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT(n != NULL);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 4);
    ASSERT_EQ(n->midi_note[0], 60);  // C4 = MIDI 60
    return 1;
}

TEST(single_note_each) {
    int result = abc_parse(&g_sheet, "K:C\nC D E F G A B");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 7);

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_E); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_F); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_G); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_A); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_B);
    return 1;
}

TEST(lowercase_notes) {
    int result = abc_parse(&g_sheet, "K:C\nc d e f g a b");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 7);

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 5);  // lowercase c is octave 5
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 5);
    return 1;
}

// ============================================================================
// Octave Tests
// ============================================================================

TEST(octave_modifier_up) {
    int result = abc_parse(&g_sheet, "K:C\nc c' c''");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 5); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 6); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 6);  // clamped to max 6
    return 1;
}

TEST(octave_modifier_down) {
    int result = abc_parse(&g_sheet, "K:C\nC C, C,,");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 4); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 3); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 2);
    return 1;
}

TEST(uppercase_ab_octave) {
    // ABC standard: All uppercase letters A-G are octave 4
    int result = abc_parse(&g_sheet, "K:C\nA B C");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_A);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 4);  // A4 = 440 Hz
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_B);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 4);  // B4
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 4);  // C4 = middle C
    return 1;
}

// ============================================================================
// Accidental Tests
// ============================================================================

TEST(sharp) {
    int result = abc_parse(&g_sheet, "K:C\n^F");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    // F4 = MIDI 65, F#4 = MIDI 66
    ASSERT_EQ(n->midi_note[0], 66);
    return 1;
}

TEST(flat) {
    int result = abc_parse(&g_sheet, "K:C\n_B");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    // B4 = MIDI 71, Bb4 = MIDI 70
    ASSERT_EQ(n->midi_note[0], 70);
    return 1;
}

TEST(natural) {
    int result = abc_parse(&g_sheet, "K:G\n=F");  // G major has F#, natural cancels it
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    // F4 = MIDI 65 (natural F, not F#)
    ASSERT_EQ(n->midi_note[0], 65);
    return 1;
}

TEST(double_sharp) {
    int result = abc_parse(&g_sheet, "K:C\n^^C");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // C4 = MIDI 60, C##4 = MIDI 62 (same as D)
    ASSERT_EQ(n->midi_note[0], 62);
    return 1;
}

TEST(double_flat) {
    int result = abc_parse(&g_sheet, "K:C\n__B");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // B4 = MIDI 71, Bbb4 = MIDI 69 (same as A)
    ASSERT_EQ(n->midi_note[0], 69);
    return 1;
}

TEST(accidental_persists_in_bar) {
    // Accidental should persist for same note within bar
    int result = abc_parse(&g_sheet, "K:C\n^F F F");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);

    struct note *n = sheet_first_note(&g_sheet);
    // All F's should be F#4 = MIDI 66
    ASSERT_EQ(n->midi_note[0], 66); n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->midi_note[0], 66); n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->midi_note[0], 66);
    return 1;
}

TEST(accidental_resets_at_bar) {
    // Accidental should reset at bar line
    int result = abc_parse(&g_sheet, "K:C\n^F | F");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 2);

    struct note *n = sheet_first_note(&g_sheet);
    // First F is F#4 = MIDI 66, second F resets to F4 = MIDI 65
    ASSERT_EQ(n->midi_note[0], 66); n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->midi_note[0], 65);  // reset after bar
    return 1;
}

// ============================================================================
// Duration Tests
// ============================================================================

TEST(duration_default) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 24);  // 1/8 = 24 ticks (PPQ=48)
    return 1;
}

TEST(duration_double) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC2");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 24 * 2 = 48 ticks
    return 1;
}

TEST(duration_quadruple) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC4");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 96);  // 24 * 4 = 96 ticks
    return 1;
}

TEST(duration_half) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC/2");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 12);  // 24 / 2 = 12 ticks
    return 1;
}

TEST(duration_slash_only) {
    // C/ should be same as C/2
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC/");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 12);  // 24 / 2 = 12 ticks
    return 1;
}

TEST(duration_double_slash) {
    // C// should be C/4
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC//");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 6);  // 24 / 4 = 6 ticks
    return 1;
}

TEST(duration_dotted) {
    // C3/2 = 1.5x duration
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC3/2");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 36);  // 24 * 3/2 = 36 ticks
    return 1;
}

// ============================================================================
// Tuplet Tests
// ============================================================================

TEST(triplet) {
    // (3CDE = triplet: 3 notes in time of 2
    // At L:1/8, normal eighth = 24 ticks, triplet = 24*2/3 = 16 ticks
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\n(3CDE");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 16);  // 24 * 2/3 = 16 ticks
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 16);
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 16);
    return 1;
}

TEST(duplet) {
    // (2CD = duplet: 2 notes in time of 3
    // At L:1/8, normal eighth = 24 ticks, duplet = 24*3/2 = 36 ticks
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\n(2CD");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 36);  // 24 * 3/2 = 36 ticks
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 36);
    return 1;
}

TEST(quadruplet) {
    // (4CDEF = quadruplet: 4 notes in time of 3
    // At L:1/8, normal eighth = 24 ticks, quadruplet = 24*3/4 = 18 ticks
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\n(4CDEF");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 4);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 18);  // 24 * 3/4 = 18 ticks
    return 1;
}

TEST(tuplet_followed_by_normal) {
    // After tuplet ends, normal notes should have normal duration
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\n(3CDE F");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 4);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 16);  // triplet
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 16);  // triplet
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 16);  // triplet
    n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->duration, 24);  // normal
    return 1;
}

// ============================================================================
// Rest Tests
// ============================================================================

TEST(rest_lowercase) {
    int result = abc_parse(&g_sheet, "K:C\nz");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT(midi_is_rest(n->midi_note[0]));
    ASSERT_EQ(midi_to_frequency_x10(n->midi_note[0]), 0);
    return 1;
}

TEST(rest_uppercase) {
    int result = abc_parse(&g_sheet, "K:C\nZ");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT(midi_is_rest(n->midi_note[0]));
    return 1;
}

TEST(rest_with_duration) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nz2");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT(midi_is_rest(n->midi_note[0]));
    ASSERT_EQ(n->duration, 48);  // 24 * 2 = 48 ticks
    return 1;
}

// ============================================================================
// Key Signature Tests
// ============================================================================

TEST(key_c_major) {
    int result = abc_parse(&g_sheet, "K:C\nF");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // F4 = MIDI 65 (natural F in C major)
    ASSERT_EQ(n->midi_note[0], 65);
    return 1;
}

TEST(key_g_major) {
    // G major has F#
    int result = abc_parse(&g_sheet, "K:G\nF");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // F#4 = MIDI 66 (F is sharp in G major)
    ASSERT_EQ(n->midi_note[0], 66);
    return 1;
}

TEST(key_f_major) {
    // F major has Bb
    int result = abc_parse(&g_sheet, "K:F\nB");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // Bb4 = MIDI 70 (B is flat in F major)
    ASSERT_EQ(n->midi_note[0], 70);
    return 1;
}

TEST(key_d_major) {
    // D major has F# and C#
    int result = abc_parse(&g_sheet, "K:D\nF C");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // F#4 = MIDI 66, C#4 = MIDI 61
    ASSERT_EQ(n->midi_note[0], 66); n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->midi_note[0], 61);
    return 1;
}

TEST(key_a_minor) {
    // A minor has no accidentals (same as C major)
    int result = abc_parse(&g_sheet, "K:Am\nF C");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // F4 = MIDI 65, C4 = MIDI 60 (natural)
    ASSERT_EQ(n->midi_note[0], 65); n = note_next(&g_pools[0], n);
    ASSERT_EQ(n->midi_note[0], 60);
    return 1;
}

TEST(key_amin_alternate) {
    int result = abc_parse(&g_sheet, "K:Amin\nF");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    // F4 = MIDI 65 (natural in A minor)
    ASSERT_EQ(n->midi_note[0], 65);
    return 1;
}

// ============================================================================
// Header Field Tests
// ============================================================================

TEST(header_title) {
    int result = abc_parse(&g_sheet, "T:Test Song\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT(strcmp(g_sheet.title, "Test Song") == 0);
    return 1;
}

TEST(header_composer) {
    int result = abc_parse(&g_sheet, "C:John Doe\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT(strcmp(g_sheet.composer, "John Doe") == 0);
    return 1;
}

TEST(header_tempo) {
    int result = abc_parse(&g_sheet, "Q:60\nL:1/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 60);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 1/4 = 48 ticks (tempo doesn't affect ticks)
    return 1;
}

TEST(header_tempo_with_note_value) {
    // Q:1/4=120 means quarter note = 120 BPM
    int result = abc_parse(&g_sheet, "Q:1/4=120\nL:1/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 120);
    ASSERT_EQ(g_sheet.tempo_note_num, 1);
    ASSERT_EQ(g_sheet.tempo_note_den, 4);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 1/4 = 48 ticks
    return 1;
}

TEST(header_tempo_eighth_note) {
    // Q:1/8=120 means eighth note = 120 BPM
    // BPM is normalized to quarter-note equivalent: 120 * 4 * 1/8 = 60
    // Ticks are tempo-independent: L:1/4 = 48 ticks always
    int result = abc_parse(&g_sheet, "Q:1/8=120\nL:1/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 60);  // Normalized to quarter-note BPM
    ASSERT_EQ(g_sheet.tempo_note_num, 1);
    ASSERT_EQ(g_sheet.tempo_note_den, 8);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 1/4 = 48 ticks (tempo-independent)
    return 1;
}

TEST(header_meter) {
    int result = abc_parse(&g_sheet, "M:3/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.meter_num, 3);
    ASSERT_EQ(g_sheet.meter_den, 4);
    return 1;
}

TEST(header_default_length) {
    int result = abc_parse(&g_sheet, "L:1/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.default_note_num, 1);
    ASSERT_EQ(g_sheet.default_note_den, 4);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 1/4 = 48 ticks
    return 1;
}

TEST(header_key) {
    int result = abc_parse(&g_sheet, "K:Gm\nC");
    ASSERT_EQ(result, 0);
    ASSERT(strcmp(g_sheet.key, "Gm") == 0);
    return 1;
}

// ============================================================================
// Repeat Tests
// ============================================================================

TEST(simple_repeat) {
    int result = abc_parse(&g_sheet, "K:C\n|:C D:|");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 4);  // C D C D

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D);
    return 1;
}

TEST(repeat_with_barlines) {
    int result = abc_parse(&g_sheet, "K:C\n|:C | D:|");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 4);
    return 1;
}

TEST(notes_before_repeat) {
    int result = abc_parse(&g_sheet, "K:C\nA B |:C D:|");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 6);  // A B C D C D

    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_A); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_B); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C); n = note_next(&g_pools[0], n);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_D);
    return 1;
}

TEST(notes_after_repeat) {
    int result = abc_parse(&g_sheet, "K:C\n|:C D:| E F");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 6);  // C D C D E F
    return 1;
}

// ============================================================================
// Frequency Tests
// ============================================================================

TEST(frequency_a440) {
    int result = abc_parse(&g_sheet, "K:C\nA");  // A = A4 = 440 Hz
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_FLOAT_EQ(midi_to_frequency_x10(n->midi_note[0]) / 10.0f, 440.0f, 1.0f);
    return 1;
}

TEST(frequency_middle_c) {
    int result = abc_parse(&g_sheet, "K:C\nC");  // C4 = 261.63 Hz
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_FLOAT_EQ(midi_to_frequency_x10(n->midi_note[0]) / 10.0f, 261.6f, 1.0f);
    return 1;
}

// ============================================================================
// MIDI Note Tests
// ============================================================================

TEST(midi_middle_c) {
    int result = abc_parse(&g_sheet, "K:C\nC");  // C4 = MIDI 60
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->midi_note[0], 60);
    return 1;
}

TEST(midi_a440) {
    int result = abc_parse(&g_sheet, "K:C\nA");  // A = A4 = MIDI 69
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->midi_note[0], 69);
    return 1;
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(whitespace_handling) {
    int result = abc_parse(&g_sheet, "K:C\n  C   D  \n  E  ");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    return 1;
}

TEST(ignore_slurs) {
    // Parser should skip slurs ()
    int result = abc_parse(&g_sheet, "K:C\n(C D)");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    return 1;
}

TEST(ignore_chord_symbols) {
    int result = abc_parse(&g_sheet, "K:C\n\"Am\"C D \"G\"E");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    return 1;
}

TEST(bar_line_types) {
    int result = abc_parse(&g_sheet, "K:C\nC | D || E |]");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    return 1;
}

TEST(title_truncation) {
    // Title longer than ABC_MAX_TITLE_LEN should be truncated
    int result = abc_parse(&g_sheet, "T:This is a very long title that exceeds the maximum length allowed\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT(strlen(g_sheet.title) < ABC_MAX_TITLE_LEN);
    return 1;
}

TEST(no_key_signature) {
    // Should work without K: field (defaults to C)
    int result = abc_parse(&g_sheet, "C D E");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    return 1;
}

TEST(only_header_no_notes) {
    int result = abc_parse(&g_sheet, "T:Empty\nK:C\n");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 0);
    ASSERT(strcmp(g_sheet.title, "Empty") == 0);
    return 1;
}

TEST(unknown_characters_skipped) {
    // Unknown characters should be skipped
    int result = abc_parse(&g_sheet, "K:C\nC $ D # E");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    return 1;
}

// ============================================================================
// Pool Exhaustion Test
// ============================================================================

TEST(pool_exhaustion) {
    // Generate more notes than pool capacity
    char big_input[2048];
    strcpy(big_input, "K:C\n");
    for (int i = 0; i < TEST_MAX_NOTES + 10; i++) {
        strcat(big_input, "C ");
    }

    int result = abc_parse(&g_sheet, big_input);
    ASSERT_EQ(result, -2);  // Pool exhausted
    return 1;
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(greensleeves_excerpt) {
    const char *music =
        "T:Greensleeves\n"
        "M:6/8\n"
        "L:1/8\n"
        "Q:120\n"
        "K:Amin\n"
        "A G |: E2 A2 :|";

    int result = abc_parse(&g_sheet, music);
    ASSERT_EQ(result, 0);
    ASSERT(strcmp(g_sheet.title, "Greensleeves") == 0);
    ASSERT_EQ(g_sheet.meter_num, 6);
    ASSERT_EQ(g_sheet.meter_den, 8);
    // Q:120 with L:1/8 (no explicit note value) = 120 eighth notes/min
    // Normalized to quarter-note BPM: 120 * 4 * 1/8 = 60
    ASSERT_EQ(g_sheet.tempo_bpm, 60);
    // A G E2 A2 E2 A2 = 6 notes (repeat unfolded)
    ASSERT_EQ(NOTE_COUNT(), 6);
    return 1;
}

TEST(total_duration) {
    int result = abc_parse(&g_sheet, "L:1/4\nQ:120\nK:C\nC D E F");
    ASSERT_EQ(result, 0);
    // 4 quarter notes = 4 * 48 ticks = 192 ticks
    ASSERT_EQ(TOTAL_TICKS(), 192);
    return 1;
}

// ============================================================================
// Tempo Normalization Tests
// ============================================================================

TEST(tempo_bare_number_with_eighth_default) {
    // Q:120 with L:1/8 means 120 eighth notes per minute
    // Normalized: 120 * 4 * 1/8 = 60 quarter-note BPM
    int result = abc_parse(&g_sheet, "L:1/8\nQ:120\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 60);
    return 1;
}

TEST(tempo_bare_number_with_quarter_default) {
    // Q:120 with L:1/4 means 120 quarter notes per minute
    // Normalized: 120 * 4 * 1/4 = 120 (unchanged)
    int result = abc_parse(&g_sheet, "L:1/4\nQ:120\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 120);
    return 1;
}

TEST(tempo_explicit_dotted_quarter) {
    // Q:3/8=120 means 120 dotted-quarter notes per minute
    // Normalized: 120 * 4 * 3/8 = 180 quarter-note BPM
    int result = abc_parse(&g_sheet, "Q:3/8=120\nL:1/8\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 180);
    ASSERT_EQ(g_sheet.tempo_note_num, 3);
    ASSERT_EQ(g_sheet.tempo_note_den, 8);
    return 1;
}

TEST(tempo_no_q_field_uses_default_length) {
    // No Q: field with L:1/8 → default 120 eighth notes/min = 60 quarter-note BPM
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 60);
    return 1;
}

TEST(tempo_no_q_field_quarter_default) {
    // No Q: field with L:1/4 → default 120 quarter notes/min = 120 quarter-note BPM
    int result = abc_parse(&g_sheet, "L:1/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.tempo_bpm, 120);
    return 1;
}

TEST(default_length_from_meter_68) {
    // M:6/8 without L: → 6/8 >= 3/4, so L:1/8
    int result = abc_parse(&g_sheet, "M:6/8\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.default_note_num, 1);
    ASSERT_EQ(g_sheet.default_note_den, 8);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 24);  // 1/8 = 24 ticks
    return 1;
}

TEST(default_length_from_meter_24) {
    // M:2/4 without L: → 2/4 < 3/4, so L:1/16
    int result = abc_parse(&g_sheet, "M:2/4\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.default_note_num, 1);
    ASSERT_EQ(g_sheet.default_note_den, 16);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 12);  // 1/16 = 12 ticks
    return 1;
}

TEST(explicit_length_overrides_meter) {
    // M:2/4 with explicit L:1/8 → L:1/8 wins
    int result = abc_parse(&g_sheet, "M:2/4\nL:1/8\nK:C\nC");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.default_note_num, 1);
    ASSERT_EQ(g_sheet.default_note_den, 8);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 24);  // 1/8 = 24 ticks
    return 1;
}

// ============================================================================
// Chord Tests
// ============================================================================

TEST(simple_chord) {
    int result = abc_parse(&g_sheet, "K:C\n[CEG]");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->chord_size, 3);
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_C);
    ASSERT_EQ(midi_to_note_name(n->midi_note[1]), NOTE_E);
    ASSERT_EQ(midi_to_note_name(n->midi_note[2]), NOTE_G);
    return 1;
}

TEST(chord_with_octaves) {
    int result = abc_parse(&g_sheet, "K:C\n[ceg]");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->chord_size, 3);
    ASSERT_EQ(midi_to_octave(n->midi_note[0]), 5);  // lowercase
    ASSERT_EQ(midi_to_octave(n->midi_note[1]), 5);
    ASSERT_EQ(midi_to_octave(n->midi_note[2]), 5);
    return 1;
}

TEST(chord_with_accidentals) {
    int result = abc_parse(&g_sheet, "K:C\n[C^E_B]");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->chord_size, 3);
    // C4 = MIDI 60, E#4 = MIDI 65 (E4=64 + 1), Bb4 = MIDI 70
    ASSERT_EQ(n->midi_note[0], 60);  // C4
    ASSERT_EQ(n->midi_note[1], 65);  // E#4 (F4)
    ASSERT_EQ(n->midi_note[2], 70);  // Bb4
    return 1;
}

TEST(chord_with_duration) {
    int result = abc_parse(&g_sheet, "L:1/8\nK:C\n[CEG]2");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 48);  // 24 * 2 = 48 ticks
    return 1;
}

// ============================================================================
// Voice Tests
// ============================================================================

TEST(single_voice) {
    int result = abc_parse(&g_sheet, "K:C\nV:SINE\nC D E");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.voice_count, 1);
    ASSERT(strcmp(g_pools[0].voice_id, "SINE") == 0);
    ASSERT_EQ(g_pools[0].count, 3);
    return 1;
}

TEST(two_voices) {
    int result = abc_parse(&g_sheet, "K:C\nV:SINE\nC D E\nV:SQUARE\nG A B");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.voice_count, 2);
    ASSERT(strcmp(g_pools[0].voice_id, "SINE") == 0);
    ASSERT(strcmp(g_pools[1].voice_id, "SQUARE") == 0);
    ASSERT_EQ(g_pools[0].count, 3);
    ASSERT_EQ(g_pools[1].count, 3);
    return 1;
}

TEST(voice_continuation) {
    // Same voice ID should continue adding to same pool
    int result = abc_parse(&g_sheet, "K:C\nV:A\nC D\nV:B\nE F\nV:A\nG A");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.voice_count, 2);
    ASSERT_EQ(g_pools[0].count, 4);  // C D G A
    ASSERT_EQ(g_pools[1].count, 2);  // E F
    return 1;
}

TEST(voice_without_key) {
    // V: should work without K: field
    int result = abc_parse(&g_sheet, "V:NOISE\nA4");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.voice_count, 1);
    ASSERT(strcmp(g_pools[0].voice_id, "NOISE") == 0);
    ASSERT_EQ(g_pools[0].count, 1);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(n->duration, 96);  // A with 4x duration = 24 * 4 = 96 ticks
    ASSERT_EQ(midi_to_note_name(n->midi_note[0]), NOTE_A);
    return 1;
}

TEST(voice_inline_whitespace) {
    // V: followed by space instead of newline should work
    int result = abc_parse(&g_sheet, "V:NOISE A B C");
    ASSERT_EQ(result, 0);
    ASSERT_EQ(g_sheet.voice_count, 1);
    ASSERT(strcmp(g_pools[0].voice_id, "NOISE") == 0);
    ASSERT_EQ(g_pools[0].count, 3);
    return 1;
}

// ============================================================================
// Quality-of-life API Tests
// ============================================================================

TEST(qol_storage_bytes) {
    ASSERT_EQ(abc_storage_bytes(2, 64), 2 * 64 * (int)sizeof(struct note));
    ASSERT_EQ(abc_storage_bytes(0, 64), 0);
    return 1;
}

TEST(qol_sheet_setup) {
    // One-shot setup over a single flat buffer + ABC_DEFINE_SHEET storage.
    ABC_DEFINE_SHEET(s, 2, 32);
    ASSERT_EQ(abc_sheet_setup(&s, s_pools, s_storage, 2, 32, ABC_MAX_CHORD_NOTES), 0);
    ASSERT_EQ(s_pools[0].capacity, 32);
    ASSERT_EQ(s_pools[1].capacity, 32);
    // Pools must point at distinct, non-overlapping slices.
    ASSERT(s_pools[1].notes == s_pools[0].notes + 32);
    int rc = abc_parse(&s, "L:1/4\nK:C\nV:1\nC D\nV:2\nE\n");
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(s.voice_count, 2);
    ASSERT_EQ(s_pools[0].count, 2);
    ASSERT_EQ(s_pools[1].count, 1);
    // Bad arguments rejected.
    ASSERT_EQ(abc_sheet_setup(&s, s_pools, s_storage, 0, 32, 1), -1);
    ASSERT_EQ(abc_sheet_setup(&s, s_pools, NULL, 2, 32, 1), -1);
    return 1;
}

TEST(qol_note_is_rest) {
    int result = abc_parse(&g_sheet, "K:C\nC z");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(abc_note_is_rest(n), 0);          // C is a note
    ASSERT_EQ(abc_note_is_rest(note_next(&g_pools[0], n)), 1);  // z is a rest
    ASSERT_EQ(abc_note_is_rest(NULL), 1);       // NULL treated as rest
    return 1;
}

TEST(qol_note_frequency_x10) {
    int result = abc_parse(&g_sheet, "K:C\nC z");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(abc_note_frequency_x10(n, 0), 2616);  // C4
    ASSERT_EQ(abc_note_frequency_x10(n, 5), 0);     // out-of-range chord idx
    ASSERT_EQ(abc_note_frequency_x10(note_next(&g_pools[0], n), 0), 0);  // rest -> 0
    return 1;
}

TEST(qol_for_each_note) {
    int result = abc_parse(&g_sheet, "K:C\nC D E F G");
    ASSERT_EQ(result, 0);
    int count = 0;
    ABC_FOR_EACH_NOTE(n, &g_pools[0]) {
        (void)n;
        count++;
    }
    ASSERT_EQ(count, 5);
    ASSERT_EQ(count, (int)g_pools[0].count);
    return 1;
}

TEST(qol_for_each_chord_note) {
    int result = abc_parse(&g_sheet, "K:C\n[CEG]");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    int seen = 0;
    ABC_FOR_EACH_CHORD_NOTE(i, n) {
        ASSERT(n->midi_note[i] != 0);
        seen++;
    }
    ASSERT_EQ(seen, 3);
    return 1;
}

TEST(qol_iterator) {
    int result = abc_parse(&g_sheet, "K:C\nC D E");
    ASSERT_EQ(result, 0);
    AbcNoteIter it;
    abc_note_iter_init(&it, &g_pools[0], 0);
    int count = 0;
    while (abc_note_iter_next(&it)) count++;
    ASSERT_EQ(count, 3);
    ASSERT(abc_note_iter_next(&it) == NULL);  // stays exhausted without loop
    return 1;
}

TEST(qol_iterator_loop) {
    int result = abc_parse(&g_sheet, "K:C\nC D");
    ASSERT_EQ(result, 0);
    AbcNoteIter it;
    abc_note_iter_init(&it, &g_pools[0], 1);  // loop = wrap
    uint8_t midis[6];
    for (int i = 0; i < 6; i++) midis[i] = abc_note_iter_next(&it)->midi_note[0];
    ASSERT_EQ(midis[0], midis[2]);  // C
    ASSERT_EQ(midis[2], midis[4]);
    ASSERT_EQ(midis[1], midis[3]);  // D
    return 1;
}

TEST(qol_sheet_voice) {
    int result = abc_parse(&g_sheet, "K:C\nV:LEAD\nC\nV:BASS\nC,\n");
    ASSERT_EQ(result, 0);
    ASSERT(abc_sheet_voice(&g_sheet, 0) == &g_pools[0]);
    ASSERT(abc_sheet_voice(&g_sheet, 1) == &g_pools[1]);
    ASSERT(abc_sheet_voice(&g_sheet, 2) == NULL);   // beyond voice_count
    ASSERT(abc_sheet_voice(NULL, 0) == NULL);
    return 1;
}

TEST(qol_ticks_to_samples) {
    // 48 ticks (1 quarter @ PPQ=48) at 120 BPM = 500 ms = 11025 samples @ 22050 Hz
    ASSERT_EQ(abc_ticks_to_samples(48, 120, 22050), 11025);
    ASSERT_EQ(abc_ticks_to_samples(48, 0, 22050), 11025);  // bpm 0 defaults to 120
    return 1;
}

TEST(qol_note_duration_ms) {
    int result = abc_parse(&g_sheet, "L:1/4\nQ:1/4=120\nK:C\nC");
    ASSERT_EQ(result, 0);
    struct note *n = sheet_first_note(&g_sheet);
    ASSERT_EQ(abc_note_duration_ms(n, g_sheet.tempo_bpm),
              ticks_to_ms(n->duration, g_sheet.tempo_bpm));
    ASSERT_EQ(abc_note_duration_ms(NULL, 120), 0);
    return 1;
}

TEST(qol_strerror) {
    ASSERT(strcmp(abc_strerror(0), "success") == 0);
    ASSERT(strcmp(abc_strerror(-1), "invalid input") == 0);
    ASSERT(strcmp(abc_strerror(-2), "note pool exhausted") == 0);
    ASSERT(abc_strerror(42) != NULL);  // unknown still returns a string
    return 1;
}

TEST(qol_count_notes_matches_parse) {
    // Repeat must be counted as expanded: |:C D:| => 4 notes.
    const char *music = "K:C\n|:C D:| E";
    uint16_t counts[2] = {0, 0};
    int voices = abc_count_notes(music, counts, 2);
    ASSERT_EQ(voices, 1);
    ASSERT_EQ(counts[0], 5);  // (C D)*2 + E
    // Dry-run prediction must match a real parse.
    int rc = abc_parse(&g_sheet, music);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(g_pools[0].count, counts[0]);
    return 1;
}

TEST(qol_count_notes_multivoice) {
    const char *music = "L:1/4\nK:C\nV:1\nC D E\nV:2\nG A\n";
    uint16_t counts[2] = {0, 0};
    int voices = abc_count_notes(music, counts, 2);
    ASSERT_EQ(voices, 2);
    ASSERT_EQ(counts[0], 3);
    ASSERT_EQ(counts[1], 2);
    return 1;
}

TEST(qol_count_notes_bad_args) {
    uint16_t counts[2];
    ASSERT_EQ(abc_count_notes(NULL, counts, 2), -1);
    ASSERT_EQ(abc_count_notes("C D", NULL, 2), -1);
    ASSERT_EQ(abc_count_notes("C D", counts, 0), -1);
    return 1;
}

// ============================================================================
// Spec Coverage Tests (broken rhythm, grace, ties, modes, inline fields, endings)
// ============================================================================

TEST(broken_rhythm_gt) {
    // D2>E2 at L:1/16: D2=24 -> dotted x3/2 = 36; E2=24 -> cut x1/2 = 12
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/16\nK:C\nD2>E2"), 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, 36);
    ASSERT_EQ(note_get(&g_pools[0], 1)->duration, 12);
    return 1;
}

TEST(broken_rhythm_lt) {
    // G2<C2: G cut to 12, C dotted to 36
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/16\nK:C\nG2<C2"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, 12);
    ASSERT_EQ(note_get(&g_pools[0], 1)->duration, 36);
    return 1;
}

TEST(broken_rhythm_preserves_total) {
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/16\nK:C\nD2>E2"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration + note_get(&g_pools[0], 1)->duration, 48);
    return 1;
}

TEST(broken_rhythm_double) {
    // D2>>E2: x7/4 = 42, x1/4 = 6
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/16\nK:C\nD2>>E2"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, 42);
    ASSERT_EQ(note_get(&g_pools[0], 1)->duration, 6);
    return 1;
}

TEST(grace_steals_from_principal) {
    // {d}f4 at L:1/16: f4 base=48; grace d=ABC_GRACE_NOTE_TICKS; principal=48-that
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/16\nK:C\n{d}f4"), 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, ABC_GRACE_NOTE_TICKS);
    ASSERT_EQ(note_get(&g_pools[0], 1)->duration, 48 - ABC_GRACE_NOTE_TICKS);
    ASSERT_EQ(TOTAL_TICKS(), 48);  // bar length preserved
    return 1;
}

TEST(grace_multiple) {
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\n{ge}d2"), 0);
    ASSERT_EQ(NOTE_COUNT(), 3);
    ASSERT_EQ(TOTAL_TICKS(), 48);  // d2 base fully conserved
    return 1;
}

TEST(tie_same_pitch_merges) {
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\nA-A"), 0);
    ASSERT_EQ(NOTE_COUNT(), 1);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, 48);  // 24 + 24
    return 1;
}

TEST(tie_different_pitch_kept) {
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\nA-B"), 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    return 1;
}

TEST(mode_dorian) {
    // A Dorian = 1 sharp (F#). F4 -> F#4 = 66
    ASSERT_EQ(abc_parse(&g_sheet, "K:Ador\nL:1/8\nF"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->midi_note[0], 66);
    return 1;
}

TEST(mode_mixolydian) {
    // G Mixolydian = no accidentals (F natural). F4 = 65
    ASSERT_EQ(abc_parse(&g_sheet, "K:Gmix\nL:1/8\nF"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->midi_note[0], 65);
    return 1;
}

TEST(mode_minor_still_works) {
    // D minor = Bb. B4 -> Bb4 = 70
    ASSERT_EQ(abc_parse(&g_sheet, "K:Dm\nL:1/8\nB"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->midi_note[0], 70);
    return 1;
}

TEST(inline_key_change) {
    // F natural, then [K:G] makes the next F an F#
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\nF[K:G]F"), 0);
    ASSERT_EQ(NOTE_COUNT(), 2);
    ASSERT_EQ(note_get(&g_pools[0], 0)->midi_note[0], 65);  // F natural
    ASSERT_EQ(note_get(&g_pools[0], 1)->midi_note[0], 66);  // F#
    return 1;
}

TEST(inline_length_change) {
    // L:1/4 then inline [L:1/8] halves the next note's length
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/4\nK:C\nC[L:1/8]C"), 0);
    ASSERT_EQ(note_get(&g_pools[0], 0)->duration, 48);  // 1/4
    ASSERT_EQ(note_get(&g_pools[0], 1)->duration, 24);  // 1/8
    return 1;
}

TEST(endings_pipe_form) {
    // |:C|1D:|2E|  unfolds to C D C E
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\n|:C|1D:|2E|"), 0);
    ASSERT_EQ(NOTE_COUNT(), 4);
    ASSERT_EQ(note_get(&g_pools[0], 0)->midi_note[0], 60);  // C
    ASSERT_EQ(note_get(&g_pools[0], 1)->midi_note[0], 62);  // D (1st ending)
    ASSERT_EQ(note_get(&g_pools[0], 2)->midi_note[0], 60);  // C (repeat)
    ASSERT_EQ(note_get(&g_pools[0], 3)->midi_note[0], 64);  // E (2nd ending)
    return 1;
}

TEST(endings_bracket_form) {
    // |:C[1D:|[2E|  same unfold C D C E
    ASSERT_EQ(abc_parse(&g_sheet, "L:1/8\nK:C\n|:C[1D:|[2E|"), 0);
    ASSERT_EQ(NOTE_COUNT(), 4);
    ASSERT_EQ(note_get(&g_pools[0], 3)->midi_note[0], 64);
    return 1;
}

TEST(caerketton_strathspey) {
    // Real-world strathspey: heavy broken rhythm, grace notes, chord-symbol
    // annotations, repeats. Must parse and conserve every bar's length.
    const char *tune =
        "X:2399\nT:Caerketton\nT:AA BB\nR:strathspey\nC:Iain Robertson\n"
        "M:4/4\nL:1/16\nK:Dm\n"
        "\"A\"|:\"Dm\"A,4 D2>E2 F2>G2 A4 | \"C\"G2<C2 C4 G2<C2 C4 | "
        "\"Dm\"A,4 D2>E2 F2>G2 A4 | \"Am7\"{d}f4 A,3B,/C/ \"Dm\"D8 :|\n"
        "\"B\"|:\"F\"F4 F2>G2 A2<c2 f4 | \"C7\" c2<C2 C4 c2<C2 C4 | "
        "\"Dm\"D4 F2>G2 A4 d2<d2 | \"Am7\"{d}a4 A,3B,/C/ \"Dm\"D8 :|\n";
    ASSERT_EQ(abc_parse(&g_sheet, tune), 0);
    ASSERT_EQ(g_sheet.voice_count, 1);
    ASSERT(strcmp(g_sheet.key, "Dm") == 0);
    // 2 sections x 4 bars, each repeated = 16 bars of 4/4 = 16 * 192 ticks.
    ASSERT_EQ(TOTAL_TICKS(), 16 * 192);
    return 1;
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("ABC Parser Test Suite\n");
    printf("=====================\n\n");

    // Initialize pools with external storage
    for (int i = 0; i < TEST_MAX_VOICES; i++) {
        note_pool_init(&g_pools[i], g_note_storage[i], TEST_MAX_NOTES, ABC_MAX_CHORD_NOTES);
    }
    sheet_init(&g_sheet, g_pools, TEST_MAX_VOICES);

    printf("Basic Parsing:\n");
    RUN_TEST(empty_input);
    RUN_TEST(null_input);
    RUN_TEST(null_sheet);
    RUN_TEST(single_note_c);
    RUN_TEST(single_note_each);
    RUN_TEST(lowercase_notes);

    printf("\nOctave Handling:\n");
    RUN_TEST(octave_modifier_up);
    RUN_TEST(octave_modifier_down);
    RUN_TEST(uppercase_ab_octave);

    printf("\nAccidentals:\n");
    RUN_TEST(sharp);
    RUN_TEST(flat);
    RUN_TEST(natural);
    RUN_TEST(double_sharp);
    RUN_TEST(double_flat);
    RUN_TEST(accidental_persists_in_bar);
    RUN_TEST(accidental_resets_at_bar);

    printf("\nDurations:\n");
    RUN_TEST(duration_default);
    RUN_TEST(duration_double);
    RUN_TEST(duration_quadruple);
    RUN_TEST(duration_half);
    RUN_TEST(duration_slash_only);
    RUN_TEST(duration_double_slash);
    RUN_TEST(duration_dotted);

    printf("\nTuplets:\n");
    RUN_TEST(triplet);
    RUN_TEST(duplet);
    RUN_TEST(quadruplet);
    RUN_TEST(tuplet_followed_by_normal);

    printf("\nRests:\n");
    RUN_TEST(rest_lowercase);
    RUN_TEST(rest_uppercase);
    RUN_TEST(rest_with_duration);

    printf("\nKey Signatures:\n");
    RUN_TEST(key_c_major);
    RUN_TEST(key_g_major);
    RUN_TEST(key_f_major);
    RUN_TEST(key_d_major);
    RUN_TEST(key_a_minor);
    RUN_TEST(key_amin_alternate);

    printf("\nHeader Fields:\n");
    RUN_TEST(header_title);
    RUN_TEST(header_composer);
    RUN_TEST(header_tempo);
    RUN_TEST(header_tempo_with_note_value);
    RUN_TEST(header_tempo_eighth_note);
    RUN_TEST(header_meter);
    RUN_TEST(header_default_length);
    RUN_TEST(header_key);

    printf("\nRepeats:\n");
    RUN_TEST(simple_repeat);
    RUN_TEST(repeat_with_barlines);
    RUN_TEST(notes_before_repeat);
    RUN_TEST(notes_after_repeat);

    printf("\nFrequency Calculation:\n");
    RUN_TEST(frequency_a440);
    RUN_TEST(frequency_middle_c);

    printf("\nMIDI Notes:\n");
    RUN_TEST(midi_middle_c);
    RUN_TEST(midi_a440);

    printf("\nEdge Cases:\n");
    RUN_TEST(whitespace_handling);
    RUN_TEST(ignore_slurs);
    RUN_TEST(ignore_chord_symbols);
    RUN_TEST(bar_line_types);
    RUN_TEST(title_truncation);
    RUN_TEST(no_key_signature);
    RUN_TEST(only_header_no_notes);
    RUN_TEST(unknown_characters_skipped);

    printf("\nError Handling:\n");
    RUN_TEST(pool_exhaustion);

    printf("\nTempo Normalization:\n");
    RUN_TEST(tempo_bare_number_with_eighth_default);
    RUN_TEST(tempo_bare_number_with_quarter_default);
    RUN_TEST(tempo_explicit_dotted_quarter);
    RUN_TEST(tempo_no_q_field_uses_default_length);
    RUN_TEST(tempo_no_q_field_quarter_default);
    RUN_TEST(default_length_from_meter_68);
    RUN_TEST(default_length_from_meter_24);
    RUN_TEST(explicit_length_overrides_meter);

    printf("\nIntegration Tests:\n");
    RUN_TEST(greensleeves_excerpt);
    RUN_TEST(total_duration);

    printf("\nChord Tests:\n");
    RUN_TEST(simple_chord);
    RUN_TEST(chord_with_octaves);
    RUN_TEST(chord_with_accidentals);
    RUN_TEST(chord_with_duration);

    printf("\nVoice Tests:\n");
    RUN_TEST(single_voice);
    RUN_TEST(two_voices);
    RUN_TEST(voice_continuation);
    RUN_TEST(voice_without_key);
    RUN_TEST(voice_inline_whitespace);

    printf("\nQuality-of-life API Tests:\n");
    RUN_TEST(qol_storage_bytes);
    RUN_TEST(qol_sheet_setup);
    RUN_TEST(qol_note_is_rest);
    RUN_TEST(qol_note_frequency_x10);
    RUN_TEST(qol_for_each_note);
    RUN_TEST(qol_for_each_chord_note);
    RUN_TEST(qol_iterator);
    RUN_TEST(qol_iterator_loop);
    RUN_TEST(qol_sheet_voice);
    RUN_TEST(qol_ticks_to_samples);
    RUN_TEST(qol_note_duration_ms);
    RUN_TEST(qol_strerror);
    RUN_TEST(qol_count_notes_matches_parse);
    RUN_TEST(qol_count_notes_multivoice);
    RUN_TEST(qol_count_notes_bad_args);

    printf("\nSpec Coverage Tests:\n");
    RUN_TEST(broken_rhythm_gt);
    RUN_TEST(broken_rhythm_lt);
    RUN_TEST(broken_rhythm_preserves_total);
    RUN_TEST(broken_rhythm_double);
    RUN_TEST(grace_steals_from_principal);
    RUN_TEST(grace_multiple);
    RUN_TEST(tie_same_pitch_merges);
    RUN_TEST(tie_different_pitch_kept);
    RUN_TEST(mode_dorian);
    RUN_TEST(mode_mixolydian);
    RUN_TEST(mode_minor_still_works);
    RUN_TEST(inline_key_change);
    RUN_TEST(inline_length_change);
    RUN_TEST(endings_pipe_form);
    RUN_TEST(endings_bracket_form);
    RUN_TEST(caerketton_strathspey);

    printf("\n=====================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
