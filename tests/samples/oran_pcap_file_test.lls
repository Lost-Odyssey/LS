// oran_pcap_file_test.ls — read a real, committed .pcap fixture off disk.
// tests/samples/oran_sample.pcap is a genuine classic pcap (openable in Wireshark)
// holding two records: an ST1 C-plane message and a U-plane IQ message. This test
// reads that file with io.read_file and parses it via read_input — the full
// on-disk path, against a persistent artifact (the file is NOT generated or
// deleted here; it lives in the repo as a sample).
import std.sys.c as c
import std.sys.io as io
import std.sys.env as env
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // Locate the fixture relative to LS_HOME (set by the test harness); fall back to
    // a repo-root-relative path for standalone runs.
    Str path = "tests/samples/oran_sample.pcap"
    match env.get("LS_HOME") {
        Some(h) => { path = h + "/tests/samples/oran_sample.pcap" }
        None => {}
    }
    chk(io.exists(path), f"fixture pcap exists: {path}")

    // ---- read the bytes off disk ----
    Str onwire = ""
    match io.read_file(path) { Ok(d) => { onwire = d } Err(e) => { @print("read failed"); c.abort() } }
    chk(onwire.len() > 24, "pcap larger than global header")

    // ---- explicit Pcap format ----
    Vec(Packet) pkts = oran.read_input(&onwire, Pcap, oran.profile_nr_100mhz_scs30())
    chk(pkts.len() == 2, "2 packets from disk pcap")
    chk(oran.section_type_of(pkts[0]) == 1, "pkt0 ST1")
    chk(oran.eaxc_of(pkts[0]) == 0x0102, "pkt0 eaxc 0x0102")
    chk(oran.is_dl?(pkts[0]), "pkt0 DL")
    chk(oran.is_uplane?(pkts[1]), "pkt1 U-plane")
    chk(oran.eaxc_of(pkts[1]) == 0x0105, "pkt1 eaxc 0x0105")

    // ---- auto-detect resolves the on-disk bytes to Pcap ----
    match oran.sniff_format(&onwire) { Pcap => {} _ => { @print("disk sniff not pcap"); c.abort() } }
    Vec(Packet) pkts2 = oran.read_input(&onwire, Auto, oran.profile_nr_100mhz_scs30())
    chk(pkts2.len() == 2, "Auto from disk -> 2 packets")

    @print("ORAN PCAP FILE PASS")
}
