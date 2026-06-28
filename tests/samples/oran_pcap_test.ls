// oran_pcap_test.ls — read a classic .pcap: global header + 1 record + Ethernet
// frame carrying an eCPRI ST1 message. Also exercises auto-detect + read_input.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // ---- build an eCPRI ST1 frame ----
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    m.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 100, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 7 })
    Writer ew = oran.writer()
    m.write(&!ew)
    Str eframe = ew.take()
    int elen = eframe.len()

    // ---- wrap in a minimal big-endian pcap ----
    Writer w = oran.writer()
    w.byte(0xa1) w.byte(0xb2) w.byte(0xc3) w.byte(0xd4)   // magic (BE)
    w.be_u16(2) w.be_u16(4)                                // version 2.4
    w.be_u32(0) w.be_u32(0) w.be_u32(65535) w.be_u32(1)   // zone, sig, snaplen, linktype=Ethernet
    int incl = 14 + elen                                  // Ethernet(14) + eCPRI frame
    w.be_u32(0) w.be_u32(0) w.be_u32(incl) w.be_u32(incl) // ts_sec, ts_usec, incl_len, orig_len
    for i in 0..12 { w.byte(0) }                          // dst+src MAC
    w.be_u16(0xAEFE)                                      // eCPRI ethertype
    w.bytes(&eframe)
    Str pcap = w.take()

    // ---- from_pcap ----
    Vec(Packet) pkts = oran.from_pcap(&pcap, oran.profile_nr_100mhz_scs30())
    chk(pkts.len() == 1, "1 packet from pcap")
    chk(oran.section_type_of(pkts[0]) == 1, "ST1")
    chk(oran.eaxc_of(pkts[0]) == 0x0102, "eaxc 0x0102")
    chk(oran.is_dl?(pkts[0]), "DL")

    // ---- auto-detect: sniff -> Pcap, read_input(Auto) -> 1 packet ----
    match oran.sniff_format(&pcap) { Pcap => {} _ => { @print("sniff not pcap"); c.abort() } }
    Vec(Packet) pkts2 = oran.read_input(&pcap, Auto, oran.profile_nr_100mhz_scs30())
    chk(pkts2.len() == 1, "read_input Auto -> 1 packet")

    // ---- sniff hex / hexdump / binary ----
    Str hx = "1002 0014 0102 0001"
    match oran.sniff_format(&hx) { Hex => {} _ => { @print("sniff not hex"); c.abort() } }
    Str hd = "0x0000:  1002 0014  .."
    match oran.sniff_format(&hd) { Hexdump => {} _ => { @print("sniff not hexdump"); c.abort() } }

    @print("ORAN PCAP PASS")
}
