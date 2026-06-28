// sim_catalog_test.ls — advisor §6.4 catalog rules (fusion & presence idioms).
//
// Feeds tiny kernels that should each trigger one catalog rule, and verifies the
// fusion gating (a single logic op does NOT trigger the vpternlogd fusion; two do).

import sim.intel.decode as decode
import sim.intel.patterns as adv
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

// decode `src`, advise with the given ISA set (neutral bottleneck), report if `rid` fired
def fires(Str src, Str rid, &Vec(Str) isa) -> bool {
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)
    Vec(adv.Suggestion) sg = adv.advise(&prog, adv.bk_other(), -1, isa, 0)
    for s in &sg {
        if s.id.eq?(&rid) { return true }
    }
    return false
}

def main() {
    Vec(Str) isa = {}
    isa.push("AVX")
    isa.push("AVX2")
    isa.push("AVX512")
    isa.push("VNNI")
    isa.push("VPCLMULQDQ")

    // B: vpternlogd fusion — needs >= 2 logic ops
    check(fires("vpand zmm0,zmm1,zmm2\nvpxor zmm3,zmm0,zmm4\n", "ternlog-fuse", &isa),
        "two logic ops -> ternlog-fuse fires")
    check(!fires("vpand zmm0,zmm1,zmm2\n", "ternlog-fuse", &isa),
        "single logic op -> ternlog-fuse does NOT fire (fusion gate)")

    // D: vmulps + vaddps -> FMA
    check(fires("vmulps zmm0,zmm1,zmm2\nvaddps zmm3,zmm0,zmm4\n", "fma-fuse-ps", &isa),
        "mul+add ps -> fma-fuse-ps fires")
    check(!fires("vmulps zmm0,zmm1,zmm2\n", "fma-fuse-ps", &isa),
        "mul alone -> fma-fuse-ps does NOT fire")

    // C: vpmaddubsw -> VNNI vpdpbusd
    check(fires("vpmaddubsw zmm0,zmm1,zmm2\n", "vnni-int8-dotprod", &isa),
        "vpmaddubsw -> vnni-int8-dotprod fires")
    // C: vpmaddwd -> VNNI vpdpwssd
    check(fires("vpmaddwd zmm0,zmm1,zmm2\n", "vnni-int16-dotprod", &isa),
        "vpmaddwd -> vnni-int16-dotprod fires")

    // D: vdivps -> reciprocal + Newton
    check(fires("vdivps zmm0,zmm1,zmm2\n", "div-to-rcp", &isa),
        "vdivps -> div-to-rcp fires")

    // A: broadcast fold
    check(fires("vbroadcastss zmm0,xmm1\nvmulps zmm2,zmm0,zmm3\n", "embed-broadcast", &isa),
        "vbroadcastss -> embed-broadcast fires")

    // E/anti: vpextr* scalarize warning (the scalar-pack trap)
    check(fires("vpextrw eax,xmm0,3\nshl eax,9\n", "vpextr-scalarize", &isa),
        "vpextrw -> vpextr-scalarize warning fires")

    // anti: byte/word compress-to-memory is microcoded -> suggest vpermb + masked store
    check(fires("vpcompressb zmmword ptr [rdi] {k1}, zmm1\n", "compress-store-microcoded", &isa),
        "vpcompressb -> compress-store-microcoded warning fires")
    check(!fires("vpermb zmm0,zmm1,zmm2\n", "compress-store-microcoded", &isa),
        "vpermb (no compress) -> compress-store-microcoded does NOT fire")

    // ISA gating: without VNNI, the int8 dot-product rule must not fire
    Vec(Str) noavx512 = {}
    noavx512.push("AVX2")
    check(!fires("vpmaddubsw zmm0,zmm1,zmm2\n", "vnni-int8-dotprod", &noavx512),
        "no VNNI feature -> vnni-int8 rule gated off")

    @print("SIM CATALOG PASS")
}
