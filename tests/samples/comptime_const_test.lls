// Compile-time constant evaluation (docs/plan_comptime_consteval.md), Steps 2-4.
//   Step 2: scalar consts folded to literals.
//   Step 3: comptime { ... return v } block evaluation (loops/if/locals).
//   Step 4: array consts materialized into constant-initialized globals (.rodata).
// No heap -> memcheck clean. JIT and AOT must agree byte-for-byte.
import std.core.math as math

// --- Step 2: scalars ---
comptime int  BITS   = 9
comptime int  MASK   = (1 << BITS) - 1        // 511
comptime bool BIG    = 1000 > 999             // true
comptime f64  INV2   = 1.0 / math.sqrt(2.0)   // 0.707107
comptime char LETTER = 'Q'                    // 81

// --- Step 3: block-form scalar ---
comptime int SUM = comptime {
    int s = 0
    for i in 0..10 { s = s + i }              // 45
    return s
}
comptime int FACT5 = comptime {
    int f = 1
    for i in 1..6 { f = f * i }               // 120
    return f
}

// --- Step 4: array lookup tables ---
comptime array(int, 10) SQ = comptime {
    array(int, 10) t = {}
    for i in 0..10 { t[i] = i * i }
    return t
}
comptime array(f64, 8) SIN8 = comptime {
    array(f64, 8) t = {}
    for i in 0..8 { t[i] = math.sin(i as f64 * 2.0 * math.PI / 8.0) }
    return t
}
comptime array(i64, 256) CRC8 = comptime {
    array(i64, 256) t = {}
    for b in 0..256 {
        i64 crc = b as i64
        for k in 0..8 {
            if (crc & 1) != 0 { crc = (crc >> 1) ^ 0xEDB88320 }
            else             { crc = crc >> 1 }
        }
        t[b] = crc
    }
    return t
}

def main() {
    @print(f"MASK={MASK}")
    @print(f"BIG={BIG}")
    @print(f"LCODE={LETTER as int}")
    @print(f"SUM={SUM}")
    @print(f"FACT5={FACT5}")
    @print(f"SQ={SQ[5]},{SQ[9]}")               // 25,81
    @print(f"CRC1={CRC8[1]}")                    // 1996959894
    @print(INV2)
    @print(SIN8[2])                              // 1.0 = sin(pi/2)
    @print("COMPTIME CONST DONE")
}
