// std.sim.regview — register-level data-movement text view (plan §7.6).
//
// Answers "what did this instruction move where in the register" in plain
// monospace text (NOT an HTML widget — see memory feedback_viz_text_over_widget):
//   * bit-level   : per-bit 0/1 before->after, field separators, caret rows
//                   (shifts / funnels / mask / pack — the bit-stagger case)
//   * byte-level  : 2-hex per byte, B(n-1)..B0, change-highlight before->after
//   * symbol/lane : byte identities A..P tracked through a shuffle (vpshufb/vpermb)
//
// Intel convention: B63 .. B0, left = MSB, right = LSB.
// Pure LS, zero compiler changes.

import std.core.vec
import std.core.str

def _digit(int d) -> Str {
    if d == 0 { return "0" }
    if d == 1 { return "1" }
    if d == 2 { return "2" }
    if d == 3 { return "3" }
    if d == 4 { return "4" }
    if d == 5 { return "5" }
    if d == 6 { return "6" }
    if d == 7 { return "7" }
    if d == 8 { return "8" }
    if d == 9 { return "9" }
    if d == 10 { return "A" }
    if d == 11 { return "B" }
    if d == 12 { return "C" }
    if d == 13 { return "D" }
    if d == 14 { return "E" }
    return "F"
}

// uppercase 2-digit hex of a byte 0..255
def hex2(int n) -> Str {
    int v = n & 255
    Str hi = _digit((v >> 4) & 15)
    Str lo = _digit(v & 15)
    return f"{hi}{lo}"
}

// byte position identity symbol: 0->A, 1->B, ... 15->P, then wraps with '#'
def byte_symbol(int i) -> Str {
    if i < 0 { return "." }
    if i > 25 { return "#" }
    Str s = ""           // 'A' = 65
    s.push_byte(65 + i)
    return s
}

// ============================================================================
// bit-level (the centerpiece: shifts / funnels / pack)
// ============================================================================

// MSB-first bit row of the low `nbits` of `value`, space every `group` bits.
def bit_row(Str label, i64 value, int nbits, int group) -> Str {
    Str s = label.pad_right(11, 32)
    for k in 0..nbits {
        int b = nbits - 1 - k
        i64 shifted = value >> (b as i64)
        int bit = (shifted & (1 as i64)) as int
        if bit == 1 { s = f"{s}1" } else { s = f"{s}0" }
        if b > 0 {
            int rem = b % group
            if rem == 0 { s = f"{s} " }
        }
    }
    return s
}

// caret row aligned under the lowest `field_w` bits of a matching bit_row.
def caret_low(int nbits, int group, int field_w, Str note) -> Str {
    Str s = "".pad_right(11, 32)
    for k in 0..nbits {
        int b = nbits - 1 - k
        if b < field_w { s = f"{s}^" } else { s = f"{s} " }
        if b > 0 {
            int rem = b % group
            if rem == 0 { s = f"{s} " }
        }
    }
    return f"{s}  <- {note}"
}

// caret row marking which of the low `nbits` flipped between before/after (XOR).
// Aligned under a matching bit_row so "what the instruction changed" is instant.
def bit_change_row(i64 before, i64 after, int nbits, int group) -> Str {
    i64 diff = before ^ after
    Str s = "  changed".pad_right(11, 32)
    for k in 0..nbits {
        int b = nbits - 1 - k
        i64 shifted = diff >> (b as i64)
        int bit = (shifted & (1 as i64)) as int
        if bit == 1 { s = f"{s}^" } else { s = f"{s} " }
        if b > 0 {
            int rem = b % group
            if rem == 0 { s = f"{s} " }
        }
    }
    return s
}

// bracket row marking a field occupying bits [lo .. lo+w-1] of the low `nbits`,
// aligned under a bit_row. Use it to show a 9-bit field's slot before AND after a
// shift so the movement is visible (e.g. bits 8..0 -> 13..5 for a <<5 stagger).
def bit_field_bracket(int nbits, int group, int lo, int w, Str note) -> Str {
    int hi = lo + w - 1
    Str s = "".pad_right(11, 32)
    for k in 0..nbits {
        int b = nbits - 1 - k
        Str c = " "
        if w == 1 {
            if b == lo { c = "|" }
        } else {
            if b == hi { c = "[" }
            if b == lo { c = "]" }
            if b < hi { if b > lo { c = "=" } }
        }
        s = f"{s}{c}"
        if b > 0 {
            int rem = b % group
            if rem == 0 { s = f"{s} " }
        }
    }
    return f"{s}  <- {note}"
}

// before/after bit transform. With change-row carets so the flipped bits are obvious.
def bit_xform(Str title, i64 before, i64 after, int nbits, int group) -> Str {
    Str h = title
    Str b = bit_row(" before", before, nbits, group)
    Str a = bit_row(" after", after, nbits, group)
    Str c = bit_change_row(before, after, nbits, group)
    return f"{h}\n{b}\n{a}\n{c}\n"
}

// ============================================================================
// byte-level (shuffles / packs at byte granularity)
// ============================================================================

// index header  "B<n-1> .. B0" aligned with byte_row
def byte_header(int n) -> Str {
    Str s = "".pad_right(11, 32)
    for k in 0..n {
        int idx = n - 1 - k
        Str lab = f"B{idx}"
        s = f"{s}{lab.pad_left(3, 32)} "
    }
    return s
}

// 2-hex per byte, highest index first
def byte_row(Str label, &Vec(int) bytes) -> Str {
    Str s = label.pad_right(11, 32)
    int n = bytes.len()
    for k in 0..n {
        int idx = n - 1 - k
        Str h = hex2(bytes.get!(idx))
        s = f"{s} {h} "
    }
    return s
}

// symbol row (A..P identities) highest index first — for tracking shuffles
def symbol_row(Str label, &Vec(int) ids) -> Str {
    Str s = label.pad_right(11, 32)
    int n = ids.len()
    for k in 0..n {
        int idx = n - 1 - k
        Str sym = byte_symbol(ids.get!(idx))
        s = f"{s}  {sym} "
    }
    return s
}

// "from" row: for each dst byte (high..low index), the SOURCE index it pulled, or
// '..' for a zero/dead slot. Makes dst[i] <- src[perm[i]] explicit as numbers,
// complementing the A..P identity row (handy when src spans >26 bytes / >1 lane).
def from_row(Str label, &Vec(int) perm) -> Str {
    Str s = label.pad_right(11, 32)
    int n = perm.len()
    for k in 0..n {
        int idx = n - 1 - k
        int src = perm.get!(idx)
        Str cell = ".."
        if src >= 0 { cell = hex2(src) }
        s = f"{s}{cell.pad_left(3, 32)} "
    }
    return s
}

// byte shuffle view: dst[i] = src identity `perm[i]` (perm given low..high index).
// renders src identities then dst identities so the movement is visible.
def shuffle_view(Str title, &Vec(int) perm) -> Str {
    int n = perm.len()
    Vec(int) src = {}
    for i in 0..n { src.push(i) }
    Str hdr = byte_header(n)
    Str srow = symbol_row(" src", &src)
    Str drow = symbol_row(" dst", perm)
    return f"{title}\n{hdr}\n{srow}\n{drow}\n"
}

// shuffle view WITH an explicit numeric source-index ("dst<-src") row under the
// identities — the clearer form for per-instruction provenance.
def shuffle_view_full(Str title, &Vec(int) perm) -> Str {
    int n = perm.len()
    Vec(int) src = {}
    for i in 0..n { src.push(i) }
    Str hdr = byte_header(n)
    Str srow = symbol_row(" src", &src)
    Str drow = symbol_row(" dst", perm)
    Str frow = from_row(" dst<-src", perm)
    return f"{title}\n{hdr}\n{srow}\n{drow}\n{frow}\n"
}

// one-line key for the register views.
def legend() -> Str {
    return "  legend:  A..P = source byte identity   .. = zero/dead slot   ^ = changed bit   [== ==] = field extent"
}

// ============================================================================
// multi-lane view (the CROSS-LANE case §7.6: vpermb/vpermd/valignr need all 64
// bytes, not one collapsed lane). Renders a 512-bit register as 4 stacked lanes
// of 16 bytes each, high lane on top, and within a lane B15..B0 (low byte = right).
// `live[i]` = number of live (low) bytes in lane i; '#' live, '.' dead.
// ============================================================================
def lane_map(Str label, &Vec(int) live) -> Str {
    int nl = live.len()
    Str hdr = "  "             // byte-range header, one 18-wide field per lane
    Str row = "  "             // bracketed lanes, all on ONE line (high lane left)
    int li = nl - 1
    while li >= 0 {
        int lv = live.get!(li)
        int hi = li * 16 + 15
        int lo = li * 16
        Str rng = f"lane{li} B{hi}..{lo}"
        hdr = f"{hdr}{rng.pad_right(18, 32)}"
        Str cell = "["
        for k in 0..16 {
            int b = 15 - k         // within lane: high byte left, low byte right
            if b < lv { cell = f"{cell}#" } else { cell = f"{cell}." }
        }
        cell = f"{cell}]"
        row = f"{row}{cell}"
        li = li - 1
    }
    return f"{label}\n{hdr}\n{row}"
}

// one gather edge: "dst B{a}..B{b} <- src B{c}..B{d}   note".  Used to spell out a
// cross-lane permute's compaction (which source bytes each dst run pulls).
def gather_edge(int dlo, int dhi, int slo, int shi, Str note) -> Str {
    Str d = f"dst B{dlo}..B{dhi}".pad_right(14, 32)
    Str s = f"<- src B{slo}..B{shi}".pad_right(16, 32)
    return f"    {d} {s} {note}"
}

// horizontal lane map driven by a byte-id vector (id >= 0 = live, < 0 = dead).
// WIDTH-GENERIC: the number of 128-bit lanes is derived from the vector length
// (16 bytes=1 lane=128b, 32=256b, 64=512b). Unlike lane_map (per-lane live COUNTS,
// assumes low N live), this renders ACTUAL per-byte liveness, correct for scattered
// results. High lane left; the auto data-movement layer feeds it.
def lane_map_ids(Str label, &Vec(int) ids) -> Str {
    int n = ids.len()
    int nlanes = n / 16
    if nlanes < 1 { nlanes = 1 }
    Str hdr = "  "
    Str row = "  "
    int li = nlanes - 1
    while li >= 0 {
        int hi = li * 16 + 15
        int lo = li * 16
        Str rng = f"lane{li} B{hi}..{lo}"
        hdr = f"{hdr}{rng.pad_right(18, 32)}"
        Str cell = "["
        for k in 0..16 {
            int b = 15 - k
            int idx = li * 16 + b
            int v = -1
            if idx < n { v = ids.get!(idx) }
            if v >= 0 { cell = f"{cell}#" } else { cell = f"{cell}." }
        }
        cell = f"{cell}]"
        row = f"{row}{cell}"
        li = li - 1
    }
    return f"{label}\n{hdr}\n{row}"
}
