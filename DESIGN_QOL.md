# Design: Quality-of-Life API Improvements

Status: proposed
Scope: additive, backward-compatible (minor version bump)

## Motivation

The current API is solid and genuinely embedded-friendly (zero dynamic
allocation, index-based note links, integer `*_x10` frequencies). The friction
is **ceremony and duplicated glue**: every consumer re-implements the same small
helpers and re-types the same multi-step init.

Evidence from downstream consumers (`main.c` here, and the TinyBit engine's
`audio.c`):

- The `midi_to_frequency_x10(...) / 10.0f` dance is repeated (3x in `main.c`,
  once in `audio.c` as `get_frequency_from_chord`).
- `audio.c` hand-wrote `is_rest_note` (rest detection).
- `audio.c` hand-wrote `duration_ticks_to_samples` (ticks -> samples).
- The init dance — `struct note storage[V][N]` -> build a `struct note *[]`
  pointer array -> loop `note_pool_init` -> `sheet_init` — is open-coded twice
  (`audio.c::init_channel`, `main.c::main`).

That duplication is the signal for what to pull into the library.

## Guiding constraints

1. **Portable / pure C / embedded-first.** No dynamic allocation. The core data
   path stays integer-only (see "Floating point" below).
2. **No breaking changes.** No existing public symbol is removed or has its
   signature changed. No struct layout changes. New build-mode defines preserve
   current behavior.
3. **Collision-safe naming.** All new public functions take the `abc_` prefix;
   all new macros take the `ABC_` prefix. (Existing symbols keep their current
   names — renaming them would be breaking.)

## Floating point: current state

The **core data path is already 100% integer**. The parser stores MIDI note
numbers + tick durations; the canonical frequency lookup
`midi_to_frequency_x10()` returns `uint16_t`; `ticks_to_ms` / `pool_total_ms`
are integer.

Float currently appears in only two **non-functional** places:

- `note_to_frequency()` — public, returns `float`, but has **zero callers**
  anywhere (parser, `main.c`, tests). Body is a plain `int / 10.0f`; no libm.
- `sheet_print()` — debug printer, uses `%.2f`; already pulls in `<stdio.h>`.

So "integer-only core" is the *current* reality, not a goal. The work is to keep
it that way and let freestanding builds drop the two float/stdio spots.

---

## Proposed additions

All names below are final (with `abc_` / `ABC_` prefixes). None change existing
struct layouts (`struct note`, `struct sheet`, `NotePool`).

### A. Setup / init

```c
// One-shot: slice one contiguous note buffer into `voices` equal pools and
// initialize the sheet. Replaces the pointer-array build + per-voice loop.
// Returns 0 on success, negative on bad args.
int abc_sheet_setup(struct sheet *sheet, NotePool *pools,
                    struct note *buffer, uint8_t voices,
                    uint16_t notes_per_voice, uint8_t max_chord_notes);

// Bytes required to back `voices` pools of `notes_per_voice` each.
// For consumers laying out their own arena (e.g. audio.c).
size_t abc_storage_bytes(uint8_t voices, uint16_t notes_per_voice);
```

```c
// Declares static pools + storage + sheet for the common static case.
// Zero runtime cost; pairs with abc_sheet_setup().
#define ABC_DEFINE_SHEET(name, VOICES, NOTES_PER_VOICE) \
    static NotePool       name##_pools[VOICES];          \
    static struct note    name##_storage[(VOICES) * (NOTES_PER_VOICE)]; \
    static struct sheet   name
```

Replaces, in `audio.c::init_channel` and `main.c`:

```c
// before
static NotePool pools[V];
static struct note storage[V][N];
struct note *ptrs[V] = { storage[0], storage[1], ... };
for (int i = 0; i < V; i++) note_pool_init(&pools[i], ptrs[i], N, CH);
sheet_init(&sheet, pools, V);

// after
ABC_DEFINE_SHEET(g, V, N);
abc_sheet_setup(&g, g_pools, g_storage, V, N, CH);
```

### B. Frequency / MIDI helpers

```c
// Rest- and bounds-safe frequency for a chord member. Returns 0 for rest/oob.
// Canonical integer form (this is audio.c::get_frequency_from_chord).
uint16_t abc_note_frequency_x10(const struct note *n, uint8_t chord_idx);

#ifndef ABC_NO_FLOAT
// Float conveniences (header-only inline; excluded entirely under ABC_NO_FLOAT).
static inline float abc_midi_to_frequency(uint8_t midi) {
    return midi_to_frequency_x10(midi) / 10.0f;
}
static inline float abc_note_frequency(const struct note *n, uint8_t chord_idx) {
    return abc_note_frequency_x10(n, chord_idx) / 10.0f;
}
#endif
```

The integer `midi_to_frequency_x10()` remains the canonical lookup and is
unchanged.

### C. Iteration

```c
// Zero-cost for-each macros. Remove the repeated `note_next(pool, n)`.
#define ABC_FOR_EACH_NOTE(n, pool) \
    for (struct note *n = pool_first_note(pool); n; n = note_next((pool), n))

#define ABC_FOR_EACH_CHORD_NOTE(i, n) \
    for (uint8_t i = 0; i < (n)->chord_size; i++)
```

```c
// Iterator struct for suspended / resumable iteration (e.g. per-voice playback
// cursors). Models audio.c's `current_note` + `advance_to_next_note`.
typedef struct {
    const NotePool *pool;
    struct note    *cur;
    uint8_t         loop;   // if set, abc_note_iter_next wraps to first note
} AbcNoteIter;

void         abc_note_iter_init(AbcNoteIter *it, const NotePool *pool, uint8_t loop);
struct note *abc_note_iter_next(AbcNoteIter *it);   // NULL at end (unless loop)

// Bounds-checked voice accessor (returns NULL for out-of-range i).
NotePool    *abc_sheet_voice(const struct sheet *sheet, uint8_t i);
```

### D. Note inspection

```c
// Rest detection, handling chord_size==0 and the single-note midi==0 case.
// This is audio.c::is_rest_note. Pure integer.
int abc_note_is_rest(const struct note *n);
```

### E. Duration

```c
// ticks -> output samples, parameterized by sample rate. Pure integer.
// This is audio.c::duration_ticks_to_samples generalized.
uint32_t abc_ticks_to_samples(uint8_t ticks, uint16_t bpm, uint32_t sample_rate);

// Convenience over ticks_to_ms(n->duration, bpm).
uint16_t abc_note_duration_ms(const struct note *n, uint16_t bpm);
```

### F. Error handling / sizing

```c
// Map abc_parse() return codes (-1 invalid input, -2 pool exhausted) to text.
const char *abc_strerror(int code);

// Pre-flight: dry-run count of notes per voice WITHOUT writing the pools, so
// embedded callers can size buffers before committing storage. Returns voice
// count, or negative on bad input. (Larger item; own implementation pass.)
int abc_count_notes(const char *abc, uint16_t *per_voice_out, uint8_t max_voices);
```

### G. Portability hygiene

```c
// 1. C++ / Arduino linkage. Header wraps declarations:
#ifdef __cplusplus
extern "C" {
#endif
/* ... */
#ifdef __cplusplus
}
#endif

// 2. Version macros for feature detection.
#define ABC_PARSER_VERSION_MAJOR 1
#define ABC_PARSER_VERSION_MINOR 1
#define ABC_PARSER_VERSION_PATCH 0
```

Build-mode defines (both default to **current behavior**, so existing builds are
unaffected):

- `ABC_NO_FLOAT` — excludes the new inline float helpers (B). Also gate the
  existing dead `note_to_frequency()` under this define (kept by default; it has
  no callers, so excluding it loses no functionality). A soft-float build becomes
  float-clean.
- `ABC_NO_PRINT` — excludes `sheet_print()`. It is the only reason `<stdio.h>`
  enters the translation unit and the only remaining float user in core. With
  both `ABC_NO_FLOAT` + `ABC_NO_PRINT`, the library compiles freestanding with
  only `<stdint.h>` + `<string.h>`, no FPU.

---

## Backward compatibility

No breaking changes when shipped as described:

- **No struct layout changes.** `struct note`, `struct sheet`, `NotePool` are
  untouched (critical for persisted/FFI-mapped state and for the TinyBit-wasm
  zero-copy typed-array views into engine memory).
- **No existing public symbol removed or changed.** `note_to_frequency()` is
  retained by default; it is only excluded when the caller opts in to
  `ABC_NO_FLOAT`.
- **New build-mode defines default to current behavior** (float on, print on).
- **Collision-safe.** All new functions are `abc_`-prefixed and all new macros
  `ABC_`-prefixed, minimizing redefinition clashes with downstream helpers.

The only way to introduce a break would be a deliberate future choice to delete
`note_to_frequency` or flip the float/print defaults — both avoidable. Ships as a
**minor version bump**.

---

## Suggested implementation order

1. **First slice (the three asks; all additive, integer-clean):**
   `abc_sheet_setup` (A), `abc_note_frequency_x10` + opt-in float (B),
   `ABC_FOR_EACH_NOTE` (C), `abc_note_is_rest` (D), `abc_ticks_to_samples` (E).
   Each deletes code that already exists in `audio.c`.
2. **Ergonomics:** `ABC_DEFINE_SHEET`, `abc_storage_bytes`, `abc_sheet_voice`,
   `abc_note_duration_ms`, `abc_strerror`, version + `extern "C"`.
3. **Larger items (own design/impl pass):** `AbcNoteIter` (C),
   `abc_count_notes` (F), `ABC_NO_FLOAT` / `ABC_NO_PRINT` build modes (G).

## Downstream follow-up (TinyBit-wasm)

After upstreaming, `audio.c` can drop `get_frequency_from_chord`,
`is_rest_note`, `duration_ticks_to_samples`, and the `init_channel` pointer-array
math in favor of the library helpers. That is a separate change in the consuming
repo and is not required for this proposal to land.
