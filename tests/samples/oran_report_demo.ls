// oran_report_demo.ls — build a realistic mixed capture, parse it, and write a
// styled standalone HTML report to C:/tmp/oran_report.html (also prints JSON+text).
import std.sys.c as c
import std.text.bytes as b
import std.sys.io as io
import oran.cus as oran

def st1(&!Writer w, int eaxc, int beam, int frame, int slot) {
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: frame,
        subframe_id: 0, slot_id: slot, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    m.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 100, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: beam })
    m.write(&!w)
}

def st9(&!Writer w, int eaxc, int v0, int v1) {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 11,
        subframe_id: 0, slot_id: 0, start_symbol_id: 3, num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 1  m.oru_ctrl_slot_mask_id = 1  m.iq_width = 8  m.comp_meth = 0
    SecType9 s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    SinrPrb p0 = {}  p0.sinr_raw.push(v0)
    SinrPrb p1 = {}  p1.sinr_raw.push(v1)
    s.prbs.push(p0)  s.prbs.push(p1)
    m.sections.push(s)
    m.write(&!w)
}

def uplane(&!Writer w, int eaxc, int iqw, int cm, int cp) {
    UplaneMsg m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: eaxc, seqid: 9 }
    m.dir = true  m.payload_ver = 1  m.filter_idx = 0
    m.frame_id = 11  m.subframe_id = 0  m.slot_id = 0  m.symbol_id = 2
    m.static_comp = false  m.iq_width = iqw  m.comp_meth = cm
    UplaneSection s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 1
    UplanePrb prb = {}
    prb.ud_comp_param = cp
    for re in 0..12 { prb.i_samples.push(re) ; prb.q_samples.push(0 - re) }
    s.prbs.push(prb)
    m.sections.push(s)
    m.write(&!w)
}

def style() -> Str {
    return "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#1a1a1a;background:#f7f8fa}h1{font-size:20px}.sum{background:#f0f6ff;border:1px solid #cfe0ff;border-radius:8px;padding:12px 16px;margin:12px 0}.pkt{background:#fff;border:1px solid #e3e6ea;border-radius:10px;padding:14px 18px;margin:14px 0}.ptype{font-weight:600;font-size:15px;color:#2b3a55;margin-bottom:8px}.ot{width:720px;max-width:100%;border-collapse:collapse;margin:6px 0;font-size:12px;table-layout:fixed}.ot td{border:1px solid #b8c0cc;padding:3px 4px;text-align:center;vertical-align:middle;word-break:break-word}.tt{background:#c0392b;color:#fff;font-weight:600;font-size:13px}.bh td{background:#1f3864;color:#fff;font-weight:600}.ms{font-size:9px;font-weight:400;opacity:.75;margin-left:3px}.c{background:#fff}.cv{display:block;color:#1f6fc0;font-family:Consolas,monospace;font-size:11px;margin-top:1px}.nb,.oc{background:#eef1f4;color:#5f6b7a;white-space:nowrap}.iq{font-size:12px;margin:2px 0 12px;color:#333}.iq b{color:#2b3a55}.ir{display:flex;gap:12px;padding:2px 0}.il{color:#5f6b7a;min-width:150px;flex:none}.iv{font-family:Consolas,monospace;word-break:break-all}</style>"
}

def bs(bool x) -> Str { if x { return "1" } else { return "0" } }

// Demonstrate DL modulation compression: a C-plane ST1 carrying SE4 (csf +
// modCompScaler) and a U-plane mod-compr packet; extract SE4 then decompress.
def modcomp_block() -> Str {
    CplaneSt1 cm = {}
    cm.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 0, rtcid_pcid: 0x010A, seqid: 1 }
    cm.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 11, subframe_id: 0, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    cm.ud_comp_hdr = 0x34     // udIqWidth=3 (64QAM), udCompMeth=4 (modulation compression)
    SecType1 sx = {}
    sx.section_id = 1  sx.num_prbc = 24  sx.re_mask = 0xFFF  sx.num_symbol = 14  sx.beam_id = 1  sx.ef = true
    sx.exts.push(oran.make_se4(true, 0, 1448))     // csf=1, modCompScaler = 1448/2048 = 0.707
    cm.sections.push(sx)
    Writer wc = oran.writer()
    cm.write(&!wc)
    Reader rc = wc.as_reader()
    CplaneSt1 cm2 = oran.parse_st1(&!rc)
    SecType1 sec0 = cm2.sections[0]
    ModComp mc = oran.find_mod_comp(&sec0.exts)

    UplaneMsg um = {}
    um.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: 0x010A, seqid: 2 }
    um.dir = true  um.payload_ver = 1  um.filter_idx = 0  um.frame_id = 11  um.subframe_id = 0  um.slot_id = 0  um.symbol_id = 0
    um.static_comp = false  um.iq_width = 3  um.comp_meth = 4    // 64QAM (3 bits/axis)
    UplaneSection us = {}  us.section_id = 1  us.start_prbu = 0  us.num_prbu = 1
    UplanePrb pr = {}
    // 64QAM 3-bit shifted samples; I sweeps the 8 levels -4..3, Q steps across them
    for re in 0..12 { pr.i_samples.push((re % 8) - 4)  pr.q_samples.push(((re / 2) % 8) - 4) }
    us.prbs.push(pr)
    um.sections.push(us)
    Writer wu = oran.writer()
    um.write(&!wu)
    Reader ru = wu.as_reader()
    UplaneMsg um2 = oran.parse_uplane(&!ru, oran.ctx(0, 0))
    UplaneSection us2 = um2.sections[0]
    UplanePrb p2 = us2.prbs[0]

    Str s = "<div class='pkt'><div class='ptype'>Modulation compression (DL) — eAxC 0x010A · cross-plane decode</div><div class='iq'>"
    s = s + "<div class='ir'><span class='il'>modulation</span><span class='iv'>" + oran.mod_scheme(us2.iq_width) + f" (udIqWidth={us2.iq_width})</span></div>"
    s = s + "<div class='ir'><span class='il'>C-plane SE4</span><span class='iv'>csf=" + bs(mc.csf) + ", modCompScaler=" + f"{mc.scaler}" + " (exp=0, mantissa=1448)</span></div>"
    s = s + "<div class='ir'><span class='il'>U-plane raw (I,Q) 3-bit</span><span class='iv'>" + oran.iq_raw_pairs(&p2.i_samples, &p2.q_samples) + "</span></div>"
    s = s + "<div class='ir'><span class='il'>constellation (I,Q) — csf removed</span><span class='iv'>" + oran.mod_const_pairs(&p2.i_samples, &p2.q_samples, us2.iq_width, mc.csf) + "</span></div>"
    s = s + "<div class='ir'><span class='il'>decompressed (I,Q) norm.</span><span class='iv'>" + oran.mod_dec_pairs(&p2.i_samples, &p2.q_samples, us2.iq_width, &mc) + "</span></div>"
    return s + "</div></div>"
}

def main() {
    Writer w = oran.writer()
    st1(&!w, 0x0102, 7, 11, 1)      // DL control, beam 7
    st1(&!w, 0x0108, 12, 11, 1)     // DL control, beam 12
    st9(&!w, 0x0103, 18, 22)        // UE A SINR: mean 20
    st9(&!w, 0x0104, 10, 14)        // UE B SINR: mean 12
    st9(&!w, 0x0105, 25, 27)        // UE C SINR: mean 26
    uplane(&!w, 0x0102, 16, 0, 0)   // U-plane IQ, 16-bit uncompressed
    uplane(&!w, 0x0106, 9, 1, 3)    // U-plane IQ, 9-bit BFP, exponent 3 (decoded = raw x 8)

    Reader r = w.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)

    Vec(Packet) sinr = pkts.filter(|p| oran.section_type_of(p) == 9)
    f64 avg = oran.mean_sinr(sinr)
    Vec(f64) vals = oran.collect_sinr(pkts)

    // assemble a standalone HTML report
    Str html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>O-RAN Capture Report</title>"
    html = html + style() + "</head><body>"
    html = html + "<h1>O-RAN Fronthaul Capture Report</h1>"
    html = html + f"<div class='sum'><b>Packets:</b> {pkts.len()} &nbsp;|&nbsp; <b>SINR reports (ST9):</b> {sinr.len()} &nbsp;|&nbsp; <b>Mean SINR:</b> {avg} dB &nbsp;|&nbsp; <b>SINR min/max:</b> {oran.minf(vals)} / {oran.maxf(vals)} dB</div>"
    html = html + oran.to_html_detail(pkts)
    html = html + modcomp_block()
    html = html + "</body></html>"

    io.write_file("C:/tmp/oran_report.html", html)
    @print("wrote C:/tmp/oran_report.html")
    @print("--- text ---")
    @print(oran.to_text_all(pkts))
    @print("--- json ---")
    @print(oran.to_json_all(sinr))
}
