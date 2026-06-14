// std/bytes.ls — byte-buffer integer loads + a stateful Reader cursor.
//
// V2 companion to bit-pattern matching (features/bit_pattern_match.md §10):
// read fixed-width big/little-endian integers out of a byte buffer, then feed the
// result straight into a `match ... { bits[...] }`. The headline use case is
// binary protocol parsing (O-RAN / eCPRI fronthaul packets).
//
// Import:  import std.bytes as b
//
// Two data sources, one Reader type (holds a Str — Str's cap tri-state carries
// ownership: cap>0 owns + frees on drop, cap==0 borrows + never frees):
//   * b.of_bytes(file_str)   — OWN the bytes (move a Str in, e.g. io.read_file's
//                               result). Self-contained, auto-frees. Recommended
//                               for files.
//   * b.borrow(ptr, len)     — BORROW raw *u8+len (network/embedded). The caller
//                               owns the buffer and must keep it alive while the
//                               Reader is used (LS has no lifetime system).
//
// Endianness is a property of the LOAD, not of the integer value or of `match`:
// pick be_* for network byte order, le_* for host/little-endian data. Once read,
// the returned integer is MSB-first aligned, so `bits[...]` extraction is uniform.

import std.c as c
import std.str

// ---- free-function loads (thin wrappers over the runtime; no bounds check) ----

fn load_u8(*u8 buf, int off)    -> u8  { return c.__ls_load_u8(buf, off) as u8 }
fn load_be_u16(*u8 buf, int off) -> u16 { return c.__ls_load_be_u16(buf, off) as u16 }
fn load_be_u32(*u8 buf, int off) -> u32 { return c.__ls_load_be_u32(buf, off) as u32 }
fn load_be_u64(*u8 buf, int off) -> u64 { return c.__ls_load_be_u64(buf, off) }
fn load_le_u16(*u8 buf, int off) -> u16 { return c.__ls_load_le_u16(buf, off) as u16 }
fn load_le_u32(*u8 buf, int off) -> u32 { return c.__ls_load_le_u32(buf, off) as u32 }
fn load_le_u64(*u8 buf, int off) -> u64 { return c.__ls_load_le_u64(buf, off) }

// ---- Reader: a cursor over a byte buffer ----

struct Reader { Str buf; int pos }

// OWN: move a Str (e.g. io.read_file's result) in; Reader.__drop frees it (cap>0).
fn of_bytes(Str buf) -> Reader { return Reader{ buf: buf, pos: 0 } }

// BORROW: wrap an existing *u8 + len as a non-owning Str (cap==0 → drop won't free).
fn borrow(*u8 p, int len) -> Reader { return Reader{ buf: Str{ data: p, len: len, cap: 0 }, pos: 0 } }

impl Reader {
    fn pos(&self) -> int       { return self.pos }
    fn len(&self) -> int       { return self.buf.len() }
    fn remaining(&self) -> int { return self.buf.len() - self.pos }
    fn eof?(&self) -> bool     { return self.pos >= self.buf.len() }
    fn has?(&self, int n) -> bool { return self.pos + n <= self.buf.len() }

    fn skip(&!self, int n)   { self.pos = self.pos + n }   // advance over n bytes
    fn seek(&!self, int off) { self.pos = off }            // absolute reposition

    // Big-endian reads, advancing the cursor. Bounds-checked: reading past the
    // end prints a diagnostic and aborts (process exit 1, same as v[i] overrun).
    fn be_u8(&!self) -> u8 {
        if self.pos + 1 > self.buf.len() { print("Reader.be_u8: read past end of buffer"); c.abort() }
        u8 v = load_u8(self.buf.data, self.pos)
        self.pos = self.pos + 1
        return v
    }
    fn be_u16(&!self) -> u16 {
        if self.pos + 2 > self.buf.len() { print("Reader.be_u16: read past end of buffer"); c.abort() }
        u16 v = load_be_u16(self.buf.data, self.pos)
        self.pos = self.pos + 2
        return v
    }
    fn be_u32(&!self) -> u32 {
        if self.pos + 4 > self.buf.len() { print("Reader.be_u32: read past end of buffer"); c.abort() }
        u32 v = load_be_u32(self.buf.data, self.pos)
        self.pos = self.pos + 4
        return v
    }
    fn be_u64(&!self) -> u64 {
        if self.pos + 8 > self.buf.len() { print("Reader.be_u64: read past end of buffer"); c.abort() }
        u64 v = load_be_u64(self.buf.data, self.pos)
        self.pos = self.pos + 8
        return v
    }

    // Little-endian reads (u8 has no endianness — use be_u8).
    fn le_u16(&!self) -> u16 {
        if self.pos + 2 > self.buf.len() { print("Reader.le_u16: read past end of buffer"); c.abort() }
        u16 v = load_le_u16(self.buf.data, self.pos)
        self.pos = self.pos + 2
        return v
    }
    fn le_u32(&!self) -> u32 {
        if self.pos + 4 > self.buf.len() { print("Reader.le_u32: read past end of buffer"); c.abort() }
        u32 v = load_le_u32(self.buf.data, self.pos)
        self.pos = self.pos + 4
        return v
    }
    fn le_u64(&!self) -> u64 {
        if self.pos + 8 > self.buf.len() { print("Reader.le_u64: read past end of buffer"); c.abort() }
        u64 v = load_le_u64(self.buf.data, self.pos)
        self.pos = self.pos + 8
        return v
    }
}
