// oran_modcomp_test.ls — DL modulation compression (SE4 + Annex A.5).
//  * modCompScaler 15-bit field <-> scale factor
//  * SE4 build -> C-plane ST1 round-trip -> extract csf + modCompScaler
//    (also checks the ef/extType bit order: extType must read back as 4)
//  * U-plane mod-compr (udCompMeth=4) IQ round-trip + decompression math
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def chkf(f64 got, f64 want, Str msg) {
    f64 d = got - want
    if d < 0.0 { d = 0.0 - d }
    if d > 0.0001 { @print(msg); @print(got); c.abort() }
}

def main() {
    // ---- modCompScaler: exponent=2, mantissa=1024 -> 1024 / 2^13 = 0.125 ----
    int field = oran.make_mod_comp_scaler(2, 1024)
    chkf(oran.mod_comp_scaler(field), 0.125, "scaler should be 0.125")

    // ---- SE4 carried in a C-plane ST1 section, round-trip, extract ----
    SecType1 s = {}
    s.section_id = 3  s.rb = false  s.sym_inc = false
    s.start_prbc = 0  s.num_prbc = 50  s.re_mask = 0xFFF  s.num_symbol = 14  s.beam_id = 2
    s.ef = true
    s.exts.push(oran.make_se4(true, 2, 1024))     // csf=1, scaler=0.125

    Writer w = oran.writer()
    s.write(&!w)
    Reader r = w.as_reader()
    SecType1 s2 = oran.parse_st1_section(&!r)
    chk(s2.exts.len() == 1, "1 SE parsed")
    chk(s2.exts[0].ext_type == 4, "extType reads back as 4 (ef/extType bit order)")
    chk(!s2.exts[0].ef, "SE4 is last (ef=0)")

    ModComp mc = oran.find_mod_comp(&s2.exts)
    chk(mc.present, "SE4 modulation-compression params found")
    chk(mc.csf, "csf = true")
    chkf(mc.scaler, 0.125, "extracted modCompScaler = 0.125")

    // ---- U-plane mod-compr (udCompMeth=4) IQ round-trip + decompress ----
    UplaneMsg m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: 0x0107, seqid: 1 }
    m.dir = true  m.payload_ver = 1  m.filter_idx = 0
    m.frame_id = 1  m.subframe_id = 0  m.slot_id = 0  m.symbol_id = 0
    m.static_comp = false  m.iq_width = 4  m.comp_meth = 4    // modulation compression, 4-bit
    UplaneSection us = {}
    us.section_id = 3  us.start_prbu = 0  us.num_prbu = 1
    UplanePrb prb = {}
    for re in 0..12 { prb.i_samples.push(2)  prb.q_samples.push(0 - 4) }
    us.prbs.push(prb)
    m.sections.push(us)

    Writer w2 = oran.writer()
    m.write(&!w2)
    Reader r2 = w2.as_reader()
    UplaneMsg m2 = oran.parse_uplane(&!r2, oran.ctx(0, 0))
    chk(m2.sections.len() == 1, "1 uplane section")
    UplaneSection us2 = m2.sections[0]
    chk(us2.comp_meth == 4, "comp_meth = 4 (mod-compr, no udCompParam)")
    chk(us2.iq_width == 4, "iqWidth = 4")
    UplanePrb p2 = us2.prbs[0]
    chk(p2.i_samples[0] == 2, "raw I round-trip")
    chk(p2.q_samples[0] == (0 - 4), "raw Q round-trip")

    // decompress with the SE4 params: frac=sample/2^3; csf adds 2^-4; * 0.125
    //   I=2: (2/8 + 1/16) * 0.125 = (0.25 + 0.0625)*0.125 = 0.0390625
    //   Q=-4: (-0.5 + 0.0625)*0.125 = -0.4375*0.125 = -0.0546875
    chkf(oran.mod_decompress(2, 4, mc.csf, mc.scaler), 0.0390625, "decompress I")
    chkf(oran.mod_decompress(0 - 4, 4, mc.csf, mc.scaler), 0.0 - 0.0546875, "decompress Q")

    // without constellation shift (csf=0): I=2 -> 0.25*0.125 = 0.03125
    chkf(oran.mod_decompress(2, 4, false, 0.125), 0.03125, "decompress I no-csf")

    @print("ORAN MODCOMP PASS")
}
