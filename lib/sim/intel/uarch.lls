// sim.intel.uarch — x86 microarchitecture parameter tables (plan §5.2).
//
// MVP carries hand-entered P-core parameters (Ice Lake / Skylake-X / Rocket Lake);
// the real backend (V1+) sources port/latency data from the LLVM TableGen schedule
// model (Apache-2.0). The Uarch struct is the stable interface — engine reads
// num_ports/fe_width, the advisor reads avx512_downclock for μarch-conditional
// warnings (§6.5: AVX-512 downclock is heavy on Skylake-X, light on Ice Lake,
// gone on Rocket Lake+).
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir

// avx512_downclock severity: 0 none / 1 light / 2 heavy
def dc_none()  -> int { return 0 }
def dc_light() -> int { return 1 }
def dc_heavy() -> int { return 2 }

struct Uarch {
    Str name
    int num_ports        // execution ports (engine ResMII denominator per port)
    int fe_width         // rename/allocation width (uops/cycle to the backend)
    int rob_size         // reorder-buffer entries (ROB-bound model, future)
    int dsb_width        // uop-cache delivery width (frontend model, future §5.2)
    int mite_width       // legacy-decode width (frontend model, future §5.2)
    int avx512_downclock // dc_* — heavy 512b load lowering core frequency
}

// --- P-core parameter tables (hand-seeded; facts from Intel optimization manual) ---

def icelake() -> Uarch {
    // Sunny Cove: 5-wide rename, 10 ports total (8 modelled here), ROB 352.
    return Uarch { name: "Ice Lake", num_ports: 8, fe_width: 5, rob_size: 352,
                   dsb_width: 6, mite_width: 5, avx512_downclock: dc_light() }
}

def skylake_x() -> Uarch {
    // Skylake-X / Cascade Lake: heavy AVX-512 downclock, smaller ROB 224.
    return Uarch { name: "Skylake-X", num_ports: 8, fe_width: 5, rob_size: 224,
                   dsb_width: 6, mite_width: 5, avx512_downclock: dc_heavy() }
}

def rocket_lake() -> Uarch {
    // Cypress Cove: backport of Sunny Cove to 14nm; AVX-512 downclock effectively gone.
    return Uarch { name: "Rocket Lake", num_ports: 8, fe_width: 5, rob_size: 352,
                   dsb_width: 6, mite_width: 5, avx512_downclock: dc_none() }
}

def downclock_name(int d) -> Str {
    if d == 2 { return "heavy" }
    if d == 1 { return "light" }
    return "none"
}

// The P-core port layout shared by the tables above (they differ in width/ROB/
// downclock, not in port topology). Labels match the ports.ls seed masks.
def port_set(&Uarch u) -> Vec(ir.Port) {
    Vec(ir.Port) v = {}
    v.push(ir.port(0, "p0 ALU/shift/vec"))
    v.push(ir.port(1, "p1 ALU/FMA/vec"))
    v.push(ir.port(2, "p2 load"))
    v.push(ir.port(3, "p3 load"))
    v.push(ir.port(4, "p4 store-data"))
    v.push(ir.port(5, "p5 vec-shuffle"))
    v.push(ir.port(6, "p6 ALU/branch"))
    v.push(ir.port(7, "p7 store-AGU"))
    return v
}

// one-line summary for reports
def summary(&Uarch u) -> Str {
    Str dc = downclock_name(u.avx512_downclock)
    return f"{u.name}: {u.num_ports} ports, {u.fe_width}-wide rename, ROB {u.rob_size}, AVX512-downclock={dc}"
}
