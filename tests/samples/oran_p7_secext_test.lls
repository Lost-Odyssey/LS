// oran_p7_secext_test.ls — P7: Section Extension TLV chain on an ST1 section.
// Builds a section with ef=1 and three SEs (SE1, SE6, and the wide-extLen SE11),
// each kept as opaque bytes, and verifies lossless round-trip of the whole chain.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def mkbytes(&Vec(int) vals) -> Str {
    Writer w = oran.writer()
    for i in 0..vals.len() { w.byte(vals[i]) }
    return w.take()
}

def main() {
    SecType1 s = {}
    s.section_id = 5  s.rb = false  s.sym_inc = false
    s.start_prbc = 0  s.num_prbc = 100  s.re_mask = 0xFFF  s.num_symbol = 14
    s.beam_id = 9     s.ef = true                      // extensions follow

    Vec(int) b1  = [0xAB, 0xCD]          // SE1: 1 word -> 2-byte opaque payload
    Vec(int) b6  = [1, 2, 3, 4, 5, 6]    // SE6: 2 words -> 6-byte opaque payload
    Vec(int) b11 = [9, 8, 7, 6, 5]       // SE11: wide extLen, 2 words -> 5-byte payload
    SecExt e1 = {}   e1.ext_type = 1   e1.ef = true   e1.raw = mkbytes(b1)
    SecExt e6 = {}   e6.ext_type = 6   e6.ef = true   e6.raw = mkbytes(b6)
    SecExt e11 = {}  e11.ext_type = 11 e11.ef = false e11.raw = mkbytes(b11)
    s.exts.push(e1)  s.exts.push(e6)  s.exts.push(e11)

    Writer w = oran.writer()
    s.write(&!w)
    // section header 8 + SE1(4) + SE6(8) + SE11(8) = 28 bytes
    chk(w.size() == 28, "section+SE total should be 28")

    Reader r = w.as_reader()
    SecType1 s2 = oran.parse_st1_section(&!r)

    chk(s2.ef, "ef set")
    chk(s2.section_id == 5, "section id")
    chk(s2.beam_id == 9, "beam id")
    chk(s2.exts.len() == 3, "3 SEs parsed")

    SecExt x0 = s2.exts[0]
    chk(x0.ext_type == 1, "se0 type 1")
    chk(x0.ef, "se0 ef (more follows)")
    chk(x0.raw.len() == 2, "se0 raw 2 bytes")
    chk(x0.raw.byte_at(0) == 0xAB, "se0 byte0")
    chk(x0.raw.byte_at(1) == 0xCD, "se0 byte1")

    SecExt x1 = s2.exts[1]
    chk(x1.ext_type == 6, "se1 type 6")
    chk(x1.raw.len() == 6, "se1 raw 6 bytes")
    chk(x1.raw.byte_at(5) == 6, "se1 last byte")

    SecExt x2 = s2.exts[2]
    chk(x2.ext_type == 11, "se2 type 11 (wide extLen)")
    chk(!x2.ef, "se2 is last")
    chk(x2.raw.len() == 5, "se2 raw 5 bytes")
    chk(x2.raw.byte_at(0) == 9, "se2 byte0")
    chk(x2.raw.byte_at(4) == 5, "se2 byte4")

    chk(r.eof?(), "consumed whole section + SE chain")

    @print("ORAN P7 SE PASS")
}
