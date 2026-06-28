// oran_p2_bitcursor_test.ls — P2: BitWriter/BitReader round-trip for the
// variable-width (1..16 bit) IQ/SINR sample packing that bit-match can't do.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // ---- unsigned round-trip, mixed widths crossing byte boundaries ----
    // pack 9-bit 0x1A3, 9-bit 0x0FF, 4-bit 0xC, 9-bit 0x055, 1-bit 1
    Writer w = oran.writer()
    BitWriter bw = oran.bit_writer()
    bw.push(&!w, 0x1A3, 9)
    bw.push(&!w, 0x0FF, 9)
    bw.push(&!w, 0xC, 4)
    bw.push(&!w, 0x055, 9)
    bw.push(&!w, 1, 1)
    bw.align(&!w)                 // flush partial byte (32 bits -> 4 bytes)
    chk(w.size() == 4, "bitwriter 32 bits -> 4 bytes")

    Reader r = w.as_reader()
    BitReader br = oran.bit_reader(r.buf.data, r.buf.len(), 0)
    chk(br.take(9) == 0x1A3, "u take 0x1A3")
    chk(br.take(9) == 0x0FF, "u take 0x0FF")
    chk(br.take(4) == 0xC,   "u take 0xC")
    chk(br.take(9) == 0x055, "u take 0x055")
    chk(br.take(1) == 1,     "u take 1")

    // ---- signed (two's complement) round-trip: typical IQ samples ----
    // 9-bit signed values: -1 (0x1FF), -256 (0x100 = min), +255 (0x0FF = max), 0
    Writer w2 = oran.writer()
    BitWriter bw2 = oran.bit_writer()
    bw2.push(&!w2, 0x1FF, 9)      // -1
    bw2.push(&!w2, 0x100, 9)      // -256
    bw2.push(&!w2, 0x0FF, 9)      // +255
    bw2.push(&!w2, 0x000, 9)      // 0
    bw2.align(&!w2)
    Reader r2 = w2.as_reader()
    BitReader br2 = oran.bit_reader(r2.buf.data, r2.buf.len(), 0)
    chk(br2.take_signed(9) == -1,   "s -1")
    chk(br2.take_signed(9) == -256, "s -256")
    chk(br2.take_signed(9) == 255,  "s +255")
    chk(br2.take_signed(9) == 0,    "s 0")

    // ---- 16-bit (full width) sample ----
    Writer w3 = oran.writer()
    BitWriter bw3 = oran.bit_writer()
    bw3.push(&!w3, 0xFFFF, 16)    // -1 as signed 16
    bw3.push(&!w3, 0x8000, 16)    // -32768 (min)
    bw3.align(&!w3)
    chk(w3.size() == 4, "two 16-bit -> 4 bytes")
    Reader r3 = w3.as_reader()
    BitReader br3 = oran.bit_reader(r3.buf.data, r3.buf.len(), 0)
    chk(br3.take_signed(16) == -1,     "s16 -1")
    chk(br3.take_signed(16) == -32768, "s16 min")

    @print("ORAN P2 BITCURSOR PASS")
}
