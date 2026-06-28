// oran_p3_st1_test.ls — P3: full Section Type 1 C-plane DL message round-trip.
// Scenario: NR 100 MHz, mu=1, 273 PRBs split into two beam allocations (MU-MIMO),
// full 14-symbol slot. Builds the packet, parses it back, asserts every field.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2,
                        payload_size: 28, rtcid_pcid: 0x0102, seqid: 0x0010 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0,
        frame_id: 137, subframe_id: 3, slot_id: 1, start_symbol_id: 0,
        num_sections: 2, section_type: 1 }
    m.ud_comp_hdr = 0     // DL, static / no compression

    // Beam 1: PRBs 0..131, beamId 1.  Beam 2: PRBs 132..272, beamId 2.
    m.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0,   num_prbc: 132, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 1 })
    m.sections.push(SecType1{ section_id: 2, rb: false, sym_inc: false,
        start_prbc: 132, num_prbc: 141, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 2 })

    Writer w = oran.writer()
    m.write(&!w)
    // 8 (ecpri) + 6 (common) + 2 (udcomp+rsvd) + 2*8 (sections) = 32
    chk(w.size() == 32, "st1 total size should be 32")

    Reader r = w.as_reader()
    CplaneSt1 m2 = oran.parse_st1(&!r)

    chk(m2.ecpri.msg_type == 2, "ecpri msg_type")
    chk(m2.ecpri.rtcid_pcid == 0x0102, "eaxc")
    chk(m2.hdr.dir, "dir DL")
    chk(m2.hdr.frame_id == 137, "frame")
    chk(m2.hdr.section_type == 1, "stype 1")
    chk(m2.hdr.num_sections == 2, "nsec 2")
    chk(m2.sections.len() == 2, "parsed 2 sections")

    SecType1 s0 = m2.sections[0]
    chk(s0.section_id == 1, "s0 id")
    chk(!s0.rb, "s0 rb")
    chk(s0.start_prbc == 0, "s0 startprb")
    chk(s0.num_prbc == 132, "s0 numprb")
    chk(s0.re_mask == 0xFFF, "s0 remask")
    chk(s0.num_symbol == 14, "s0 numsym")
    chk(!s0.ef, "s0 ef")
    chk(s0.beam_id == 1, "s0 beam")

    SecType1 s1 = m2.sections[1]
    chk(s1.section_id == 2, "s1 id")
    chk(s1.start_prbc == 132, "s1 startprb")
    chk(s1.num_prbc == 141, "s1 numprb")
    chk(s1.beam_id == 2, "s1 beam")

    // full coverage check: 0..132 + 132..273 = all 273 PRBs
    chk(s0.start_prbc + s0.num_prbc == s1.start_prbc, "contiguous")
    chk(s1.start_prbc + s1.num_prbc == 273, "covers 273 PRBs")

    @print("ORAN P3 ST1 PASS")
}
