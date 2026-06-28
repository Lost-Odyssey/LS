// oran/cus.ls — O-RAN WG4 fronthaul CUS-plane (Control/User/Sync) parse + build.
//
// Spec: O-RAN.WG4.TS.CUS.0-R005-v20.00. A pure-LS library, parallel to std,
// built on std.text.bytes (Reader) for parsing and a Writer (below) for building.
//
// Import:  import oran.cus as oran   (free fns qualified: oran.writer / oran.parse_ecpri)
//          import std.text.bytes      (for the Reader type, if you hold one)
//
// Pipeline: [input] -> parse -> Vec(Packet) -> filter (closures) -> stats / render.
// Wire format is network byte order (big-endian, MSB first) per spec 7.3.1.
//
// Design: protocol fields are stored as `int` (small non-negative values), not
// u8/u16 — building packets and comparing fields then needs no casts. The on-wire
// bit width lives in the write masks / be_uN calls and in the field comments, not
// in the storage type.
//
// Reference scenario for tests: NR 100 MHz, numerology mu=1 (30 kHz SCS),
// 273 PRBs, FFT 4096, 64 antennas.

import std.sys.c as c
import std.text.bytes as b
import std.core.str
import std.core.vec
import std.text.json as json

// ---- small helpers ----
def b2i(bool x) -> int { if x { return 1 } else { return 0 } }

// hex digit -> 0..15, or -1 for non-hex (used by the input adapters)
def hex_nibble(int ch) -> int {
    if ch >= 48 && ch <= 57  { return ch - 48 }   // '0'..'9'
    if ch >= 97 && ch <= 102 { return ch - 87 }   // 'a'..'f'
    if ch >= 65 && ch <= 70  { return ch - 55 }   // 'A'..'F'
    return -1
}

// =====================================================================
//  Writer — growable big-endian byte buffer (pure LS, owns its buffer).
//  Move-only (raw *u8 + Destroy, no Clone): bind/move, never copy.
// =====================================================================
struct Writer { *u8 buf; int len; int cap }

def writer() -> Writer {
    *u8 b0 = c.malloc(64)
    return Writer{ buf: b0, len: 0, cap: 64 }
}

methods Writer: Destroy {
    def ~(&!self) { c.free(self.buf) }
}

methods Writer {
    def reserve(&!self, int n) {
        if self.len + n <= self.cap { return }
        int nc = self.cap * 2
        while nc < self.len + n { nc = nc * 2 }
        self.buf = c.realloc(self.buf, nc)
        self.cap = nc
    }
    // one raw byte (low 8 bits of v); 'u8' is a reserved type name so the method is 'byte'
    def byte(&!self, int v) {
        self.reserve(1)
        self.buf[self.len] = v as u8
        self.len = self.len + 1
    }
    def be_u16(&!self, int v) { self.byte((v >> 8) & 0xFF); self.byte(v & 0xFF) }
    def be_u24(&!self, int v) { self.byte((v >> 16) & 0xFF); self.byte((v >> 8) & 0xFF); self.byte(v & 0xFF) }
    def be_u32(&!self, int v) {
        self.byte((v >> 24) & 0xFF); self.byte((v >> 16) & 0xFF)
        self.byte((v >> 8) & 0xFF); self.byte(v & 0xFF)
    }
    def be_u64(&!self, u64 v) {
        int hi = (v >> (32 as u64)) as int
        int lo = (v & (0xFFFFFFFF as u64)) as int
        self.be_u32(hi); self.be_u32(lo)
    }
    // append the raw bytes of a Str (used to splice opaque Section-Extension payloads)
    def bytes(&!self, &Str s) {
        for i in 0..s.len() { self.byte(s.byte_at(i)) }
    }
    def size(&self) -> int { return self.len }
    // a non-owning Reader over the bytes written so far (must not outlive Writer)
    def as_reader(&self) -> Reader { return b.borrow(self.buf, self.len) }
    // overwrite 2 already-written bytes at `off` (used to backpatch a length field
    // once the trailing bytes are known, e.g. U-plane eCPRI payload_size)
    def patch_be_u16(&!self, int off, int v) {
        self.buf[off] = ((v >> 8) & 0xFF) as u8
        self.buf[off + 1] = (v & 0xFF) as u8
    }
    // hand the buffer to an owned Str (cap>0 owns + frees on drop); empties the
    // Writer so its destructor frees nothing.
    def take(&!self) -> Str {
        Str s = Str{ data: self.buf, len: self.len, cap: self.cap }
        self.buf = nil
        self.len = 0
        self.cap = 0
        return s
    }
}

// =====================================================================
//  Input adapters — turn a byte/text source into an owning Reader.
//   from_file:    read a binary capture (io.read_file -> of_bytes).  [see io]
//   from_hex:     a continuous hex string ("100000200102..."), ignoring any
//                 whitespace/colons between digits.
//   from_hexdump: tcpdump -x/-X or similar, where each line has an offset
//                 column ("0x0000:") and a trailing ASCII column to discard.
//  The returned Reader OWNS its decoded buffer and frees it on drop.
// =====================================================================
def from_hex(&Str hex) -> Reader {
    Writer w = writer()
    int hi = -1
    for i in 0..hex.len() {
        int nib = hex_nibble(hex.byte_at(i))
        if nib < 0 { continue }
        if hi < 0 { hi = nib } else { w.byte((hi << 4) | nib); hi = -1 }
    }
    Str bytes = w.take()
    return b.of_bytes(bytes)
}

def from_hexdump(&Str dump) -> Reader {
    Writer w = writer()
    Vec(Str) lns = dump.lines()
    int nl = lns.len()
    for li in 0..nl {
        Str line = lns[li]
        // strip the offset column (everything up to ':'), then trim
        int colon = line.find(":")
        Str t = {}
        if colon >= 0 { t = line.substr(colon + 1, line.len() - colon - 1).trim() }
        else { t = line.trim() }
        // cut the trailing ASCII column: hex and ASCII are separated by 2+ spaces
        Str hexpart = {}
        int dbl = t.find("  ")
        if dbl >= 0 { hexpart = t.substr(0, dbl) } else { hexpart = t }
        int hi = -1
        for i in 0..hexpart.len() {
            int nib = hex_nibble(hexpart.byte_at(i))
            if nib < 0 { continue }
            if hi < 0 { hi = nib } else { w.byte((hi << 4) | nib); hi = -1 }
        }
    }
    Str bytes = w.take()
    return b.of_bytes(bytes)
}

// =====================================================================
//  eCPRI transport header (8 bytes, spec 5.1.3.2.1)
//   Octet1: version(4) reserved(3) concat(1) | Octet2: message(8)
//   Octet3-4: payload(16) | 5-6: rtcid/pcid(16) | 7-8: seqid(16)
//  ecpriMessage: 0x00 = IQ data (U-plane), 0x02 = RT control (C-plane).
//  rtcid/pcid is the 16-bit eAxC ID (opaque here; band/CC/RU split is M-plane).
// =====================================================================
struct EcpriHdr {
    int version       // 4b, =0x1
    bool concat       // 1b
    int msg_type      // 8b
    int payload_size  // 16b
    int rtcid_pcid    // 16b eAxC ID
    int seqid         // 16b
}

def parse_ecpri(&!Reader r) -> EcpriHdr {
    EcpriHdr h = {}
    match r.be_u8() {
        bits[4:ver][3:rsv][1:cc] => { h.version = ver; h.concat = cc }
        _ => {}
    }
    h.msg_type     = r.be_u8() as int
    h.payload_size = r.be_u16() as int
    h.rtcid_pcid   = r.be_u16() as int
    h.seqid        = r.be_u16() as int
    return h
}

methods EcpriHdr {
    def write(&self, &!Writer w) {
        w.byte((self.version << 4) | b2i(self.concat))
        w.byte(self.msg_type)
        w.be_u16(self.payload_size)
        w.be_u16(self.rtcid_pcid)
        w.be_u16(self.seqid)
    }
}

// =====================================================================
//  C-plane application common header (octets 9..14, spec 7.4.x / 7.5.2)
//   O9: dataDirection(1) payloadVersion(3) filterIndex(4)
//   O10: frameId(8) | O11-12: subframeId(4) slotId(6) startSymbolId(6)
//   O13: numberOfSections(8) | O14: sectionType(8)
//  ST-specific octets (udCompHdr, timeOffset, ...) are handled per Section Type.
// =====================================================================
struct CpCommonHdr {
    bool dir            // dataDirection: 0=UL/Rx, 1=DL/Tx
    int payload_ver     // 3b, =1
    int filter_idx      // 4b
    int frame_id        // 8b
    int subframe_id     // 4b
    int slot_id         // 6b
    int start_symbol_id // 6b
    int num_sections    // 8b
    int section_type    // 8b
}

def parse_cp_common(&!Reader r) -> CpCommonHdr {
    CpCommonHdr h = {}
    match r.be_u8() {
        bits[1:d][3:pv][4:fi] => { h.dir = d; h.payload_ver = pv; h.filter_idx = fi }
        _ => {}
    }
    h.frame_id = r.be_u8() as int
    match r.be_u16() {
        bits[4:sf][6:sl][6:ss] => { h.subframe_id = sf; h.slot_id = sl; h.start_symbol_id = ss }
        _ => {}
    }
    h.num_sections = r.be_u8() as int
    h.section_type = r.be_u8() as int
    return h
}

methods CpCommonHdr {
    def write(&self, &!Writer w) {
        w.byte((b2i(self.dir) << 7) | (self.payload_ver << 4) | self.filter_idx)
        w.byte(self.frame_id)
        w.be_u16((self.subframe_id << 12) | (self.slot_id << 6) | self.start_symbol_id)
        w.byte(self.num_sections)
        w.byte(self.section_type)
    }
}

// =====================================================================
//  Section Extensions (spec 7.3.2 / 7.6.2): a TLV chain appended to a section
//  when its ef (extension flag) is set. Each SE starts with extType(7)+ef(1),
//  then extLen — 8 bits for all SEs except SE 11/19/20 which use 16 bits — in
//  units of 4-byte words (>=1, includes the extType/extLen word). ef in the SE
//  means another SE follows; the whole SE is an integer number of 4-byte words.
//
//  v1 keeps each SE's body as opaque bytes (`raw`) so any extType round-trips
//  losslessly without per-type decoding; specific SEs can be decoded on top.
// =====================================================================
struct SecExt {
    int ext_type     // 7b
    bool ef          // 1b: another SE follows
    Str raw          // SE bytes after the extType/extLen header (opaque payload)
}

def se_extlen_is_wide(int et) -> bool { return et == 11 || et == 19 || et == 20 }

// read n raw bytes from the reader into an owned Str (advances the cursor)
def read_raw(&!Reader r, int n) -> Str {
    Writer w = writer()
    for i in 0..n { w.byte(r.be_u8() as int) }
    return w.take()
}

def parse_se_chain(&!Reader r) -> Vec(SecExt) {
    Vec(SecExt) out = {}
    bool more = true
    while more {
        SecExt se = {}
        match r.be_u8() { bits[1:efb][7:et] => { se.ef = efb; se.ext_type = et } _ => {} }   // ef is MSB, extType low 7 bits
        int extlen = 0
        int hdr = 2
        if se_extlen_is_wide(se.ext_type) { extlen = r.be_u16() as int; hdr = 3 }
        else { extlen = r.be_u8() as int; hdr = 2 }
        int payload = extlen * 4 - hdr
        if payload < 0 { payload = 0 }
        se.raw = read_raw(&!r, payload)
        more = se.ef
        out.push(se)
    }
    return out
}

def write_se_chain(&Vec(SecExt) exts, &!Writer w) {
    for i in 0..exts.len() {
        SecExt se = exts[i]
        bool more = i < exts.len() - 1          // ef is structural: set iff another SE follows
        w.byte((b2i(more) << 7) | se.ext_type)  // ef is MSB, extType low 7 bits
        int hdr = 2
        if se_extlen_is_wide(se.ext_type) { hdr = 3 }
        int extlen = (se.raw.len() + hdr) / 4
        if se_extlen_is_wide(se.ext_type) { w.be_u16(extlen) } else { w.byte(extlen) }
        w.bytes(&se.raw)
    }
}

// =====================================================================
//  Section Type 1 section description (octets 17..24 = 64 bits, spec 7.4.3)
//   sectionId(12) rb(1) symInc(1) startPrbc(10) numPrbc(8)
//   reMask(12) numSymbol(4) ef(1) beamId(15)
//  The whole 8-byte header packs into one u64 — parse with be_u64 + bit-match,
//  build by packing a u64 (the two are exact mirrors). ef=1 (Section Extensions
//  follow) is handled in a later phase; v1 parses ef=0 sections.
// =====================================================================
struct SecType1 {
    int section_id    // 12b
    bool rb           // 1b  resource-block indicator (every-other-PRB)
    bool sym_inc      // 1b  symbol-number increment
    int start_prbc    // 10b
    int num_prbc      // 8b
    int re_mask       // 12b resource-element mask
    int num_symbol    // 4b
    bool ef           // 1b  extension flag (Section Extensions follow when set)
    int beam_id       // 15b
    Vec(SecExt) exts  // present iff ef
}

def parse_st1_section(&!Reader r) -> SecType1 {
    SecType1 s = {}
    match r.be_u64() {
        bits[12:sid][1:rb][1:si][10:sp][8:np][12:rm][4:ns][1:ef][15:bid] => {
            s.section_id = sid; s.rb = rb; s.sym_inc = si
            s.start_prbc = sp; s.num_prbc = np; s.re_mask = rm
            s.num_symbol = ns; s.ef = ef; s.beam_id = bid
        }
        _ => {}
    }
    if s.ef { s.exts = parse_se_chain(&!r) }
    return s
}

methods SecType1 {
    def write(&self, &!Writer w) {
        u64 p = ((self.section_id as u64) << (52 as u64))
              | ((b2i(self.rb) as u64) << (51 as u64))
              | ((b2i(self.sym_inc) as u64) << (50 as u64))
              | ((self.start_prbc as u64) << (40 as u64))
              | ((self.num_prbc as u64) << (32 as u64))
              | ((self.re_mask as u64) << (20 as u64))
              | ((self.num_symbol as u64) << (16 as u64))
              | ((b2i(self.ef) as u64) << (15 as u64))
              | (self.beam_id as u64)
        w.be_u64(p)
        if self.ef { write_se_chain(&self.exts, &!w) }
    }
}

// =====================================================================
//  Full Section Type 1 C-plane message: eCPRI + common header
//   + udCompHdr(O15) + reserved(O16) + numberOfSections * SecType1.
// =====================================================================
struct CplaneSt1 {
    EcpriHdr ecpri
    CpCommonHdr hdr
    int ud_comp_hdr        // octet 15 (udIqWidth<<4 | udCompMeth); 0 for static/DL
    Vec(SecType1) sections
}

def parse_st1(&!Reader r) -> CplaneSt1 {
    CplaneSt1 m = {}
    m.ecpri = parse_ecpri(&!r)
    m.hdr = parse_cp_common(&!r)
    m.ud_comp_hdr = r.be_u8() as int    // octet 15
    int reserved = r.be_u8() as int     // octet 16
    int n = m.hdr.num_sections
    for i in 0..n {
        SecType1 s = parse_st1_section(&!r)
        m.sections.push(s)
    }
    return m
}

methods CplaneSt1 {
    def write(&self, &!Writer w) {
        self.ecpri.write(&!w)
        self.hdr.write(&!w)
        w.byte(self.ud_comp_hdr)        // octet 15
        w.byte(0)                       // octet 16 reserved
        for s in &self.sections { s.write(&!w) }
    }
}

// =====================================================================
//  bit-cursor — read/write variable-width fields (1..16 bits) that are NOT
//  byte-aligned: IQ samples (iSample/qSample) and SINR values, MSB-first.
//  This is what bit-match can't do (its widths are compile-time literals);
//  here the width is a runtime value (udIqWidth). Samples are two's-complement
//  signed (spec 8.3.3.16/17), so take_signed sign-extends.
//
//  Both are POD (no owned buffers): BitReader points into an existing *u8;
//  BitWriter is just the bit accumulator and takes the target Writer as an
//  argument each call (a Writer is move-only and can't live in a struct field).
// =====================================================================
struct BitReader { *u8 buf; int len; int byte_pos; int bit_pos }

def bit_reader(*u8 buf, int len, int start) -> BitReader {
    return BitReader{ buf: buf, len: len, byte_pos: start, bit_pos: 0 }
}

methods BitReader {
    // read `width` bits MSB-first as an unsigned value
    def take(&!self, int width) -> int {
        int v = 0
        for i in 0..width {
            if self.byte_pos >= self.len { @print("BitReader: read past end"); c.abort() }
            int byte = self.buf[self.byte_pos] as int
            int bit = (byte >> (7 - self.bit_pos)) & 1
            v = (v << 1) | bit
            self.bit_pos = self.bit_pos + 1
            if self.bit_pos == 8 { self.bit_pos = 0; self.byte_pos = self.byte_pos + 1 }
        }
        return v
    }
    // read `width` bits as a two's-complement signed value
    def take_signed(&!self, int width) -> int {
        int v = self.take(width)
        if (v & (1 << (width - 1))) != 0 { v = v - (1 << width) }
        return v
    }
    // advance to the next byte boundary (discard remaining bits in the current byte)
    def align(&!self) {
        if self.bit_pos != 0 { self.bit_pos = 0; self.byte_pos = self.byte_pos + 1 }
    }
    def pos(&self) -> int { return self.byte_pos }
}

struct BitWriter { int acc; int nbits }

def bit_writer() -> BitWriter { return BitWriter{ acc: 0, nbits: 0 } }

methods BitWriter {
    // append `width` low bits of v, MSB-first, flushing whole bytes to w
    def push(&!self, &!Writer w, int v, int width) {
        for i in 0..width {
            int bit = (v >> (width - 1 - i)) & 1
            self.acc = (self.acc << 1) | bit
            self.nbits = self.nbits + 1
            if self.nbits == 8 { w.byte(self.acc); self.acc = 0; self.nbits = 0 }
        }
    }
    // pad the current partial byte with zeros and flush it (1-byte alignment)
    def align(&!self, &!Writer w) {
        if self.nbits > 0 { w.byte(self.acc << (8 - self.nbits)); self.acc = 0; self.nbits = 0 }
    }
}

// =====================================================================
//  Section Type 9: SINR reporting (O-RU -> O-DU, spec 7.4.11).
//   Common header octets 9..14 are the same shape as CpCommonHdr (octet-12's
//   low 6 bits carry symbolId rather than startSymbolId — same position).
//   Octet 15: numSinrPerPrb(3) oruControlSinrSlotMaskId(5) | Octet 16: reserved.
//   Per section header (octets 17..20 = 32 bits):
//     sectionId(12) rb(1) symInc(1) startPrbu(10) numPrbu(8)
//   Per PRB: optional sinrCompParam(8, BFP exponent) + numSinrPerPrb * sinrValue
//   (each `iq_width` bits, two's-complement signed), then 1-byte alignment.
//
//   iq_width and comp_meth are configured via M-plane (static), NOT on the wire,
//   so parse takes them as arguments (the Profile). comp_meth: 0=uncompressed,
//   1=BFP. sinrValue raw decode: uncompressed -> raw; BFP -> raw * 2^compParam.
// =====================================================================
struct SinrPrb {
    int comp_param      // sinrCompParam (BFP exponent); 0 when uncompressed
    Vec(int) sinr_raw   // numSinrPerPrb raw signed SINR values (sub-band order, low freq first)
}

struct SecType9 {
    int section_id
    bool rb
    bool sym_inc
    int start_prbu
    int num_prbu
    Vec(SinrPrb) prbs
}

struct CplaneSt9 {
    EcpriHdr ecpri
    CpCommonHdr hdr            // section_type=9; start_symbol_id field holds symbolId
    int num_sinr_per_prb      // 3b, divisor of 12 (1,2,3,4,6,12)
    int oru_ctrl_slot_mask_id // 5b
    int iq_width              // SINR sample bit width (M-plane); 0 means 16
    int comp_meth             // 0=uncompressed, 1=BFP (M-plane)
    Vec(SecType9) sections
}

// raw signed SINR -> decoded real value (linear). dB reference level scaling is
// an M-plane concern applied by the caller on top of this.
def pow2f(int e) -> f64 {
    f64 r = 1.0
    for i in 0..e { r = r * 2.0 }
    return r
}
// BFP exponent is the low 4 bits of the (sinr/ud)CompParam byte (high 4 reserved).
def bfp_exp(int comp_param) -> int { return comp_param & 0xF }

// decode one compressed mantissa to its real value.
//   none(0)/mod-compr(4 raw): value as-is   BFP(1): mantissa * 2^exp
//   block-scaling(2): mantissa * scaler (1 int + 7 frac bits, comp_param/128)
//   u-law(3): not decompressed here (companding) -> returns raw
def iq_decode(int mantissa, int comp_param, int comp_meth) -> f64 {
    if comp_meth == 1 { return (mantissa as f64) * pow2f(bfp_exp(comp_param)) }
    if comp_meth == 2 { return (mantissa as f64) * ((comp_param as f64) / 128.0) }
    return mantissa as f64
}
def sinr_decode(int raw, int comp_param, int comp_meth) -> f64 { return iq_decode(raw, comp_param, comp_meth) }

def comp_name(int cm) -> Str {
    if cm == 0 { return "none" }
    if cm == 1 { return "BFP" }
    if cm == 2 { return "block-scaling" }
    if cm == 3 { return "u-law" }
    if cm == 4 { return "modulation-compr" }
    return "other"
}

// "(i,q) (i,q) ..." raw mantissa pairs
def iq_raw_pairs(&Vec(int) iv, &Vec(int) qv) -> Str {
    Str s = ""
    for i in 0..iv.len() { if i > 0 { s = s + " " } s = s + "(" + istr(iv[i]) + "," + istr(qv[i]) + ")" }
    return s
}
// decoded "(i,q) (i,q) ..." pairs (mantissa shifted/scaled by comp method)
def iq_dec_pairs(&Vec(int) iv, &Vec(int) qv, int comp_param, int comp_meth) -> Str {
    Str s = ""
    for i in 0..iv.len() {
        if i > 0 { s = s + " " }
        int di = iq_decode(iv[i], comp_param, comp_meth) as int
        int dq = iq_decode(qv[i], comp_param, comp_meth) as int
        s = s + "(" + istr(di) + "," + istr(dq) + ")"
    }
    return s
}

// human-readable udCompParam (the per-PRB compression parameter byte):
//   BFP -> raw byte + extracted exponent + scale factor 2^exp
//   block-scaling -> raw byte + scaler value (1 int + 7 frac bits, byte/128)
def compparam_str(int comp_param, int comp_meth) -> Str {
    if comp_meth == 1 {
        int e = bfp_exp(comp_param)
        return hex8(comp_param) + "  →  BFP exponent " + istr(e) + "  (scale ×2^" + istr(e) + " = ×" + istr(1 << e) + ")"
    }
    if comp_meth == 2 {
        return hex8(comp_param) + "  →  block scaler " + istr(comp_param) + "/128"
    }
    return hex8(comp_param)
}

// ---- modulation compression (DL only, SE4 + Annex A.5) ----
// The compressed IQ lives in the U-plane; its csf + modCompScaler live in the
// matching C-plane section's SE4. So decode is cross-plane: extract the params
// from the C-plane section, then decompress the U-plane samples.
struct ModComp { bool present; bool csf; f64 scaler }

// modCompScaler: 15-bit field = exponent(4 MSB) + mantissa(11 LSB);
// scale factor = mantissa / 2^(11 + exponent)  (range 0 .. 1-2^-11)
def mod_comp_scaler(int field15) -> f64 {
    int exponent = (field15 >> 11) & 0xF
    int mantissa = field15 & 0x7FF
    return (mantissa as f64) / pow2f(11 + exponent)
}
def make_mod_comp_scaler(int exponent, int mantissa) -> int { return ((exponent & 0xF) << 11) | (mantissa & 0x7FF) }

// build an SE4 (modulation compression parameters: csf + modCompScaler)
def make_se4(bool csf, int exponent, int mantissa) -> SecExt {
    int field = make_mod_comp_scaler(exponent, mantissa)
    Writer w = writer()
    w.byte((b2i(csf) << 7) | ((field >> 8) & 0x7F))   // csf(1) | modCompScaler[14:8]
    w.byte(field & 0xFF)                               // modCompScaler[7:0]
    SecExt se = {}
    se.ext_type = 4
    se.ef = false
    se.raw = w.take()
    return se
}

// extract SE4 modulation-compression params from a section's extension list
def find_mod_comp(&Vec(SecExt) exts) -> ModComp {
    ModComp mc = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 4 {
            if se.raw.len() >= 2 {
                int b0 = se.raw.byte_at(0)
                int b1 = se.raw.byte_at(1)
                mc.present = true
                mc.csf = ((b0 >> 7) & 1) != 0
                mc.scaler = mod_comp_scaler(((b0 & 0x7F) << 8) | b1)
            }
        }
    }
    return mc
}

// modulation decompression of one udIqWidth-bit two's-complement sample (Annex A.5):
//   frac = sample / 2^(w-1); if csf add the shift 2^(-w); scaled = frac * modCompScaler.
// Result is the normalized IQ (|scaled| <= 1, 1.0 = 0 dBFS).
def mod_decompress(int sample, int iq_width, bool csf, f64 scaler) -> f64 {
    f64 frac = (sample as f64) / pow2f(iq_width - 1)
    if csf { frac = frac + (1.0 / pow2f(iq_width)) }
    return frac * scaler
}

// "(I,Q) (I,Q) ..." of mod-compr decompressed (normalized fractional) samples
def mod_dec_pairs(&Vec(int) iv, &Vec(int) qv, int iq_width, &ModComp mc) -> Str {
    Str s = ""
    for i in 0..iv.len() {
        if i > 0 { s = s + " " }
        f64 di = mod_decompress(iv[i], iq_width, mc.csf, mc.scaler)
        f64 dq = mod_decompress(qv[i], iq_width, mc.csf, mc.scaler)
        s = s + "(" + f"{di}" + "," + f"{dq}" + ")"
    }
    return s
}

// modulation scheme inferred from udIqWidth for mod-compr: each axis has 2^w
// states, so the constellation is (2^w)^2 = 4^w QAM (Annex A.5). In mixed-MCS
// this is the largest constellation in the block; lower-MCS REs are not shifted.
def mod_scheme(int iq_width) -> Str {
    if iq_width == 1 { return "QPSK" }
    if iq_width == 2 { return "16QAM" }
    if iq_width == 3 { return "64QAM" }
    if iq_width == 4 { return "256QAM" }
    if iq_width == 5 { return "1024QAM" }
    return "iqWidth " + istr(iq_width)
}

// un-shift only (remove the csf constellation shift): the fractional constellation
// point, BEFORE the modCompScaler power scaling. Equivalent to the constellation
// diagram position.  frac = sample / 2^(w-1); add shift 2^(-w) when csf.
def mod_unshift(int sample, int iq_width, bool csf) -> f64 {
    f64 frac = (sample as f64) / pow2f(iq_width - 1)
    if csf { frac = frac + (1.0 / pow2f(iq_width)) }
    return frac
}
def mod_unshift_pairs(&Vec(int) iv, &Vec(int) qv, int iq_width, bool csf) -> Str {
    Str s = ""
    for i in 0..iv.len() {
        if i > 0 { s = s + " " }
        f64 di = mod_unshift(iv[i], iq_width, csf)
        f64 dq = mod_unshift(qv[i], iq_width, csf)
        s = s + "(" + f"{di}" + "," + f"{dq}" + ")"
    }
    return s
}

// the constellation point as an EXACT reduced fraction (2*sample + csf) / 2^w,
// e.g. 3/4, -1/4 for 16QAM or 7/8, -5/8 for 64QAM — matches the QAM grid lines.
def mod_const_frac(int sample, int iq_width, bool csf) -> Str {
    int num = 2 * sample + b2i(csf)
    int den = 1 << iq_width
    if num == 0 { return "0" }
    while (num & 1) == 0 && den > 1 { num = num / 2  den = den / 2 }   // reduce powers of 2
    if den == 1 { return istr(num) }
    return istr(num) + "/" + istr(den)
}
def mod_const_pairs(&Vec(int) iv, &Vec(int) qv, int iq_width, bool csf) -> Str {
    Str s = ""
    for i in 0..iv.len() {
        if i > 0 { s = s + " " }
        s = s + "(" + mod_const_frac(iv[i], iq_width, csf) + "," + mod_const_frac(qv[i], iq_width, csf) + ")"
    }
    return s
}

// =====================================================================
//  Section Extension field decoders (Pass 2) — decode a SecExt.raw into a
//  typed struct. The raw is kept (still round-trips via write_se_chain);
//  these are an overlay view. has_seN?/find_seN follow find_mod_comp's shape.
//  Pad helper: a SE is an integer number of 4-byte words; its 2-byte header
//  (extType+extLen, or 3 for the wide-extLen SE11/19/20) plus the value bytes
//  must be a multiple of 4. make_* pad the value accordingly.
// =====================================================================
def se_has?(&Vec(SecExt) exts, int et) -> bool {
    for i in 0..exts.len() { if exts[i].ext_type == et { return true } }
    return false
}
def se_pad4(&!Writer w, int hdr) {            // pad value so (hdr + value) % 4 == 0
    while ((w.size() + hdr) % 4) != 0 { w.byte(0) }
}

// ---- SE6: non-contiguous PRB in time/freq (extType 0x06) ----
struct Se6 { bool present; int repetition; int rbg_size; int rbg_mask; int priority; int symbol_mask }
def make_se6(int repetition, int rbg_size, int rbg_mask, int priority, int symbol_mask) -> SecExt {
    Writer w = writer()
    w.byte(((repetition & 1) << 7) | ((rbg_size & 0x7) << 4) | ((rbg_mask >> 24) & 0xF))
    w.byte((rbg_mask >> 16) & 0xFF)
    w.byte((rbg_mask >> 8) & 0xFF)
    w.byte(rbg_mask & 0xFF)
    w.byte(((priority & 0x3) << 6) | ((symbol_mask >> 8) & 0x3F))
    w.byte(symbol_mask & 0xFF)
    SecExt se = {}  se.ext_type = 6  se.ef = false  se.raw = w.take()
    return se
}
def find_se6(&Vec(SecExt) exts) -> Se6 {
    Se6 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 6 {
            if se.raw.len() >= 6 {
                int b0 = se.raw.byte_at(0)
                r.present = true
                r.repetition = (b0 >> 7) & 1
                r.rbg_size = (b0 >> 4) & 0x7
                r.rbg_mask = ((b0 & 0xF) << 24) | (se.raw.byte_at(1) << 16) | (se.raw.byte_at(2) << 8) | se.raw.byte_at(3)
                r.priority = (se.raw.byte_at(4) >> 6) & 0x3
                r.symbol_mask = ((se.raw.byte_at(4) & 0x3F) << 8) | se.raw.byte_at(5)
            }
        }
    }
    return r
}

// ---- SE12: non-contiguous PRB via frequency ranges (extType 0x0C) ----
struct Se12 { bool present; int priority; int symbol_mask; Vec(int) off_start_prb; Vec(int) num_prb }
def make_se12(int priority, int symbol_mask, &Vec(int) off, &Vec(int) num) -> SecExt {
    Writer w = writer()
    w.byte(((priority & 0x3) << 6) | ((symbol_mask >> 8) & 0x3F))
    w.byte(symbol_mask & 0xFF)
    for i in 0..off.len() { w.byte(off[i] & 0xFF)  w.byte(num[i] & 0xFF) }
    se_pad4(&!w, 2)
    SecExt se = {}  se.ext_type = 12  se.ef = false  se.raw = w.take()
    return se
}
def find_se12(&Vec(SecExt) exts) -> Se12 {
    Se12 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 12 {
            if se.raw.len() >= 2 {
                r.present = true
                r.priority = (se.raw.byte_at(0) >> 6) & 0x3
                r.symbol_mask = ((se.raw.byte_at(0) & 0x3F) << 8) | se.raw.byte_at(1)
                int npairs = (se.raw.len() - 2) / 2
                for k in 0..npairs {
                    r.off_start_prb.push(se.raw.byte_at(2 + k * 2))
                    r.num_prb.push(se.raw.byte_at(2 + k * 2 + 1))
                }
            }
        }
    }
    return r
}

// ---- SE10: group config of multiple ports (extType 0x0A) ----
//  beamGroupType: 0=common, 1=matrix (consecutive beamIds), 2=vector listing
//  (ids in SE), 3=portListIndex (not decoded). ids present only for type 2/3.
struct Se10 { bool present; int beam_group_type; int num_portc; Vec(int) ids }
def make_se10(int beam_group_type, int num_portc, &Vec(int) ids) -> SecExt {
    Writer w = writer()
    w.byte(((beam_group_type & 0x3) << 6) | (num_portc & 0x3F))
    if beam_group_type == 2 || beam_group_type == 3 {
        for i in 0..ids.len() { w.byte((ids[i] >> 8) & 0x7F)  w.byte(ids[i] & 0xFF) }
    } else {
        w.byte(0)   // reserved
    }
    se_pad4(&!w, 2)
    SecExt se = {}  se.ext_type = 10  se.ef = false  se.raw = w.take()
    return se
}
def find_se10(&Vec(SecExt) exts) -> Se10 {
    Se10 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 10 {
            if se.raw.len() >= 1 {
                int b0 = se.raw.byte_at(0)
                r.present = true
                r.beam_group_type = (b0 >> 6) & 0x3
                r.num_portc = b0 & 0x3F
                if r.beam_group_type == 2 || r.beam_group_type == 3 {
                    // numPortc ids follow (spec 7.7.10); the value is zero-padded to a
                    // 4-byte word, so read exactly num_portc, not (len-1)/2.
                    int nid = r.num_portc
                    for k in 0..nid {
                        if 1 + k * 2 + 1 < se.raw.len() {
                            int id = ((se.raw.byte_at(1 + k * 2) & 0x7F) << 8) | se.raw.byte_at(1 + k * 2 + 1)
                            r.ids.push(id)
                        }
                    }
                }
            }
        }
    }
    return r
}

// ---- SE5: modulation compression additional params (extType 0x05) ----
//  one or more 28-bit sets { mcScaleReMask(12), csf(1), mcScaleOffset(15) },
//  bit-packed; trailing all-zero set is padding (spec 7.7.5 ambiguity rule).
struct McSet { int mc_scale_re_mask; bool csf; int mc_scale_offset }
struct Se5 { bool present; Vec(McSet) sets }
def make_se5(&Vec(McSet) sets) -> SecExt {
    Writer w = writer()
    BitWriter bw = bit_writer()
    for i in 0..sets.len() {
        McSet m = sets[i]
        bw.push(&!w, m.mc_scale_re_mask, 12)
        bw.push(&!w, b2i(m.csf), 1)
        bw.push(&!w, m.mc_scale_offset, 15)
    }
    bw.align(&!w)
    se_pad4(&!w, 2)
    SecExt se = {}  se.ext_type = 5  se.ef = false  se.raw = w.take()
    return se
}
def find_se5(&Vec(SecExt) exts) -> Se5 {
    Se5 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 5 {
            r.present = true
            int maxsets = (se.raw.len() * 8) / 28
            BitReader br = bit_reader(se.raw.data, se.raw.len(), 0)
            for k in 0..maxsets {
                McSet m = {}
                m.mc_scale_re_mask = br.take(12)
                m.csf = br.take(1) != 0
                m.mc_scale_offset = br.take(15)
                r.sets.push(m)
            }
            r.sets.truncate(se5_keep(&r.sets))   // drop trailing all-zero (padding) sets
        }
    }
    return r
}
// count of leading non-(trailing-zero) sets
def se5_keep(&Vec(McSet) sets) -> int {
    int keep = sets.len()
    while keep > 0 {
        McSet last = sets[keep - 1]
        if last.mc_scale_re_mask == 0 && !last.csf && last.mc_scale_offset == 0 { keep = keep - 1 }
        else { return keep }
    }
    return keep
}

// ---- SE18: uplink transmission window management (extType 0x12) ----
//  toT: 0=normal transmission, 1=uniformly distributed over the window.
struct Se18 { bool present; int tx_window_offset; int tx_window_size; int tot }
def make_se18(int off, int size, int tot) -> SecExt {
    Writer w = writer()
    w.byte((off >> 8) & 0xFF)
    w.byte(off & 0xFF)
    w.byte((size >> 8) & 0x3F)
    w.byte(size & 0xFF)
    w.byte(tot & 0x3)
    w.byte(0)
    SecExt se = {}  se.ext_type = 18  se.ef = false  se.raw = w.take()
    return se
}
def find_se18(&Vec(SecExt) exts) -> Se18 {
    Se18 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 18 {
            if se.raw.len() >= 5 {
                r.present = true
                r.tx_window_offset = (se.raw.byte_at(0) << 8) | se.raw.byte_at(1)
                r.tx_window_size = ((se.raw.byte_at(2) & 0x3F) << 8) | se.raw.byte_at(3)
                r.tot = se.raw.byte_at(4) & 0x3
            }
        }
    }
    return r
}

// ---- SE1: beamforming weights (extType 0x01) — needs K = number of TRX/antennas
//  (M-plane / clause 12.5.3). bfwCompHdr = bfwIqWidth(4) | bfwCompMeth(4); then
//  bfwCompParam (0/1 byte for none/BFP/block-scaling), then K * {bfwI, bfwQ} each
//  bfwIqWidth bits, two's-complement, bit-packed, zero-padded to 4 bytes.
//  bfwCompMeth: 0=none, 1=BFP, 2=block-scaling, 4/5=beamspace (not decoded → weights
//  left empty, raw still round-trips). bfwIqWidth 0 means 16.
struct Se1 { bool present; int bfw_iq_width; int bfw_comp_meth; int bfw_comp_param; Vec(int) bfw_i; Vec(int) bfw_q }
def make_se1(int bfw_iq_width, int bfw_comp_meth, int bfw_comp_param, &Vec(int) bfw_i, &Vec(int) bfw_q) -> SecExt {
    Writer w = writer()
    w.byte(((bfw_iq_width & 0xF) << 4) | (bfw_comp_meth & 0xF))   // bfwCompHdr (width 16 -> nibble 0)
    if bfw_comp_meth == 1 || bfw_comp_meth == 2 { w.byte(bfw_comp_param) }
    BitWriter bw = bit_writer()
    for k in 0..bfw_i.len() {
        bw.push(&!w, bfw_i[k], bfw_iq_width)
        bw.push(&!w, bfw_q[k], bfw_iq_width)
    }
    bw.align(&!w)
    se_pad4(&!w, 2)
    SecExt se = {}  se.ext_type = 1  se.ef = false  se.raw = w.take()
    return se
}
def find_se1(&Vec(SecExt) exts, int num_trx) -> Se1 {
    Se1 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 1 {
            if se.raw.len() >= 1 {
                r.present = true
                int hdr = se.raw.byte_at(0)
                int iw = (hdr >> 4) & 0xF
                if iw == 0 { iw = 16 }
                r.bfw_iq_width = iw
                r.bfw_comp_meth = hdr & 0xF
                int off = 1
                if r.bfw_comp_meth == 1 || r.bfw_comp_meth == 2 { r.bfw_comp_param = se.raw.byte_at(1)  off = 2 }
                if r.bfw_comp_meth <= 2 {                         // none/BFP/block-scaling: decode weights
                    BitReader br = bit_reader(se.raw.data, se.raw.len(), off)
                    for k in 0..num_trx {
                        r.bfw_i.push(br.take_signed(iw))
                        r.bfw_q.push(br.take_signed(iw))
                    }
                }
            }
        }
    }
    return r
}

// ---- SE11: flexible beamforming weights per PRB bundle (extType 0x0B, 16-bit
//  extLen). disableBFWs(1) RAD(1) bundleOffset(6) | numBundPrb(8) | bfwCompHdr(8),
//  then one bundle per group: bfwCompParam(var) + contInd(1)+beamId(15) + L*{bfwI,
//  bfwQ} (bit-packed, byte-aligned after each bundle). #bundles = ceil(numPrbc /
//  numBundPrb); L = num_trx. disableBFWs=1 omits the weights (beamId only).
struct BundleW { int bfw_comp_param; bool cont_ind; int beam_id; Vec(int) bfw_i; Vec(int) bfw_q }
struct Se11 { bool present; bool disable_bfws; int rad; int bundle_offset; int num_bund_prb;
              int bfw_iq_width; int bfw_comp_meth; Vec(BundleW) bundles }
def make_se11(bool disable_bfws, int rad, int bundle_offset, int num_bund_prb,
              int bfw_iq_width, int bfw_comp_meth, &Vec(BundleW) bundles) -> SecExt {
    Writer w = writer()
    w.byte((b2i(disable_bfws) << 7) | ((rad & 1) << 6) | (bundle_offset & 0x3F))
    w.byte(num_bund_prb & 0xFF)
    w.byte(((bfw_iq_width & 0xF) << 4) | (bfw_comp_meth & 0xF))
    for bi in 0..bundles.len() {
        BundleW bd = bundles[bi]
        if bfw_comp_meth == 1 || bfw_comp_meth == 2 { w.byte(bd.bfw_comp_param) }
        w.be_u16((b2i(bd.cont_ind) << 15) | (bd.beam_id & 0x7FFF))
        if !disable_bfws {
            BitWriter bw = bit_writer()
            for k in 0..bd.bfw_i.len() {
                bw.push(&!w, bd.bfw_i[k], bfw_iq_width)
                bw.push(&!w, bd.bfw_q[k], bfw_iq_width)
            }
            bw.align(&!w)
        }
    }
    se_pad4(&!w, 3)         // SE11 has a 3-byte header (ef/extType + 16-bit extLen)
    SecExt se = {}  se.ext_type = 11  se.ef = false  se.raw = w.take()
    return se
}
def find_se11(&Vec(SecExt) exts, int num_prbc, int num_trx) -> Se11 {
    Se11 r = {}
    for i in 0..exts.len() {
        SecExt se = exts[i]
        if se.ext_type == 11 {
            if se.raw.len() >= 3 {
                r.present = true
                int b0 = se.raw.byte_at(0)
                r.disable_bfws = ((b0 >> 7) & 1) != 0
                r.rad = (b0 >> 6) & 1
                r.bundle_offset = b0 & 0x3F
                r.num_bund_prb = se.raw.byte_at(1)
                int hdr = se.raw.byte_at(2)
                int iw = (hdr >> 4) & 0xF
                if iw == 0 { iw = 16 }
                r.bfw_iq_width = iw
                r.bfw_comp_meth = hdr & 0xF
                int nbund = 1
                if r.num_bund_prb > 0 { nbund = ceil_div(num_prbc, r.num_bund_prb) }
                int pos = 3
                for bi in 0..nbund {
                    BundleW bd = {}
                    if r.bfw_comp_meth == 1 || r.bfw_comp_meth == 2 { bd.bfw_comp_param = se.raw.byte_at(pos)  pos = pos + 1 }
                    int cb = (se.raw.byte_at(pos) << 8) | se.raw.byte_at(pos + 1)
                    bd.cont_ind = ((cb >> 15) & 1) != 0
                    bd.beam_id = cb & 0x7FFF
                    pos = pos + 2
                    if !r.disable_bfws && r.bfw_comp_meth <= 2 {
                        BitReader br = bit_reader(se.raw.data, se.raw.len(), pos)
                        for k in 0..num_trx {
                            bd.bfw_i.push(br.take_signed(iw))
                            bd.bfw_q.push(br.take_signed(iw))
                        }
                        br.align()
                        pos = br.pos()
                    }
                    r.bundles.push(bd)
                }
            }
        }
    }
    return r
}

// =====================================================================
//  Pass 3 — semantic resolution. Combine a section's base fields with its
//  decoded SEs into the EFFECTIVE allocation. Key point: SE6/SE12 REWRITE the
//  base symbol/PRB fields (spec 7.7.6: startSymbolId/rb/symInc/numSymbol "shall
//  not be used" when SE6 is present; symbolMask/rbgMask define the real set).
// =====================================================================
struct EffectiveAlloc { Vec(int) symbols; Vec(int) prbs; Vec(int) beams }

def ceil_div(int a, int b) -> int { return (a + b - 1) / b }

// SE6 rbgMask -> absolute PRB list via the f(n) RBG-size formula (spec 7.7.6.3)
def rbg_to_prbs(int start_prbc, int num_prbc, int rbg_size, int rbg_mask) -> Vec(int) {
    Vec(int) out = {}
    if rbg_size <= 0 { return out }     // rbgSize=0 (rb-bit) special-case not handled in v1
    int off0 = start_prbc % rbg_size
    int last = ceil_div(num_prbc + off0, rbg_size) - 1
    int f0 = rbg_size - off0
    int flast = (start_prbc + num_prbc) % rbg_size
    if flast == 0 { flast = rbg_size }
    for n in 0..(last + 1) {
        if ((rbg_mask >> n) & 1) != 0 {
            int lo = 0
            int cnt = 0
            if n == 0 { lo = start_prbc  cnt = f0 }
            else if n == last { lo = start_prbc + num_prbc - flast  cnt = flast }
            else { lo = start_prbc + f0 + (n - 1) * rbg_size  cnt = rbg_size }
            for j in 0..cnt { out.push(lo + j) }
        }
    }
    return out
}

// SE12 frequency ranges -> absolute PRB list (each: offStartPrb gap + numPrb run)
def ranges_to_prbs(int start_prbc, &Vec(int) off, &Vec(int) num) -> Vec(int) {
    Vec(int) out = {}
    int cur = start_prbc
    for r in 0..off.len() {
        cur = cur + off[r]
        for j in 0..num[r] { out.push(cur + j) }
        cur = cur + num[r]
    }
    return out
}

// resolve an ST1 section to its effective symbols / PRBs / beams.
//   start_symbol_id: from the C-plane common header (octet 12).
//   carrier_prbs:    cell PRB count (used when numPrbc=0 = all PRBs).
def resolve_st1_section(&SecType1 s, int start_symbol_id, int carrier_prbs) -> EffectiveAlloc {
    EffectiveAlloc r = {}
    Se6 e6 = find_se6(&s.exts)
    Se12 e12 = find_se12(&s.exts)
    int np = s.num_prbc
    if np == 0 { np = carrier_prbs }

    // --- symbols (SE6/SE12 symbolMask overrides startSymbolId/numSymbol) ---
    if e6.present {
        for n in 0..14 { if ((e6.symbol_mask >> n) & 1) != 0 { r.symbols.push(n) } }
    } else if e12.present {
        for n in 0..14 { if ((e12.symbol_mask >> n) & 1) != 0 { r.symbols.push(n) } }
    } else {
        for k in 0..s.num_symbol { r.symbols.push(start_symbol_id + k) }
    }

    // --- PRBs (SE6 rbgMask / SE12 ranges override startPrbc/numPrbc) ---
    if e6.present {
        r.prbs = rbg_to_prbs(s.start_prbc, np, e6.rbg_size, e6.rbg_mask)
    } else if e12.present {
        r.prbs = ranges_to_prbs(s.start_prbc, &e12.off_start_prb, &e12.num_prb)
    } else {
        int step = 1
        if s.rb { step = 2 }            // rb=1: every other PRB
        for k in 0..np { r.prbs.push(s.start_prbc + k * step) }
    }

    // --- beams (SE10 multi-port expansion) ---
    r.beams.push(s.beam_id)
    Se10 e10 = find_se10(&s.exts)
    if e10.present {
        if e10.beam_group_type == 1 {
            for k in 0..e10.num_portc { r.beams.push(s.beam_id + 1 + k) }   // consecutive beamIds
        } else if e10.beam_group_type == 2 {
            for k in 0..e10.ids.len() { r.beams.push(e10.ids[k]) }          // listed beam/ueIds
        }
        // type 0 (common): all numPortc ports share base beamId (already present)
    }
    return r
}

def parse_st9(&!Reader r, int iq_width, int comp_meth, int carrier_prbs) -> CplaneSt9 {
    CplaneSt9 m = {}
    m.iq_width = iq_width
    m.comp_meth = comp_meth
    m.ecpri = parse_ecpri(&!r)
    m.hdr = parse_cp_common(&!r)
    match r.be_u8() {                   // octet 15
        bits[3:nsinr][5:maskid] => { m.num_sinr_per_prb = nsinr; m.oru_ctrl_slot_mask_id = maskid }
        _ => {}
    }
    int reserved = r.be_u8() as int     // octet 16
    int w = iq_width
    if w == 0 { w = 16 }
    int nsec = m.hdr.num_sections
    for si in 0..nsec {
        SecType9 s = {}
        match r.be_u32() {              // section header octets 17..20
            bits[12:sid][1:rb][1:syi][10:sp][8:np] => {
                s.section_id = sid; s.rb = rb; s.sym_inc = syi; s.start_prbu = sp; s.num_prbu = np
            }
            _ => {}
        }
        int nprb = s.num_prbu
        if nprb == 0 { nprb = carrier_prbs }   // numPrbu=0 = all PRBs (spec 8.3.3.12)
        for pi in 0..nprb {
            SinrPrb prb = {}
            if comp_meth == 1 { prb.comp_param = r.be_u8() as int }   // byte-aligned
            BitReader br = bit_reader(r.buf.data, r.buf.len(), r.pos())
            for k in 0..m.num_sinr_per_prb { prb.sinr_raw.push(br.take_signed(w)) }
            br.align()
            r.seek(br.pos())            // sync Reader past the bit-packed PRB
            s.prbs.push(prb)
        }
        m.sections.push(s)
    }
    return m
}

methods CplaneSt9 {
    def write(&self, &!Writer w) {
        self.ecpri.write(&!w)
        self.hdr.write(&!w)
        w.byte((self.num_sinr_per_prb << 5) | self.oru_ctrl_slot_mask_id)   // octet 15
        w.byte(0)                                                            // octet 16 reserved
        int iw = self.iq_width
        if iw == 0 { iw = 16 }
        for s in &self.sections {
            w.be_u32((s.section_id << 20) | (b2i(s.rb) << 19) | (b2i(s.sym_inc) << 18)
                     | (s.start_prbu << 8) | s.num_prbu)
            for prb in &s.prbs {
                if self.comp_meth == 1 { w.byte(prb.comp_param) }
                BitWriter bw = bit_writer()
                for raw in prb.sinr_raw { bw.push(&!w, raw, iw) }
                bw.align(&!w)
            }
        }
    }
}

// =====================================================================
//  U-plane IQ data message (spec 8.3.2). Common header is only octets 9..12
//  (dir/pver/fidx, frameId, subframeId/slotId, slotId/symbolId) — there is NO
//  numberOfSections/sectionType, so sections run until the eCPRI payload ends
//  (payload_size locates the frame tail; build backpatches it).
//   Per section: sectionId(12) rb(1) symInc(1) startPrbu(10) numPrbu(8) [=32b],
//   optional udCompHdr(8)+reserved(8) (absent under static M-plane compression),
//   then numPrbu PRBs, each = optional udCompParam + 12 * {iSample, qSample}
//   (each iqWidth bits, two's-complement signed), byte-aligned.
// =====================================================================
// =====================================================================
//  Profile — the cell's M-plane meta-config that parsing needs. The wire format
//  is not fully self-describing: numPrbu/numPrbc = 0 means "all PRBs" (needs the
//  carrier PRB count), static compression omits udCompHdr (needs iqWidth/method),
//  SINR format is static. A Profile supplies all of that. Pick a default profile
//  for a common cell, or build one with profile()/ctx_cell().
// =====================================================================
struct Profile {
    int bw_mhz          // channel bandwidth (informative)
    int scs_khz         // sub-carrier spacing kHz (informative): 15/30/60/120
    int carrier_prbs    // total PRBs in the cell's SCS+bandwidth (e.g. 273 for 100 MHz, 30 kHz);
                        // required to parse sections that signal numPrbu/numPrbc = 0 ("all PRBs")
    int sinr_iq_width   // ST9 SINR sample width (M-plane), 0 means 16
    int sinr_comp_meth  // 0=uncompressed, 1=BFP
    int u_iq_width      // U-plane IQ width when static compression is configured
    int u_comp_meth     // U-plane comp method when static
    bool u_static       // true: no per-section udCompHdr on the wire (width/method from this profile)
}

// generic builder: a cell with `carrier_prbs` PRBs, dynamic U-plane compression
// (per-section udCompHdr on the wire) and uncompressed SINR.
def profile(int bw_mhz, int scs_khz, int carrier_prbs) -> Profile {
    return Profile{ bw_mhz: bw_mhz, scs_khz: scs_khz, carrier_prbs: carrier_prbs,
                    sinr_iq_width: 0, sinr_comp_meth: 0, u_iq_width: 0, u_comp_meth: 0, u_static: false }
}

// ---- default profiles (3GPP TS 38.101-1 FR1 / 36.101 transmission bandwidth) ----
def profile_nr_100mhz_scs30() -> Profile { return profile(100, 30, 273) }  // NR 100 MHz, mu=1 (headline)
def profile_nr_100mhz_scs60() -> Profile { return profile(100, 60, 135) }  // NR 100 MHz, mu=2
def profile_nr_50mhz_scs30()  -> Profile { return profile(50, 30, 133) }   // NR 50 MHz, mu=1
def profile_nr_50mhz_scs15()  -> Profile { return profile(50, 15, 270) }   // NR 50 MHz, mu=0
def profile_nr_20mhz_scs15()  -> Profile { return profile(20, 15, 106) }   // NR 20 MHz, mu=0
def profile_lte_20mhz()       -> Profile { return profile(20, 15, 100) }   // LTE 20 MHz

// ---- back-compat / minimal builders (no carrier PRB count) ----
def ctx(int sinr_iw, int sinr_cm) -> Profile {
    return Profile{ bw_mhz: 0, scs_khz: 0, carrier_prbs: 0,
                    sinr_iq_width: sinr_iw, sinr_comp_meth: sinr_cm,
                    u_iq_width: sinr_iw, u_comp_meth: 0, u_static: false }
}
def ctx_cell(int sinr_iw, int sinr_cm, int carrier_prbs) -> Profile {
    return Profile{ bw_mhz: 0, scs_khz: 0, carrier_prbs: carrier_prbs,
                    sinr_iq_width: sinr_iw, sinr_comp_meth: sinr_cm,
                    u_iq_width: sinr_iw, u_comp_meth: 0, u_static: false }
}

struct UplanePrb {
    int ud_comp_param    // 0/8/16 bits depending on comp method (BFP=exponent)
    Vec(int) i_samples   // 12 in-phase samples (signed)
    Vec(int) q_samples   // 12 quadrature samples (signed)
}
struct UplaneSection {
    int section_id
    bool rb
    bool sym_inc
    int start_prbu
    int num_prbu
    int ud_comp_hdr      // udIqWidth<<4 | udCompMeth (on wire unless static); 0 otherwise
    int comp_meth        // effective udCompMeth for this section (0=none,1=BFP,2=blockScale,3=u-law,4=modCompr)
    int iq_width         // effective IQ bit width (0 means 16)
    Vec(UplanePrb) prbs
}
struct UplaneMsg {
    EcpriHdr ecpri
    bool dir
    int payload_ver
    int filter_idx
    int frame_id
    int subframe_id
    int slot_id
    int symbol_id
    bool static_comp     // mirrors Profile.u_static for this message
    int iq_width         // effective IQ width used (for round-trip/build)
    int comp_meth
    Vec(UplaneSection) sections
}

def parse_uplane(&!Reader r, Profile ct) -> UplaneMsg {
    int frame_start = r.pos()
    UplaneMsg m = {}
    m.static_comp = ct.u_static
    m.ecpri = parse_ecpri(&!r)
    match r.be_u8() {                          // octet 9
        bits[1:d][3:pv][4:fi] => { m.dir = d; m.payload_ver = pv; m.filter_idx = fi }
        _ => {}
    }
    m.frame_id = r.be_u8() as int              // octet 10
    match r.be_u16() {                         // octets 11-12
        bits[4:sf][6:sl][6:sy] => { m.subframe_id = sf; m.slot_id = sl; m.symbol_id = sy }
        _ => {}
    }
    int frame_end = frame_start + 4 + m.ecpri.payload_size
    int eff_iw = ct.u_iq_width
    int eff_cm = ct.u_comp_meth
    while r.pos() < frame_end {
        UplaneSection s = {}
        match r.be_u32() {                     // section header (32 bits)
            bits[12:sid][1:rb][1:syi][10:sp][8:np] => {
                s.section_id = sid; s.rb = rb; s.sym_inc = syi; s.start_prbu = sp; s.num_prbu = np
            }
            _ => {}
        }
        int iw = eff_iw
        int cm = eff_cm
        if !ct.u_static {                      // per-section udCompHdr on the wire
            match r.be_u8() {
                bits[4:iqw][4:cmth] => { s.ud_comp_hdr = (iqw << 4) | cmth; iw = iqw; cm = cmth }
                _ => {}
            }
            int rsvd = r.be_u8() as int
        }
        if iw == 0 { iw = 16 }
        s.comp_meth = cm
        s.iq_width = iw
        // numPrbu = 0 means "all PRBs in the carrier" (NR >255-PRB case); use the
        // cell's carrier_prbs from the Profile (spec 8.3.3.12 / 7.5.3.6).
        int nprb = s.num_prbu
        if nprb == 0 { nprb = ct.carrier_prbs }
        for pp in 0..nprb {
            UplanePrb prb = {}
            // udCompParam: 1 byte for BFP/block-scaling/u-law; absent for none/mod-compr
            if cm == 1 || cm == 2 || cm == 3 { prb.ud_comp_param = r.be_u8() as int }
            BitReader br = bit_reader(r.buf.data, r.buf.len(), r.pos())
            for re in 0..12 {
                prb.i_samples.push(br.take_signed(iw))
                prb.q_samples.push(br.take_signed(iw))
            }
            br.align()
            r.seek(br.pos())
            s.prbs.push(prb)
        }
        m.sections.push(s)
    }
    return m
}

methods UplaneMsg {
    def write(&self, &!Writer w) {
        int frame_start = w.size()
        self.ecpri.write(&!w)                  // payload_size is backpatched below
        w.byte((b2i(self.dir) << 7) | (self.payload_ver << 4) | self.filter_idx)
        w.byte(self.frame_id)
        w.be_u16((self.subframe_id << 12) | (self.slot_id << 6) | self.symbol_id)
        int iw = self.iq_width
        if iw == 0 { iw = 16 }
        for s in &self.sections {
            w.be_u32((s.section_id << 20) | (b2i(s.rb) << 19) | (b2i(s.sym_inc) << 18)
                     | (s.start_prbu << 8) | s.num_prbu)
            if !self.static_comp {
                w.byte((iw & 0xF) << 4 | (self.comp_meth & 0xF))   // udCompHdr
                w.byte(0)                                          // reserved
            }
            for prb in &s.prbs {
                if self.comp_meth == 1 || self.comp_meth == 2 || self.comp_meth == 3 { w.byte(prb.ud_comp_param) }
                BitWriter bw = bit_writer()
                for re in 0..12 {
                    bw.push(&!w, prb.i_samples[re], iw)
                    bw.push(&!w, prb.q_samples[re], iw)
                }
                bw.align(&!w)
            }
        }
        // backpatch eCPRI payload_size = bytes after the 4-byte eCPRI common header
        w.patch_be_u16(frame_start + 2, w.size() - frame_start - 4)
    }
}

// =====================================================================
//  Packet — a parsed C-plane or U-plane message. Parsing a capture yields a
//  Vec(Packet); filtering and stats run over that.
// =====================================================================
enum Packet {
    CpSt1(CplaneSt1)
    CpSt9(CplaneSt9)
    Up(UplaneMsg)
}

// Parse one eCPRI frame. Dispatches on eCPRI message type (octet 2): 0x00 = IQ
// data (U-plane); otherwise C-plane, further split on sectionType (octet 14).
def parse_packet(&!Reader r, Profile ct) -> Packet {
    int start = r.pos()
    r.seek(start + 1)
    int msg = r.be_u8() as int
    r.seek(start)
    if msg == 0 { return Up(parse_uplane(&!r, ct)) }
    r.seek(start + 13)
    int stype = r.be_u8() as int
    r.seek(start)
    if stype == 9 { return CpSt9(parse_st9(&!r, ct.sinr_iq_width, ct.sinr_comp_meth, ct.carrier_prbs)) }
    return CpSt1(parse_st1(&!r))
}

// Parse a whole capture (back-to-back eCPRI frames) with an explicit Profile.
def parse_capture(&!Reader r, Profile ct) -> Vec(Packet) {
    Vec(Packet) out = {}
    while !r.eof?() { out.push(parse_packet(&!r, ct)) }
    return out
}

// Convenience wrapper: iq_width/comp_meth apply to ST9 SINR (and to U-plane when
// static compression is configured); U-plane defaults to per-section udCompHdr.
def parse_all(&!Reader r, int iq_width, int comp_meth) -> Vec(Packet) {
    return parse_capture(&!r, ctx(iq_width, comp_meth))
}

// =====================================================================
//  pcap reader — classic .pcap (tcpdump -w / Wireshark "Save As pcap").
//   global header(24B) + per-record(16B header + Ethernet frame). Each frame's
//   link layer (Ethernet [+ VLAN] [+ IPv4/UDP]) is stripped to the eCPRI start,
//   then parse_packet reads exactly one frame (it self-delimits).
// =====================================================================
def read_u32e(*u8 buf, int off, bool le) -> int {
    if le { return b.load_le_u32(buf, off) as int }
    return b.load_be_u32(buf, off) as int
}
// Ethernet (+ VLAN + IPv4/UDP) -> offset of the eCPRI message
def pcap_ecpri_offset(*u8 buf, int off) -> int {
    int et = b.load_be_u16(buf, off + 12) as int
    int p = off + 14
    if et == 0x8100 { et = b.load_be_u16(buf, p + 2) as int  p = p + 4 }   // 802.1Q VLAN
    if et == 0x0800 {                                                       // IPv4 -> UDP
        int ihl = ((b.load_u8(buf, p) as int) & 0x0F) * 4
        p = p + ihl + 8
    }
    return p     // et should now be 0xAEFE (eCPRI ethertype)
}
def from_pcap(&Str data, Profile prof) -> Vec(Packet) {
    Vec(Packet) out = {}
    int len = data.len()
    if len < 24 { return out }
    int m0 = data.byte_at(0)
    int m1 = data.byte_at(1)
    bool le = false
    bool ok = false
    if m0 == 0xa1 && m1 == 0xb2 { le = false  ok = true }        // big-endian pcap
    else if m0 == 0xd4 && m1 == 0xc3 { le = true  ok = true }    // little-endian pcap
    if !ok { return out }                                        // not a classic pcap
    int linktype = read_u32e(data.data, 20, le)
    Reader r = b.borrow(data.data, len)
    int pos = 24
    while pos + 16 <= len {
        int incl = read_u32e(data.data, pos + 8, le)
        int data_off = pos + 16
        if data_off + incl > len { return out }                 // truncated record
        int eoff = data_off
        if linktype == 1 { eoff = pcap_ecpri_offset(data.data, data_off) }
        r.seek(eoff)
        out.push(parse_packet(&!r, prof))
        pos = data_off + incl
    }
    return out
}

// =====================================================================
//  Input dispatch — auto-detect the format, or specify it explicitly.
// =====================================================================
enum InputFormat { Auto, Binary, Hex, Hexdump, Pcap }

def sniff_format(&Str data) -> InputFormat {
    if data.len() >= 4 {
        int b0 = data.byte_at(0)
        int b1 = data.byte_at(1)
        if (b0 == 0xa1 && b1 == 0xb2) || (b0 == 0xd4 && b1 == 0xc3) { return Pcap }
    }
    // Sample the head. Raw binary has non-printable bytes (eCPRI starts with 0x10) ->
    // Binary. Text with a ':' offset column -> Hexdump. Pure hex+whitespace -> Hex.
    int n = data.len()
    if n > 80 { n = 80 }
    bool is_text = true
    bool has_colon = false
    bool any_nonhex = false
    for i in 0..n {
        int ch = data.byte_at(i)
        if ch == 58 { has_colon = true }                        // ':'
        bool printable = (ch >= 32 && ch <= 126) || ch == 9 || ch == 10 || ch == 13
        if !printable { is_text = false }
        bool hexd = (ch >= 48 && ch <= 57) || (ch >= 97 && ch <= 102) || (ch >= 65 && ch <= 70)
        bool ws = ch == 32 || ch == 9 || ch == 10 || ch == 13
        if !hexd && !ws && ch != 58 && ch != 120 { any_nonhex = true }   // 58=':' 120='x'
    }
    if !is_text { return Binary }
    if has_colon { return Hexdump }
    if !any_nonhex { return Hex }
    return Binary
}

def read_input(&Str data, InputFormat fmt, Profile prof) -> Vec(Packet) {
    InputFormat f = fmt
    match f { Auto => { f = sniff_format(data) } _ => {} }
    match f {
        Pcap => { return from_pcap(data, prof) }
        Hexdump => { Reader r = from_hexdump(data)  return parse_capture(&!r, prof) }
        Hex => { Reader r = from_hex(data)  return parse_capture(&!r, prof) }
        _ => { Reader r = b.borrow(data.data, data.len())  return parse_capture(&!r, prof) }
    }
}

// ---- field accessors for filter closures (operate on parsed structs) ----
// section_type_of returns the C-plane Section Type; 256 marks a U-plane packet.
def section_type_of(&Packet p) -> int {
    match p {
        CpSt1(m) => { return m.hdr.section_type }
        CpSt9(m) => { return m.hdr.section_type }
        Up(m) => { return 256 }
    }
}
def is_uplane?(&Packet p) -> bool {
    match p { CpSt1(m) => { return false } CpSt9(m) => { return false } Up(m) => { return true } }
}
def eaxc_of(&Packet p) -> int {
    match p {
        CpSt1(m) => { return m.ecpri.rtcid_pcid }
        CpSt9(m) => { return m.ecpri.rtcid_pcid }
        Up(m) => { return m.ecpri.rtcid_pcid }
    }
}
def is_dl?(&Packet p) -> bool {
    match p { CpSt1(m) => { return m.hdr.dir } CpSt9(m) => { return m.hdr.dir } Up(m) => { return m.dir } }
}
def frame_id_of(&Packet p) -> int {
    match p {
        CpSt1(m) => { return m.hdr.frame_id }
        CpSt9(m) => { return m.hdr.frame_id }
        Up(m) => { return m.frame_id }
    }
}

// =====================================================================
//  Statistics. collect_sinr flattens every decoded SINR value from the ST9
//  packets in a (possibly pre-filtered) Vec(Packet); the generic reducers run
//  over the resulting Vec(f64). Headline: mean SINR by filter, e.g.
//     Vec(Packet) ue = pkts.filter(|p| oran.eaxc_of(p) == myEaxc)
//     f64 avg = oran.mean_sinr(ue)
// =====================================================================
// all decoded SINR values from one ST9 message (section/PRB/sub-band order)
def st9_sinr_values(&CplaneSt9 m) -> Vec(f64) {
    Vec(f64) out = {}
    for si in 0..m.sections.len() {
        SecType9 s = m.sections[si]
        for ppi in 0..s.prbs.len() {
            SinrPrb prb = s.prbs[ppi]
            for ki in 0..prb.sinr_raw.len() {
                out.push(sinr_decode(prb.sinr_raw[ki], prb.comp_param, m.comp_meth))
            }
        }
    }
    return out
}

def collect_sinr(&Vec(Packet) pkts) -> Vec(f64) {
    Vec(f64) out = {}
    int n = pkts.len()
    for pi in 0..n {
        Packet p = pkts[pi]
        match p {
            CpSt9(m) => {
                Vec(f64) vs = st9_sinr_values(m)
                for vi in 0..vs.len() { out.push(vs[vi]) }
            }
            _ => {}
        }
    }
    return out
}

def sumf(&Vec(f64) xs) -> f64 {
    f64 s = 0.0
    for i in 0..xs.len() { s = s + xs[i] }
    return s
}
def mean(&Vec(f64) xs) -> f64 {
    int n = xs.len()
    if n == 0 { return 0.0 }
    return sumf(xs) / (n as f64)
}
def minf(&Vec(f64) xs) -> f64 {
    int n = xs.len()
    if n == 0 { return 0.0 }
    f64 m = xs[0]
    for i in 1..n { if xs[i] < m { m = xs[i] } }
    return m
}
def maxf(&Vec(f64) xs) -> f64 {
    int n = xs.len()
    if n == 0 { return 0.0 }
    f64 m = xs[0]
    for i in 1..n { if xs[i] > m { m = xs[i] } }
    return m
}
// convenience: mean SINR over (a filtered set of) packets
def mean_sinr(&Vec(Packet) pkts) -> f64 {
    Vec(f64) xs = collect_sinr(pkts)
    return mean(xs)
}

// =====================================================================
//  Rendering — JSON (std.text.json), plain text, and an HTML table. All
//  operate on a (possibly filtered) Vec(Packet); to_json/to_text take a
//  single &Packet. Headline: parse -> filter -> to_json_all -> write file.
// =====================================================================
def dirstr(bool dl) -> Str { if dl { return "DL" } else { return "UL" } }

def to_json(&Packet p) -> JsonValue {
    JsonValue o = json.object_new()
    match p {
        CpSt1(m) => {
            o.set("type", json.str_val("ST1"))
            o.set("eaxc", json.number_int(m.ecpri.rtcid_pcid))
            o.set("dir", json.str_val(dirstr(m.hdr.dir)))
            o.set("frameId", json.number_int(m.hdr.frame_id))
            o.set("slotId", json.number_int(m.hdr.slot_id))
            JsonValue arr = json.array_new()
            for si in 0..m.sections.len() {
                SecType1 s = m.sections[si]
                JsonValue so = json.object_new()
                so.set("sectionId", json.number_int(s.section_id))
                so.set("startPrbc", json.number_int(s.start_prbc))
                so.set("numPrbc", json.number_int(s.num_prbc))
                so.set("beamId", json.number_int(s.beam_id))
                arr.push(so)
            }
            o.set("sections", arr)
        }
        CpSt9(m) => {
            o.set("type", json.str_val("ST9_SINR"))
            o.set("eaxc", json.number_int(m.ecpri.rtcid_pcid))
            o.set("dir", json.str_val(dirstr(m.hdr.dir)))
            o.set("numSinrPerPrb", json.number_int(m.num_sinr_per_prb))
            JsonValue arr = json.array_new()
            Vec(f64) vs = st9_sinr_values(m)
            for vi in 0..vs.len() { arr.push(json.number(vs[vi])) }
            o.set("sinr", arr)
        }
        Up(m) => {
            o.set("type", json.str_val("U_IQ"))
            o.set("eaxc", json.number_int(m.ecpri.rtcid_pcid))
            o.set("dir", json.str_val(dirstr(m.dir)))
            o.set("symbolId", json.number_int(m.symbol_id))
            o.set("sections", json.number_int(m.sections.len()))
        }
    }
    return o
}

def to_json_all(&Vec(Packet) pkts) -> Str {
    JsonValue arr = json.array_new()
    for i in 0..pkts.len() { arr.push(to_json(pkts[i])) }
    return json.stringify(arr)
}

def to_text(&Packet p) -> Str {
    match p {
        CpSt1(m) => {
            return f"ST1   eAxC={m.ecpri.rtcid_pcid} {dirstr(m.hdr.dir)} frame={m.hdr.frame_id} slot={m.hdr.slot_id} sections={m.sections.len()}"
        }
        CpSt9(m) => {
            Vec(f64) vs = st9_sinr_values(m)
            f64 avg = mean(vs)
            return f"ST9   eAxC={m.ecpri.rtcid_pcid} SINR mean={avg} count={vs.len()}"
        }
        Up(m) => {
            return f"U-IQ  eAxC={m.ecpri.rtcid_pcid} {dirstr(m.dir)} sym={m.symbol_id} sections={m.sections.len()}"
        }
    }
}

def to_text_all(&Vec(Packet) pkts) -> Str {
    Str out = ""
    for i in 0..pkts.len() {
        out = out + to_text(pkts[i]) + "\n"
    }
    return out
}

// ---- formatting helpers for the detailed view ----
def istr(int n) -> Str { return f"{n}" }
// numPrbc/numPrbu: 0 has the special meaning "all PRBs in the carrier" (spec 7.5.3.6 / 8.3.3.12)
def prbn(int n) -> Str { if n == 0 { return "0 (all)" } else { return f"{n}" } }
def bstr(bool x) -> Str { if x { return "true" } else { return "false" } }
def hex8(int v) -> Str {
    Str d = "0123456789abcdef"
    return "0x" + d.substr((v >> 4) & 0xF, 1) + d.substr(v & 0xF, 1)
}
def hex16(int v) -> Str {
    Str d = "0123456789abcdef"
    Str out = "0x"
    for k in 0..4 { out = out + d.substr((v >> ((3 - k) * 4)) & 0xF, 1) }
    return out
}
def intlist(&Vec(int) xs) -> Str {
    Str s = "["
    for i in 0..xs.len() { if i > 0 { s = s + ", " } s = s + istr(xs[i]) }
    return s + "]"
}
// ORAN-spec-style octet layout table: 8 bit columns (msb..lsb) + bytes + octet.
// Each field is a cell spanning its bit width (colspan); split fields show their
// value on the most-significant octet piece. Variable IQ/SINR samples are listed
// below the table (raw + decoded), one block per PRB.
def oc_cell(Str label, int bits) -> Str { return f"<td colspan='{bits}' class='c'>{label}</td>" }
def oc_cellv(Str label, Str val, int bits) -> Str {
    return f"<td colspan='{bits}' class='c'>{label}<span class='cv'>{val}</span></td>"
}
def oc_row(Str cells, Str nb, Str oct) -> Str { return f"<tr>{cells}<td class='nb'>{nb}</td><td class='oc'>{oct}</td></tr>" }
def oc_head(Str title) -> Str {
    return f"<table class='ot'><tr><td colspan='10' class='tt'>{title}</td></tr><tr class='bh'><td>0<span class='ms'>msb</span></td><td>1</td><td>2</td><td>3</td><td>4</td><td>5</td><td>6</td><td>7<span class='ms'>lsb</span></td><td>bytes</td><td>octet</td></tr>"
}

// the 5 eCPRI octet rows (octets 1..8), identical for every message
def oc_ecpri(&EcpriHdr e) -> Str {
    Str s = oc_row(oc_cellv("ecpriVersion", istr(e.version), 4) + oc_cellv("ecpriReserved", "0", 3) + oc_cellv("concat", bstr(e.concat), 1), "1", "1")
    s = s + oc_row(oc_cellv("ecpriMessage", hex8(e.msg_type), 8), "1", "2")
    s = s + oc_row(oc_cellv("ecpriPayload", istr(e.payload_size), 8), "2", "3")
    s = s + oc_row(oc_cellv("ecpriRtcid/Pcid", hex16(e.rtcid_pcid), 8), "2", "5")
    s = s + oc_row(oc_cellv("ecpriSeqid", hex16(e.seqid), 8), "2", "7")
    return s
}
// common header octets 9..13 (octet 12's 6 lsbs are named by `sym_label`)
def oc_common(&CpCommonHdr h, Str sym_label) -> Str {
    Str s = oc_row(oc_cellv("dataDir", dirstr(h.dir), 1) + oc_cellv("payloadVersion", istr(h.payload_ver), 3) + oc_cellv("filterIndex", istr(h.filter_idx), 4), "1", "9")
    s = s + oc_row(oc_cellv("frameId", istr(h.frame_id), 8), "1", "10")
    s = s + oc_row(oc_cellv("subframeId", istr(h.subframe_id), 4) + oc_cellv("slotId", istr(h.slot_id), 4), "1", "11")
    s = s + oc_row(oc_cell("slotId", 2) + oc_cellv(sym_label, istr(h.start_symbol_id), 6), "1", "12")
    s = s + oc_row(oc_cellv("numberOfSections", istr(h.num_sections), 8), "1", "13")
    s = s + oc_row(oc_cellv("sectionType", istr(h.section_type), 8), "1", "14")
    return s
}
// a 32-bit "sectionId(12) rb(1) symInc(1) startPrbX(10) numPrbX(8)" header (ST9/U-plane)
def oc_sec32(int sid, bool rb, bool si, int sp, int np, Str prb_label, Str o0) -> Str {
    Str s = oc_row(oc_cellv("sectionId", istr(sid), 8), "1", o0)
    s = s + oc_row(oc_cell("sectionId", 4) + oc_cellv("rb", bstr(rb), 1) + oc_cellv("symInc", bstr(si), 1) + oc_cellv(prb_label, istr(sp), 2), "1", "+1")
    s = s + oc_row(oc_cell(prb_label, 8), "1", "+2")
    s = s + oc_row(oc_cellv(np_label(prb_label), prbn(np), 8), "1", "+3")
    return s
}
def np_label(Str prb_label) -> Str { if prb_label.eq?("startPrbu") { return "numPrbu" } else { return "numPrbc" } }

def to_html_detail(&Vec(Packet) pkts) -> Str {
    Str out = "<div class='oran'>"
    for i in 0..pkts.len() {
        Packet p = pkts[i]
        match p {
            CpSt1(m) => {
                out = out + "<div class='pkt'><div class='ptype'>C-Plane · Section Type 1 (DL/UL control) — eAxC " + hex16(m.ecpri.rtcid_pcid) + "</div>"
                out = out + oc_head("Section Type 1: DL/UL control")
                out = out + oc_ecpri(&m.ecpri) + oc_common(&m.hdr, "startSymbolId")
                out = out + oc_row(oc_cellv("udCompHdr", hex8(m.ud_comp_hdr), 8), "1", "15")
                out = out + oc_row(oc_cell("reserved", 8), "1", "16")
                for si in 0..m.sections.len() {
                    SecType1 s = m.sections[si]
                    out = out + oc_row(oc_cellv("sectionId", istr(s.section_id), 8), "1", "17")
                    out = out + oc_row(oc_cell("sectionId", 4) + oc_cellv("rb", bstr(s.rb), 1) + oc_cellv("symInc", bstr(s.sym_inc), 1) + oc_cellv("startPrbc", istr(s.start_prbc), 2), "1", "18")
                    out = out + oc_row(oc_cell("startPrbc", 8), "1", "19")
                    out = out + oc_row(oc_cellv("numPrbc", prbn(s.num_prbc), 8), "1", "20")
                    out = out + oc_row(oc_cellv("reMask", hex16(s.re_mask), 8), "1", "21")
                    out = out + oc_row(oc_cell("reMask", 4) + oc_cellv("numSymbol", istr(s.num_symbol), 4), "1", "22")
                    out = out + oc_row(oc_cellv("ef", bstr(s.ef), 1) + oc_cellv("beamId", istr(s.beam_id), 7), "1", "23")
                    out = out + oc_row(oc_cell("beamId", 8), "1", "24")
                    for ei in 0..s.exts.len() {
                        SecExt ex = s.exts[ei]
                        out = out + oc_row(oc_cellv(f"Section Extension {ex.ext_type}", istr(ex.raw.len()) + "B, ef=" + bstr(ex.ef), 8), "var", "SE")
                    }
                }
                out = out + "</table></div>"
            }
            CpSt9(m) => {
                out = out + "<div class='pkt'><div class='ptype'>C-Plane · Section Type 9 (SINR report) — eAxC " + hex16(m.ecpri.rtcid_pcid) + "</div>"
                out = out + oc_head("Section Type 9: SINR reporting")
                out = out + oc_ecpri(&m.ecpri) + oc_common(&m.hdr, "symbolId")
                out = out + oc_row(oc_cellv("numSinrPerPrb", istr(m.num_sinr_per_prb), 3) + oc_cellv("oruControlSinrSlotMaskId", istr(m.oru_ctrl_slot_mask_id), 5), "1", "15")
                out = out + oc_row(oc_cell("reserved", 8), "1", "16")
                for si in 0..m.sections.len() {
                    SecType9 s = m.sections[si]
                    out = out + oc_sec32(s.section_id, s.rb, s.sym_inc, s.start_prbu, s.num_prbu, "startPrbu", "17")
                }
                out = out + "</table>"
                for si in 0..m.sections.len() {
                    SecType9 s = m.sections[si]
                    out = out + f"<div class='iq'><b>section [{si}]</b> SINR values (compMeth=" + comp_name(m.comp_meth) + ")"
                    for pi in 0..s.prbs.len() {
                        SinrPrb prb = s.prbs[pi]
                        out = out + f"<div class='ir'><span class='il'>prb[{pi}] raw</span><span class='iv'>" + intlist(&prb.sinr_raw) + "</span></div>"
                        if m.comp_meth != 0 {
                            Str dec = ""
                            for ki in 0..prb.sinr_raw.len() { if ki > 0 { dec = dec + ", " } dec = dec + istr(iq_decode(prb.sinr_raw[ki], prb.comp_param, m.comp_meth) as int) }
                            out = out + f"<div class='ir'><span class='il'>prb[{pi}] decoded</span><span class='iv'>[" + dec + "]  (compParam=" + istr(prb.comp_param) + ")</span></div>"
                        }
                    }
                    out = out + "</div>"
                }
                out = out + "</div>"
            }
            Up(m) => {
                out = out + "<div class='pkt'><div class='ptype'>U-Plane · IQ data — eAxC " + hex16(m.ecpri.rtcid_pcid) + "</div>"
                out = out + oc_head("U-Plane: IQ data")
                out = out + oc_ecpri(&m.ecpri)
                out = out + oc_row(oc_cellv("dataDir", dirstr(m.dir), 1) + oc_cellv("payloadVersion", istr(m.payload_ver), 3) + oc_cellv("filterIndex", istr(m.filter_idx), 4), "1", "9")
                out = out + oc_row(oc_cellv("frameId", istr(m.frame_id), 8), "1", "10")
                out = out + oc_row(oc_cellv("subframeId", istr(m.subframe_id), 4) + oc_cellv("slotId", istr(m.slot_id), 4), "1", "11")
                out = out + oc_row(oc_cell("slotId", 2) + oc_cellv("symbolId", istr(m.symbol_id), 6), "1", "12")
                for si in 0..m.sections.len() {
                    UplaneSection s = m.sections[si]
                    out = out + oc_sec32(s.section_id, s.rb, s.sym_inc, s.start_prbu, s.num_prbu, "startPrbu", "13")
                    if s.comp_meth != 0 || s.ud_comp_hdr != 0 {
                        out = out + oc_row(oc_cellv("udIqWidth", istr(s.iq_width), 4) + oc_cellv("udCompMeth", comp_name(s.comp_meth), 4), "1", "udC")
                    }
                }
                out = out + "</table>"
                for si in 0..m.sections.len() {
                    UplaneSection s = m.sections[si]
                    out = out + f"<div class='iq'><b>section [{si}]</b> IQ samples (compMeth=" + comp_name(s.comp_meth) + ", iqWidth=" + istr(s.iq_width) + ")"
                    for pi in 0..s.prbs.len() {
                        UplanePrb prb = s.prbs[pi]
                        if s.comp_meth == 1 || s.comp_meth == 2 {     // show the per-PRB compression parameter
                            out = out + f"<div class='ir'><span class='il'>prb[{pi}] udCompParam</span><span class='iv'>" + compparam_str(prb.ud_comp_param, s.comp_meth) + "</span></div>"
                        }
                        out = out + f"<div class='ir'><span class='il'>prb[{pi}] raw (I,Q)</span><span class='iv'>" + iq_raw_pairs(&prb.i_samples, &prb.q_samples) + "</span></div>"
                        if s.comp_meth == 1 || s.comp_meth == 2 {     // BFP / block-scaling decode from udCompParam alone
                            out = out + f"<div class='ir'><span class='il'>prb[{pi}] decoded (I,Q)</span><span class='iv'>" + iq_dec_pairs(&prb.i_samples, &prb.q_samples, prb.ud_comp_param, s.comp_meth) + "</span></div>"
                        }
                        if s.comp_meth == 4 {                          // mod-compr decode needs SE4 (cross-plane)
                            out = out + f"<div class='ir'><span class='il'>prb[{pi}] decoded</span><span class='iv'>needs SE4 modCompScaler (see C-plane) — use mod_decompress()</span></div>"
                        }
                    }
                    out = out + "</div>"
                }
                out = out + "</div>"
            }
        }
    }
    return out + "</div>"
}

def to_html_all(&Vec(Packet) pkts) -> Str {
    Str out = "<table>\n<tr><th>type</th><th>eAxC</th><th>dir</th><th>detail</th></tr>\n"
    for i in 0..pkts.len() {
        Packet p = pkts[i]
        match p {
            CpSt1(m) => {
                out = out + f"<tr><td>ST1</td><td>{m.ecpri.rtcid_pcid}</td><td>{dirstr(m.hdr.dir)}</td><td>{m.sections.len()} section(s)</td></tr>\n"
            }
            CpSt9(m) => {
                Vec(f64) vs = st9_sinr_values(m)
                f64 avg = mean(vs)
                out = out + f"<tr><td>ST9</td><td>{m.ecpri.rtcid_pcid}</td><td>{dirstr(m.hdr.dir)}</td><td>mean SINR={avg} ({vs.len()} vals)</td></tr>\n"
            }
            Up(m) => {
                out = out + f"<tr><td>U-IQ</td><td>{m.ecpri.rtcid_pcid}</td><td>{dirstr(m.dir)}</td><td>{m.sections.len()} section(s)</td></tr>\n"
            }
        }
    }
    out = out + "</table>"
    return out
}

// =====================================================================
//  hex dump — re-serialize a packet and show it as bytes.
//   to_hex:       xxd-style (offset + 16 hex/row).
//   to_annotated: one line per logical field group (offset, byte len, decoded).
//  Both rebuild the wire bytes via the build path, so they round-trip the parse.
//  NOTE: rows are built with f-strings (linear lowering), never long `a + b + c`
//  chains — Str `+` operator-overload checking is exponential past ~22 operands.
// =====================================================================
def packet_bytes(&Packet p) -> Str {
    Writer w = writer()
    match p {
        CpSt1(m) => { m.write(&!w) }
        CpSt9(m) => { m.write(&!w) }
        Up(m) => { m.write(&!w) }
    }
    return w.take()
}
def hex8b(int v) -> Str {
    Str d = "0123456789abcdef"
    return d.substr((v >> 4) & 0xF, 1) + d.substr(v & 0xF, 1)
}
def hexdump_str(&Str bytes) -> Str {
    Str out = ""
    int n = bytes.len()
    int off = 0
    while off < n {
        out = out + hex16(off) + "  "
        for j in 0..16 {
            if off + j < n { out = out + hex8b(bytes.byte_at(off + j)) + " " }
            else { out = out + "   " }
            if j == 7 { out = out + " " }
        }
        out = out + "\n"
        off = off + 16
    }
    return out
}
def to_hex(&Packet p) -> Str {
    Str bytes = packet_bytes(p)
    return hexdump_str(&bytes)
}
def to_hex_all(&Vec(Packet) pkts) -> Str {
    Str out = ""
    for i in 0..pkts.len() { out = out + f"--- packet[{i}] ---\n" + to_hex(pkts[i]) }
    return out
}

// one annotation line: "0x0008  +6B  commonHdr: <decoded>"
def ann(Str name, int off, int len, Str detail) -> Str {
    return hex16(off) + "  +" + istr(len) + "B  " + name + ": " + detail + "\n"
}
def to_annotated(&Packet p) -> Str {
    match p {
        CpSt1(m) => {
            Str s = ""
            int off = 0
            s = s + ann("eCPRI", off, 8, f"ver={m.ecpri.version} concat={bstr(m.ecpri.concat)} msg={hex8(m.ecpri.msg_type)} payloadSize={m.ecpri.payload_size} eAxC={hex16(m.ecpri.rtcid_pcid)} seqId={hex16(m.ecpri.seqid)}")
            off = off + 8
            s = s + ann("commonHdr", off, 6, f"{dirstr(m.hdr.dir)} payloadVer={m.hdr.payload_ver} filterIdx={m.hdr.filter_idx} frameId={m.hdr.frame_id} subframeId={m.hdr.subframe_id} slotId={m.hdr.slot_id} startSymbolId={m.hdr.start_symbol_id} numSections={m.hdr.num_sections} sectionType={m.hdr.section_type}")
            off = off + 6
            s = s + ann("udCompHdr", off, 2, f"{hex8(m.ud_comp_hdr)} + reserved(1B)")
            off = off + 2
            for si in 0..m.sections.len() {
                SecType1 sec = m.sections[si]
                Writer sw = writer()
                sec.write(&!sw)
                int slen = sw.take().len()
                s = s + ann(f"section[{si}]", off, 8, f"id={sec.section_id} rb={bstr(sec.rb)} symInc={bstr(sec.sym_inc)} startPrbc={sec.start_prbc} numPrbc={prbn(sec.num_prbc)} reMask={hex16(sec.re_mask)} numSymbol={sec.num_symbol} ef={bstr(sec.ef)} beamId={sec.beam_id}")
                if sec.ef && sec.exts.len() > 0 {
                    s = s + ann(f"  +SExt x{sec.exts.len()}", off + 8, slen - 8, "section extensions (TLV chain)")
                }
                off = off + slen
            }
            return s
        }
        CpSt9(m) => {
            Str s = ""
            int off = 0
            s = s + ann("eCPRI", off, 8, f"ver={m.ecpri.version} msg={hex8(m.ecpri.msg_type)} payloadSize={m.ecpri.payload_size} eAxC={hex16(m.ecpri.rtcid_pcid)} seqId={hex16(m.ecpri.seqid)}")
            off = off + 8
            s = s + ann("commonHdr", off, 6, f"{dirstr(m.hdr.dir)} frameId={m.hdr.frame_id} subframeId={m.hdr.subframe_id} slotId={m.hdr.slot_id} symbolId={m.hdr.start_symbol_id} numSections={m.hdr.num_sections} sectionType=9")
            off = off + 6
            s = s + ann("sinrCfg", off, 1, f"numSinrPerPrb={m.num_sinr_per_prb} oruCtrlSlotMaskId={m.oru_ctrl_slot_mask_id} iqWidth={m.iq_width} compMeth={m.comp_meth}")
            off = off + 1
            for si in 0..m.sections.len() {
                SecType9 sec = m.sections[si]
                f64 avg = st9_section_sinr(&sec, m.comp_meth)
                s = s + ann(f"section[{si}]", off, 4, f"id={sec.section_id} startPrbu={sec.start_prbu} numPrbu={prbn(sec.num_prbu)} prbs={sec.prbs.len()} meanSINR={avg}")
                off = off + 4
            }
            return s
        }
        Up(m) => {
            Str s = ""
            int off = 0
            s = s + ann("eCPRI", off, 8, f"ver={m.ecpri.version} msg={hex8(m.ecpri.msg_type)} payloadSize={m.ecpri.payload_size} eAxC={hex16(m.ecpri.rtcid_pcid)} seqId={hex16(m.ecpri.seqid)}")
            off = off + 8
            s = s + ann("dataHdr", off, 4, f"{dirstr(m.dir)} payloadVer={m.payload_ver} filterIdx={m.filter_idx} frameId={m.frame_id} subframeId={m.subframe_id} slotId={m.slot_id} symbolId={m.symbol_id}")
            off = off + 4
            for si in 0..m.sections.len() {
                UplaneSection sec = m.sections[si]
                s = s + ann(f"u-section[{si}]", off, 4, f"id={sec.section_id} startPrbu={sec.start_prbu} numPrbu={prbn(sec.num_prbu)} iqWidth={sec.iq_width} compMeth={sec.comp_meth} prbs={sec.prbs.len()}")
                off = off + 4
            }
            return s
        }
    }
}
def to_annotated_all(&Vec(Packet) pkts) -> Str {
    Str out = ""
    for i in 0..pkts.len() { out = out + f"--- packet[{i}] ---\n" + to_annotated(pkts[i]) }
    return out
}

// =====================================================================
//  CSV dump — one row per section, column union across all packet types.
//  Empty cells where a column does not apply. Ideal for Excel/pandas stats.
// =====================================================================
def st9_section_sinr(&SecType9 s, int comp_meth) -> f64 {
    Vec(f64) vs = {}
    for ppi in 0..s.prbs.len() {
        SinrPrb prb = s.prbs[ppi]
        for ki in 0..prb.sinr_raw.len() {
            vs.push(sinr_decode(prb.sinr_raw[ki], prb.comp_param, comp_meth))
        }
    }
    return mean(vs)
}
def to_csv(&Vec(Packet) pkts) -> Str {
    Str out = "type,eaxc,dir,frameId,subframe,slot,symbol,sectionType,sectionId,startPrb,numPrb,beamId,sinrMean,iqWidth,compMeth\n"
    for i in 0..pkts.len() {
        Packet p = pkts[i]
        match p {
            CpSt1(m) => {
                for si in 0..m.sections.len() {
                    SecType1 s = m.sections[si]
                    out = out + f"ST1,{m.ecpri.rtcid_pcid},{dirstr(m.hdr.dir)},{m.hdr.frame_id},{m.hdr.subframe_id},{m.hdr.slot_id},{m.hdr.start_symbol_id},1,{s.section_id},{s.start_prbc},{s.num_prbc},{s.beam_id},,,\n"
                }
            }
            CpSt9(m) => {
                for si in 0..m.sections.len() {
                    SecType9 s = m.sections[si]
                    f64 avg = st9_section_sinr(&s, m.comp_meth)
                    out = out + f"ST9,{m.ecpri.rtcid_pcid},{dirstr(m.hdr.dir)},{m.hdr.frame_id},{m.hdr.subframe_id},{m.hdr.slot_id},{m.hdr.start_symbol_id},9,{s.section_id},{s.start_prbu},{s.num_prbu},,{avg},{m.iq_width},{m.comp_meth}\n"
                }
            }
            Up(m) => {
                for si in 0..m.sections.len() {
                    UplaneSection s = m.sections[si]
                    out = out + f"U-IQ,{m.ecpri.rtcid_pcid},{dirstr(m.dir)},{m.frame_id},{m.subframe_id},{m.slot_id},{m.symbol_id},,{s.section_id},{s.start_prbu},{s.num_prbu},,,{s.iq_width},{s.comp_meth}\n"
                }
            }
        }
    }
    return out
}
