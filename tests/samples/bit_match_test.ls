// bit_match_test.ls — V1 bit-pattern matching, JIT+AOT+memcheck.
//   * basic MSB-first field extraction + binding (int / u16 / u8 subjects)
//   * 1-bit field -> bool
//   * match-value arms (bits[4:0x1]...) + fall-through to a generic arm
//   * OR-pattern (bits[..] | bits[..])
//   * wildcard `_` field skip
//   * 64-bit subject with a >32-bit field (i64 binder)
// Prints "BITMATCH PASS" on success; "FAIL ..." (+ abort) on any mismatch.
import std.sys.c as c

def fail(Str msg) {
    @print(msg)
    c.abort()
}

def main() {
    // ---- 1. basic 32-bit extraction (eCPRI-ish common header) ----
    // version=1, rsv=0, concat=0, msg_type=0x14(20), payload=256
    int raw = 0x10140100
    match raw {
        bits[4:version][3:rsv][1:concat][8:msg_type][16:payload] => {
            if (version != 1)   { fail("FAIL version") }
            if (rsv != 0)       { fail("FAIL rsv") }
            if (concat)         { fail("FAIL concat") }   // 1-bit field -> bool
            if (msg_type != 20) { fail("FAIL msg_type") }
            if (payload != 256) { fail("FAIL payload") }
        }
        _ => { fail("FAIL basic nomatch") }
    }

    // ---- 2. u16 subject: split into two bytes ----
    u16 hw = 0xABCD as u16
    match hw {
        bits[8:high][8:low] => {
            if (high != 171) { fail("FAIL u16 high") }   // 0xAB
            if (low != 205)  { fail("FAIL u16 low") }     // 0xCD
        }
        _ => { fail("FAIL u16 nomatch") }
    }

    // ---- 3. u8 subject: three sub-fields ----
    u8 byte = 0xC3 as u8     // 1100_0011
    match byte {
        bits[2:a][3:b][3:cc] => {
            if (a != 3)  { fail("FAIL u8 a") }   // 11
            if (b != 0)  { fail("FAIL u8 b") }   // 000
            if (cc != 3) { fail("FAIL u8 c") }   // 011
        }
        _ => { fail("FAIL u8 nomatch") }
    }

    // ---- 4. match-value arms + fall-through to a generic binding arm ----
    match raw {
        bits[4:0x2][28:_] => { fail("FAIL should not match version 2") }
        bits[4:0x1][3:_][1:_][8:0x14][16:size] => {
            if (size != 256) { fail("FAIL matchval size") }
        }
        _ => { fail("FAIL matchval fell through") }
    }

    // ---- 5. OR-pattern on a u8 high nibble ----
    u8 hi_a = 0xA5 as u8
    match hi_a {
        bits[4:0xA][4:_] | bits[4:0x5][4:_] => { }
        _ => { fail("FAIL or 0xA") }
    }
    u8 hi_b = 0x53 as u8
    match hi_b {
        bits[4:0xA][4:_] | bits[4:0x5][4:_] => { }
        _ => { fail("FAIL or 0x5") }
    }
    u8 hi_c = 0x73 as u8
    match hi_c {
        bits[4:0xA][4:_] | bits[4:0x5][4:_] => { fail("FAIL or 0x7 matched") }
        _ => { }
    }

    // ---- 6. wildcard field skip: only bind the tail ----
    match raw {
        bits[4:_][28:rest] => {
            // rest = low 28 bits of 0x10140100 = 0x0140100
            if (rest != 0x0140100) { fail("FAIL wildcard rest") }
        }
        _ => { fail("FAIL wildcard nomatch") }
    }

    // ---- 7. 1-bit bool field set ----
    int flags = 0x80000000 as int
    match flags {
        bits[1:top][31:_] => {
            if (!top) { fail("FAIL top bit") }
        }
        _ => { fail("FAIL bool nomatch") }
    }

    // ---- 8. 64-bit subject, >32-bit field (i64 binder). bit63 set — exercises
    //         the full-u64 literal parse (strtoull, not saturating strtoll). ----
    u64 big = 0xAABBCCDDEE112233 as u64
    match big {
        bits[40:wide][24:tail] => {
            i64 want_wide = 0xAABBCCDDEE as i64
            i64 want_tail = 0x112233 as i64
            if (wide != want_wide) { fail("FAIL u64 wide") }
            if (tail != want_tail) { fail("FAIL u64 tail") }
        }
        _ => { fail("FAIL u64 nomatch") }
    }

    @print("BITMATCH PASS")
}
