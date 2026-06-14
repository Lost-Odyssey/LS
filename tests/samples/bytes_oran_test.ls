// bytes_oran_test.ls — std.bytes Reader + bit-pattern match on a synthetic
// O-RAN / eCPRI fronthaul packet. JIT+AOT+memcheck. Prints "ORAN PASS".
//
// Packet layout (12 bytes), big-endian on the wire:
//   eCPRI common header (4B):
//     [0]=0x10  version=1, reserved=0, C=0
//     [1]=0x00  message type = 0x00 (U-plane IQ data)
//     [2..3]=0x0020  payload size = 32
//   eCPRI Type-0 transport (4B):
//     [4..5]=0x0102  PC_ID
//     [6..7]=0x0304  SEQ_ID
//   O-RAN U-plane application header (4B):
//     [8]=0x90       dataDirection=1, payloadVersion=1, filterIndex=0
//     [9]=0x37       frameId = 55
//     [10..11]=0x2143  subframeId=2, slotId=5, startSymbolId=3  (4+6+6=16 bits)
import std.c as c
import std.bytes as b

fn fail(Str msg) { print(msg); c.abort() }

fn put(*u8 p, int i, int v) { p[i] = v as u8 }

fn main() {
    // ---- build the packet ----
    *u8 buf = c.malloc(16)
    put(buf, 0, 0x10) put(buf, 1, 0x00) put(buf, 2, 0x00) put(buf, 3, 0x20)
    put(buf, 4, 0x01) put(buf, 5, 0x02) put(buf, 6, 0x03) put(buf, 7, 0x04)
    put(buf, 8, 0x90) put(buf, 9, 0x37) put(buf, 10, 0x21) put(buf, 11, 0x43)

    Reader r = b.borrow(buf, 12)

    // ---- eCPRI common header: read u32, match bit fields ----
    match r.be_u32() {
        bits[4:version][3:rsv][1:concat][8:msg_type][16:payload_size] => {
            if (version != 1)        { fail("FAIL ecpri version") }
            if (concat)              { fail("FAIL ecpri concat") }
            if (msg_type != 0)       { fail("FAIL ecpri msg_type") }
            if (payload_size != 32)  { fail("FAIL ecpri payload_size") }
        }
        _ => { fail("FAIL ecpri nomatch") }
    }

    // ---- eCPRI Type-0 transport: PC_ID + SEQ_ID ----
    u16 pc_id  = r.be_u16()
    u16 seq_id = r.be_u16()
    if (pc_id != (0x0102 as u16))  { fail("FAIL pc_id") }
    if (seq_id != (0x0304 as u16)) { fail("FAIL seq_id") }

    // ---- O-RAN U-plane application header ----
    // octet 8: dataDirection(1) | payloadVersion(3) | filterIndex(4)
    match r.be_u8() {
        bits[1:dir][3:pver][4:fidx] => {
            if (!dir)         { fail("FAIL uplane dir") }
            if (pver != 1)    { fail("FAIL uplane pver") }
            if (fidx != 0)    { fail("FAIL uplane fidx") }
        }
        _ => { fail("FAIL uplane octet8") }
    }
    // octet 9: frameId
    u8 frame_id = r.be_u8()
    if (frame_id != (55 as u8)) { fail("FAIL frame_id") }
    // octets 10..11: subframeId(4) | slotId(6) | startSymbolId(6) — crosses a byte
    match r.be_u16() {
        bits[4:subframe][6:slot][6:sym] => {
            if (subframe != 2) { fail("FAIL subframe") }
            if (slot != 5)     { fail("FAIL slot") }
            if (sym != 3)      { fail("FAIL sym") }
        }
        _ => { fail("FAIL slotinfo") }
    }

    // ---- cursor exhausted ----
    if (r.remaining() != 0) { fail("FAIL remaining") }
    if (!r.eof?())          { fail("FAIL eof") }
    c.free(buf)

    // ---- little-endian sanity: same 4 bytes read LE ----
    *u8 lebuf = c.malloc(8)
    put(lebuf, 0, 0xAA) put(lebuf, 1, 0xBB) put(lebuf, 2, 0xCC) put(lebuf, 3, 0xDD)
    Reader rl = b.borrow(lebuf, 4)
    u32 lw = rl.le_u32()           // bytes AA BB CC DD little-endian = 0xDDCCBBAA
    if (lw != (0xDDCCBBAA as u32)) { fail("FAIL le_u32") }
    c.free(lebuf)

    // ---- owned path: of_bytes takes an owned heap Str, frees it on drop ----
    // f-string builds an owned (cap>0) Str with bytes 'A''B''C''D' = 0x41424344.
    Str owned = f"ABCD"
    Reader ro = b.of_bytes(owned)
    u32 ow = ro.be_u32()
    if (ow != (0x41424344 as u32)) { fail("FAIL of_bytes be_u32") }

    print("ORAN PASS")
}
