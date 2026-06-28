// oran_p1_test.ls — P1: eCPRI header + C-plane common header round-trip.
// Scenario: NR 100 MHz, mu=1 (30 kHz), C-plane Section Type 1 DL message.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // ---- eCPRI header round-trip (C-plane: msg_type 0x02) ----
    EcpriHdr e = EcpriHdr{ version: 1, concat: false, msg_type: 2,
                           payload_size: 32, rtcid_pcid: 0x0102, seqid: 0x0304 }
    Writer w1 = oran.writer()
    e.write(&!w1)
    chk(w1.size() == 8, "ecpri size should be 8")
    Reader r1 = w1.as_reader()
    EcpriHdr e2 = oran.parse_ecpri(&!r1)
    chk(e2.version == 1, "ecpri version")
    chk(!e2.concat, "ecpri concat")
    chk(e2.msg_type == 2, "ecpri msg_type")
    chk(e2.payload_size == 32, "ecpri payload")
    chk(e2.rtcid_pcid == 0x0102, "ecpri rtcid")
    chk(e2.seqid == 0x0304, "ecpri seqid")

    // ---- C-plane common header round-trip ----
    // DL (dir=1), payloadVersion=1, filterIndex=0, frame 137, subframe 3,
    // slot 1 (mu=1 -> 2 slots/subframe), startSymbol 0, 1 section, ST=1.
    CpCommonHdr h = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0,
        frame_id: 137, subframe_id: 3, slot_id: 1, start_symbol_id: 0,
        num_sections: 1, section_type: 1 }
    Writer w2 = oran.writer()
    h.write(&!w2)
    chk(w2.size() == 6, "cp common size should be 6")
    Reader r2 = w2.as_reader()
    CpCommonHdr h2 = oran.parse_cp_common(&!r2)
    chk(h2.dir, "cp dir")
    chk(h2.payload_ver == 1, "cp pver")
    chk(h2.filter_idx == 0, "cp fidx")
    chk(h2.frame_id == 137, "cp frame")
    chk(h2.subframe_id == 3, "cp subframe")
    chk(h2.slot_id == 1, "cp slot")
    chk(h2.start_symbol_id == 0, "cp startsym")
    chk(h2.num_sections == 1, "cp nsec")
    chk(h2.section_type == 1, "cp stype")

    @print("ORAN P1 PASS")
}
