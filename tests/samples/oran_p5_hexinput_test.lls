// oran_p5_hexinput_test.ls — input adapters: parse the SAME ST1 packet from a
// continuous hex string and from a tcpdump -X style hexdump.
//
// Packet (24 bytes): eCPRI(8) + common(6) + udComp/rsvd(2) + ST1 section(8).
//   eCPRI: ver1 msg2 payload0x14 eAxC0x0102 seq1
//   common: DL pver1 fidx0 frame50 sf0 slot0 sym0 nsec1 ST1
//   section: sid1 startPrbc0 numPrbc100 reMask0xFFF numSym14 ef0 beam1
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def verify(CplaneSt1 m) {
    chk(m.ecpri.version == 1, "ver")
    chk(m.ecpri.msg_type == 2, "msg")
    chk(m.ecpri.rtcid_pcid == 0x0102, "eaxc")
    chk(m.ecpri.seqid == 1, "seq")
    chk(m.hdr.dir, "DL")
    chk(m.hdr.frame_id == 50, "frame")
    chk(m.hdr.section_type == 1, "ST1")
    chk(m.sections.len() == 1, "1 sec")
    SecType1 s = m.sections[0]
    chk(s.section_id == 1, "sid")
    chk(s.start_prbc == 0, "startprb")
    chk(s.num_prbc == 100, "numprb")
    chk(s.re_mask == 0xFFF, "remask")
    chk(s.num_symbol == 14, "numsym")
    chk(s.beam_id == 1, "beam")
}

def main() {
    // ---- continuous hex (whitespace/colons ignored) ----
    Str hex = "1002 0014 0102 0001 9032 0000 0101 0000 0010 0064 FFFE 0001"
    Reader r1 = oran.from_hex(hex)
    CplaneSt1 m1 = oran.parse_st1(&!r1)
    verify(m1)

    // ---- tcpdump -X style hexdump (offset column + ASCII column) ----
    Str dump = "0x0000:  1002 0014 0102 0001 9032 0000 0101 0000  ........\n0x0010:  0010 0064 fffe 0001                       ........"
    Reader r2 = oran.from_hexdump(dump)
    CplaneSt1 m2 = oran.parse_st1(&!r2)
    verify(m2)

    @print("ORAN P5 HEXINPUT PASS")
}
