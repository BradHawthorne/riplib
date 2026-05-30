/*
 * rip_meganum.h — Base-36 ("MegaNum") parameter decoder.
 *
 * RIPscrip encodes numeric command parameters as fixed-width base-36
 * ASCII digits: '0'..'9' then 'A'..'Z' (case-insensitive in this
 * implementation; lowercase a-z is accepted as the equivalent
 * uppercase value).  See `docs/spec/01-wire-format.md` §1.5 for the
 * full encoding spec.
 *
 * Decoders are header-only `static inline` because:
 *   - They're called in tight loops by the parser FSM (one per
 *     parameter field of every command).
 *   - The implementation is small enough that inlining costs less
 *     than the call overhead.
 *   - No external symbols means no extra archive object and no
 *     CMake / CI archive-verification list to update.
 *
 * Extracted from src/ripscrip.c as step 3 of audit candidate C-002
 * (decompose ripscrip.c monolith).  Used by src/ripscrip.c and
 * src/ripscrip2.c.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#pragma once

#include <stdint.h>

/* Decode a single base-36 ASCII digit.  Returns 0 for any character
 * outside the documented range — silent-fallback semantics matching
 * the original DLL behaviour. */
static inline int rip_mega_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    return 0;
}

/* 2-digit decode (0..1295) — most common; coordinates, colours,
 * widths, flags. */
static inline int16_t rip_mega2(const char *p) {
    return (int16_t)(rip_mega_digit(p[0]) * 36 + rip_mega_digit(p[1]));
}

/* 3-digit decode (0..46655) — extended IDs, large counts. */
static inline int32_t rip_mega3(const char *p) {
    return (int32_t)(rip_mega_digit(p[0]) * 1296 +
                     rip_mega_digit(p[1]) * 36 +
                     rip_mega_digit(p[2]));
}

/* 4-digit decode (0..1679615) — bit-fields, extension fields. */
static inline int32_t rip_mega4(const char *p) {
    return (int32_t)(rip_mega_digit(p[0]) * 46656 +
                     rip_mega_digit(p[1]) * 1296 +
                     rip_mega_digit(p[2]) * 36 +
                     rip_mega_digit(p[3]));
}

/* NOTE: this header exports ONLY the rip_*-prefixed names.  The historical
 * unprefixed aliases (mega_digit/mega2/mega3/mega4) are defined locally in
 * src/ripscrip.c — the one TU that uses them — so this shared header does
 * not leak common short macro names into every includer (C-016).  ripscrip2.c
 * uses its own decoders (mega1/mega2l) and does not include this header. */
