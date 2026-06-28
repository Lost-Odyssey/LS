// sim.htmlview — HTML renderer over the SAME analysis data the text views use (§7.6).
//
// The text views (regview / movement) and this module are two renderers over one
// analysis: you feed the library an instruction + control mask, it computes the data
// movement, and this module emits a styled HTML page instead of monospace text. HTML
// earns its keep on the things text can't do: color that tracks a byte's identity as
// it moves across SRC->DST, proportional colored port-pressure bars, and highlighted
// changed bits. Generic — adapts to register width (ids.len()/16 lanes) and any
// permute/shuffle mnemonic; nothing is hardcoded per kernel.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir
import sim.core.engine as engine
import sim.core.engine2 as e2
import sim.intel.movement as mv

// ---- HTML escaping ---------------------------------------------------------
def esc(Str s) -> Str {
    Str a = s.replace("&", "&amp;")
    Str b = a.replace("<", "&lt;")
    return b.replace(">", "&gt;")
}

// color per byte identity, grouped BY SOURCE LANE: each 128-bit lane gets its own
// hue family (lane0 blue / lane1 green / lane2 orange / lane3 purple), and bytes
// within a lane vary only in lightness by position. So the original data reads as 4
// color families, and after a cross-lane permute a byte keeps its SOURCE-lane color
// -> a green byte sitting in the blue lane = "this came from lane 1" at a glance.
def color_of(int id) -> Str {
    int lane = id / 16
    int pos = id % 16
    int hue = 210                    // lane 0 = blue
    if lane == 1 { hue = 145 }       // lane 1 = green
    if lane == 2 { hue = 30 }        // lane 2 = orange
    if lane == 3 { hue = 285 }       // lane 3 = purple
    // lanes 4..7 = a SECOND source register (two-input ops: unpack/blend/align/or),
    // a distinct hue family so src-B bytes are visually separable from src-A.
    if lane == 4 { hue = 0 }         // src B lane 0 = red
    if lane == 5 { hue = 175 }       // src B lane 1 = teal
    if lane == 6 { hue = 50 }        // src B lane 2 = gold
    if lane == 7 { hue = 320 }       // src B lane 3 = pink
    int light = 40 + pos             // 40..55% across the 16 positions in a lane
    return f"hsl({hue},52%,{light}%)"
}

// ---- byte-lane grid (4 lanes x 16 bytes, MSB-first, colored by identity) ----
def h_lanes(Str label, &Vec(int) ids) -> Str {
    int n = ids.len()
    int lanes = n / 16
    Str out = f"  <div class='lanes'><div class='lab'>{esc(label)}</div>\n"
    int l = lanes - 1
    while l >= 0 {
        out = f"{out}    <div class='lane'><span class='lname'>L{l}</span>"
        int b = 15
        while b >= 0 {
            int idx = l * 16 + b
            int id = 0 - 1
            if idx < n { id = ids.get!(idx) }
            if id >= 0 {
                Str col = color_of(id)
                int slane = id / 16
                Str cell = f"<span class='by' style='background:{col}' title='B{idx} = src byte {id} (from lane {slane})'>{id}</span>"
                out = f"{out}{cell}"
            } else {
                out = f"{out}<span class='by dead'>.</span>"
            }
            b = b - 1
        }
        out = f"{out}</div>\n"
        l = l - 1
    }
    return f"{out}  </div>\n"
}

// ---- memory view: VERTICAL, one element per row (top = lowest address) ------
// For memory operands (compress-store / expand-load) the dense layout is shown top-to-
// bottom, one ew-bit element per line; each cell is coloured by its source element's
// lane so you can see which register element landed at which memory slot.
def h_mem_vertical(Str label, Str base, &Vec(int) src_elems, int ew) -> Str {
    int ebytes = ew / 8
    int n = src_elems.len()
    Str out = f"  <div class='memv'><div class='lab'>{esc(label)}</div>\n"
    for i in 0..n {
        int se = src_elems.get!(i)
        int off = i * ebytes
        Str row = f"    <div class='memrow'><span class='moff'>[{esc(base)} + {off}]</span>"
        // the actual ew/8 source bytes that land in this slot, MSB-first, src-lane coloured
        int b = ebytes - 1
        while b >= 0 {
            int id = se * ebytes + b
            Str col = color_of(id)
            Str cell = f"<span class='by' style='background:{col}' title='byte {id} (src elem {se})'>{id}</span>"
            row = f"{row}{cell}"
            b = b - 1
        }
        row = f"{row}<span class='mnote'>{ew}-bit (src elem {se})</span></div>\n"
        out = f"{out}{row}"
    }
    return f"{out}  </div>\n"
}

def _hex1(int v) -> Str {
    int d = v & 15
    if d < 10 { return f"{d}" }
    if d == 10 { return "a" }
    if d == 11 { return "b" }
    if d == 12 { return "c" }
    if d == 13 { return "d" }
    if d == 14 { return "e" }
    return "f"
}
// ---- index-control mask (the ONE renderer for every permute index mask) -----
// Each control element is a full ew-bit unit whose low `idx_bits` bits are the index; the
// high bits are ignored. `sel_bit` >= 0 marks a two-table select bit (vpermi2*/vpermt2*);
// sel_bit < 0 = single-source (vpermb/d/w/q, vpermil*, vpshufd, ...). General: any width /
// idx_bits / sel_bit — no per-mnemonic special-casing. Two forms chosen by element width:
//   * ew >= 32 (dword/qword): drawn TO SCALE as ew/4 hex nibbles — high nibbles = 0
//     (shown, not omitted), low nibble(s) = index, the nibble holding the select bit is
//     blue — so each control reads as a full ew-bit value.
//   * ew < 32 (byte/word): compact [ignored . | select-bit | index-decimal] to stay narrow.
def h_index_mask_sel(Str label, &Vec(int) idx, int ew, int idx_bits, int sel_bit) -> Str {
    int n = idx.len()
    int per_lane = 128 / ew
    if per_lane < 1 { per_lane = 1 }
    int lanes = n / per_lane
    Str ename = "dword"
    if ew == 8 { ename = "byte" }
    if ew == 16 { ename = "word" }
    if ew == 64 { ename = "qword" }
    int telems = 1
    int z = 0
    while z < idx_bits { telems = telems * 2; z = z + 1 }
    // wide elements (dword/qword + float) are drawn TO SCALE as ew/4 hex nibbles; narrow
    // ones (byte/word) stay compact so they don't get too wide.
    bool nibform = false
    if ew >= 32 { nibform = true }
    // sel_bit < 0 -> single-source mask (no table-select bit): every control element is just
    // a lane-relative index, the high bits ignored. (vpermilps/vpshufd immediate selectors.)
    bool twosrc = false
    if sel_bit >= 0 { twosrc = true }
    int nnib = ew / 4
    Str out = f"  <div class='lanes'><div class='lab'>{esc(label)}</div>\n"
    if nibform {
        if twosrc {
            out = out + f"    <div class='kgran'>each control shown to scale as a {ew}-bit {ename} ({nnib} hex nibbles): higher nibbles = 0 (ignored); the index sits in the low nibble(s), and the nibble holding <span class='sbit'>bit {sel_bit}</span> (blue) is the table select (0=src1, 1=src2)</div>\n"
        } else {
            out = out + f"    <div class='kgran'>each control shown to scale as a {ew}-bit {ename} ({nnib} hex nibbles): low nibble = lane-relative index (0..{telems - 1}), higher nibbles = 0 (ignored)</div>\n"
        }
    } else {
        if twosrc {
            out = out + f"    <div class='kgran'>per {ename}: high bits ignored (shown <span class='ib'>.</span>), <span class='sbit'>bit {sel_bit}</span> = table select (0=src1, 1=src2), then the within-table index (0..{telems - 1})</div>\n"
        } else {
            out = out + f"    <div class='kgran'>per {ename}: high bits ignored (shown <span class='ib'>.</span>), then the lane-relative index (0..{telems - 1})</div>\n"
        }
    }
    int l = lanes - 1
    while l >= 0 {
        out = f"{out}    <div class='lane'><span class='lname'>L{l}</span>"
        int e = per_lane - 1
        while e >= 0 {
            int ei = l * per_lane + e
            int v = 0 - 1
            if ei < n { v = idx.get!(ei) }
            if v >= 0 {
                int within = v & (telems - 1)
                int tbl = (v >> idx_bits) & 1
                if nibform {
                    Str tip = f"dst {ename} {ei}: index {within}"
                    if twosrc { tip = f"dst {ename} {ei}: table {tbl} (src{tbl + 1}), index {within}" }
                    out = f"{out}<span class='nib2' title='{tip}'>"
                    int nb = nnib - 1
                    while nb >= 0 {
                        int lo = nb * 4
                        int hi = lo + 3
                        int nibval = (v >> lo) & 15
                        if twosrc {
                            if sel_bit < lo {
                                out = f"{out}<span class='nbz'>0</span>"   // above the select bit -> ignored
                            } else {
                                if sel_bit <= hi {
                                    // this nibble carries the table-select bit (its hex value's
                                    // top bit = select, low bits = index when they share it)
                                    out = f"{out}<span class='nbs'>{_hex1(nibval)}</span>"
                                } else {
                                    out = f"{out}<span class='nbi'>{_hex1(nibval)}</span>"   // below -> pure index
                                }
                            }
                        } else {
                            // single-source: nibbles holding index bits are amber, the rest 0
                            if lo >= idx_bits {
                                out = f"{out}<span class='nbz'>0</span>"
                            } else {
                                out = f"{out}<span class='nbi'>{_hex1(nibval)}</span>"
                            }
                        }
                        nb = nb - 1
                    }
                    out = f"{out}</span>"
                } else {
                    // show the ignored bits above the index/select as faded '.' (distinct
                    // from the blue select bit). condense to one marker when there are many.
                    int ign_count = ew - idx_bits          // single-source: bits above index
                    if twosrc { ign_count = ew - 1 - sel_bit }
                    Str ign = ""
                    if ign_count > 0 {
                        if ign_count <= 4 {
                            int q = 0
                            while q < ign_count { ign = ign + "<span class='ib'>.</span>"; q = q + 1 }
                        } else {
                            ign = f"<span class='ib' title='{ign_count} ignored high bits'>.</span>"
                        }
                    }
                    if twosrc {
                        out = f"{out}<span class='sel2' title='dst {ename} {ei}: table {tbl} (src{tbl + 1}), index {within}'>{ign}<span class='sb'>{tbl}</span><span class='ix'>{within}</span></span>"
                    } else {
                        out = f"{out}<span class='sel2' title='dst {ename} {ei}: index {within}'>{ign}<span class='ix'>{within}</span></span>"
                    }
                }
            } else {
                out = f"{out}<span class='sel2 dead'><span class='sb'>.</span><span class='ix'>.</span></span>"
            }
            e = e - 1
        }
        out = f"{out}</div>\n"
        l = l - 1
    }
    return f"{out}  </div>\n"
}

// ---- k-register predicate mask: a flat N-bit number (NOT a lane grid) -------
// A write-mask k holds one bit per element; the bit count = elements = VL/ew, so it is
// 8 bits for qword ops, 16 for dword, 32 for word, 64 for byte. Shows the data
// GRANULARITY (which element width each bit controls) and the bits MSB-first.
def h_kmask(Str label, &Vec(int) bits, int ew) -> Str {
    int n = bits.len()
    Str gran = "qword"
    if ew == 8 { gran = "byte" }
    if ew == 16 { gran = "word" }
    if ew == 32 { gran = "dword" }
    if ew == 64 { gran = "qword" }
    Str out = f"  <div class='lanes'><div class='lab'>{esc(label)}</div>\n"
    out = out + f"    <div class='kgran'>{n}-bit mask; each bit selects one {ew}-bit element ({gran})</div>\n"
    out = out + "    <div class='lane'>"
    int e = n - 1
    while e >= 0 {
        int b = 0
        if e < n { b = bits.get!(e) }
        Str cls = "kb off"
        if b == 1 { cls = "kb on" }
        Str cell = f"<span class='{cls}' title='bit {e} (element {e})'>{b}</span>"
        out = f"{out}{cell}"
        if e % 8 == 0 { if e > 0 { out = f"{out}<span class='kgap'></span>" } }
        e = e - 1
    }
    out = f"{out}</div>\n  </div>\n"
    return out
}

// ---- word-lane view: N elements of a vector op shown in parallel ------------
// makes a "scalar-looking" per-element vector op (the hoist-scan test/shift) visibly
// 32-wide: every word is a colored cell (by 128-bit lane); msb=true ticks bit15 (the
// 0x8000 test). note explains the parallel operation.
def h_words(Str label, int n, bool msb, Str note) -> Str {
    int per_lane = n / 4
    if per_lane < 1 { per_lane = n }
    Str tick = ""
    if msb { tick = "<sup>15</sup>" }
    Str out = f"  <div class='lanes'><div class='lab'>{esc(label)}</div>\n    <div class='lane wline'>"
    int i = n - 1
    while i >= 0 {
        int lane = i / per_lane
        int hue = 210
        if lane == 1 { hue = 145 }
        if lane == 2 { hue = 30 }
        if lane == 3 { hue = 285 }
        int light = 42 + (i % per_lane) * 2
        Str cell = f"<span class='wd' style='background:hsl({hue},52%,{light}%)'>w{i}{tick}</span>"
        out = f"{out}{cell}"
        i = i - 1
    }
    out = f"{out}</div>\n    <div class='wnote'>{esc(note)}</div>\n  </div>\n"
    return out
}

// ---- gather-run table (cross-lane rows highlighted) ------------------------
def h_runs(Str mn, &Vec(int) eff) -> Str {
    Vec(mv.Run) runs = mv.find_runs(eff)
    Str cap = f"{esc(mn)} — auto-derived gather runs (dst[i]=src[idx], MSB-first)"
    Str out = f"  <table class='runs'><caption>{cap}</caption>\n"
    out = f"{out}    <tr><th>dst bytes</th><th>src bytes</th><th>lane</th></tr>\n"
    int nr = runs.len()
    int ri = nr - 1
    while ri >= 0 {
        mv.Run r = runs.get!(ri)
        Str cls = "inlane"
        Str tag = "in-lane"
        if r.dlo / 16 != r.slo / 16 { cls = "xlane"; tag = "CROSS-LANE" }
        Str row = f"    <tr class='{cls}'><td>[{r.dhi}..{r.dlo}]</td><td>[{r.shi}..{r.slo}]</td><td>{tag}</td></tr>\n"
        out = f"{out}{row}"
        ri = ri - 1
    }
    return f"{out}  </table>\n"
}

// ---- one movement step: SRC grid -> mask line -> DST grid (library-computed) --
// the colored SRC/DST grids show the movement; maskdesc names the control mask and
// its concrete values (no verbose per-byte run table needed).
def h_permute(Str title, &Str mn, &mv.ByteReg src, &Vec(int) mask, Str maskdesc) -> Str {
    bool cross = mv.is_cross_lane(mn)
    Vec(int) eff = mv.eff_source(mask, cross, 64)
    mv.ByteReg dst = mv.perm_apply(src, &eff)
    Str srcg = h_lanes("SRC (byte identities; gray = dead/zero)", &src.ids)
    Str ml = f"  <div class='maskline'>{esc(maskdesc)}</div>\n"
    Str dstg = h_lanes("DST (after the permute)", &dst.ids)
    Str cl = "in-lane"
    if cross { cl = "CROSS-LANE" }
    Str head = f"  <div class='step'><h3>{esc(title)} <span class='kind'>[{cl}]</span></h3>\n"
    return f"{head}{srcg}{ml}{dstg}  </div>\n"
}

// ---- engine-1 bottleneck: proportional colored port-pressure bars -----------
def h_bottleneck(&engine.Bottleneck b, &Vec(ir.Uop) uops, &Vec(ir.Port) ports) -> Str {
    int nports = ports.len()
    Vec(int) pload = engine.port_pressure(uops, nports)
    int sc = engine.scale()
    int pmax = b.res_mii_x
    if pmax < 1 { pmax = 1 }
    Str meta = f"uops={b.total_uops} · ResMII={b.res_mii_x * 100 / sc}/100 c · crit-path={b.critical}c"
    // recurrence layer: show RecMII + II (actual steady cyc/iter) when loop-carried.
    if b.rec_mii_x > 0 {
        meta = f"{meta} · RecMII={b.rec_mii_x * 100 / sc}/100 c · II={b.ii_x * 100 / sc}/100 c"
    }
    Str out = f"  <div class='bn'><h3>engine-1 bottleneck: <span class='verdict'>{esc(b.kind)}</span></h3>\n"
    out = f"{out}  <p class='meta'>{meta}</p>\n  <table class='ports'>\n"
    int p = 0
    for pt in &ports {
        int lp = pload.get!(p)
        int pct = lp * 100 / pmax
        if pct > 100 { pct = 100 }
        Str cls = "pbar"
        if p == b.port_id { cls = "pbar hot" }
        Str val = f"{lp * 100 / sc}/100"
        Str lbl = esc(pt.label)
        Str row = f"    <tr><td class='plbl'>{lbl}</td><td class='ptrk'><div class='{cls}' style='width:{pct}%'></div></td><td class='pval'>{val}</td></tr>\n"
        out = f"{out}{row}"
        p = p + 1
    }
    return f"{out}  </table>\n  </div>\n"
}

// ---- bit-level transform: before/after with changed bits highlighted --------
def bit_at(i64 v, int b) -> int {
    i64 sh = b as i64
    i64 one = 1 as i64
    return ((v >> sh) & one) as int
}

def h_bits(Str label, i64 before, i64 after, int width) -> Str {
    Str br = "before  "
    Str ar = "after   "
    int b = width - 1
    while b >= 0 {
        int bb = bit_at(before, b)
        int ab = bit_at(after, b)
        br = f"{br}{bb}"
        if bb != ab { ar = f"{ar}<span class='chg'>{ab}</span>" }
        else { ar = f"{ar}<span class='same'>{ab}</span>" }
        if b % 8 == 0 {
            if b > 0 { br = f"{br} "; ar = f"{ar} " }
        }
        b = b - 1
    }
    Str head = f"  <div class='bits'><div class='lab'>{esc(label)}</div>\n  <pre>"
    return f"{head}{br}\n{ar}</pre>\n  </div>\n"
}

// ---- page wrapper (embedded CSS, dark monospace theme) ----------------------
def page(Str title, Str body) -> Str {
    // CSS built as separate += statements (one literal each) to avoid long-+-chains.
    Str css = "<style>"
    css = css + "body{font-family:Consolas,'Courier New',monospace;background:#f6f7f9;color:#2b2f36;padding:22px;line-height:1.4}"
    css = css + "h2{color:#1f4e5f;border-bottom:1px solid #d6dce2;padding-bottom:6px}"
    css = css + "h3{color:#2563a8;margin:16px 0 6px;font-size:15px}"
    css = css + ".meta{color:#6a7079;margin:2px 0 8px}"
    css = css + ".lanes{margin:6px 0}.lane{white-space:nowrap;margin:2px 0}"
    css = css + ".lname{display:inline-block;width:30px;color:#8a909a}"
    css = css + ".by{display:inline-block;width:22px;text-align:center;margin:1px;border-radius:3px;color:#fff;font-size:11px}"
    css = css + ".by.dead{background:#e4e8ec;color:#aab0b8}"
    css = css + ".ign{display:inline-block;width:22px;box-sizing:border-box;text-align:center;margin:1px;padding:1px 0;border-radius:2px;font-size:11px;font-family:Consolas,monospace;background:#eef1f5;color:#aeb6c0;border:0.5px solid #dde2e8}"
    css = css + ".ixv{display:inline-block;width:22px;box-sizing:border-box;text-align:center;margin:1px;padding:1px 0;border-radius:2px;font-size:11px;font-family:Consolas,monospace;background:#faeeda;color:#633806;border:0.5px solid #ef9f27}"
    css = css + ".ixv.dead{background:#f3f4f6;color:#aab0b8;border-color:#e0e4e8}"
    css = css + ".dwsep{display:inline-block;width:8px}"
    css = css + ".sbit{display:inline-block;box-sizing:border-box;text-align:center;padding:0 5px;border-radius:2px;font-size:11px;font-family:Consolas,monospace;background:#2563a8;color:#fff;font-weight:bold}"
    css = css + ".sel2{display:inline-block;margin:1px;border-radius:2px;overflow:hidden;border:0.5px solid #cdd6e0;white-space:nowrap;vertical-align:middle}"
    css = css + ".sel2 .sb{display:inline-block;width:13px;text-align:center;font-size:10px;font-family:Consolas,monospace;background:#2563a8;color:#fff;font-weight:bold}"
    css = css + ".sel2 .ib{display:inline-block;width:11px;text-align:center;font-size:10px;font-family:Consolas,monospace;background:#e9ebee;color:#b6bcc4}"
    css = css + ".sel2 .ix{display:inline-block;min-width:17px;text-align:center;padding:0 1px;font-size:11px;font-family:Consolas,monospace;background:#faeeda;color:#633806}"
    css = css + ".kgran .ib{display:inline-block;width:13px;text-align:center;border-radius:2px;background:#e9ebee;color:#b6bcc4;font-family:Consolas,monospace}"
    css = css + ".sel2.dead .sb{background:#cfd3d8}.sel2.dead .ix{background:#f3f4f6;color:#aab0b8}"
    css = css + ".nib2{display:inline;white-space:nowrap}"
    css = css + ".nib2 .nbz{display:inline-block;width:14px;margin:1px;box-sizing:border-box;text-align:center;border-radius:2px;font-size:10px;font-family:Consolas,monospace;background:#f3f4f6;color:#c2c8d0}"
    css = css + ".nib2 .nbs{display:inline-block;width:14px;margin:1px;box-sizing:border-box;text-align:center;border-radius:2px;font-size:10px;font-family:Consolas,monospace;background:#2563a8;color:#fff;font-weight:bold}"
    css = css + ".nib2 .nbi{display:inline-block;width:14px;margin:1px;box-sizing:border-box;text-align:center;border-radius:2px;font-size:11px;font-family:Consolas,monospace;background:#faeeda;color:#633806}"
    css = css + ".kgran{color:#3f6388;font-size:12px;margin:2px 0 4px}"
    css = css + ".kb{display:inline-block;width:16px;text-align:center;margin:1px;border-radius:2px;font-size:11px;font-family:Consolas,monospace}"
    css = css + ".kb.on{background:#2f8a4e;color:#fff;font-weight:bold}.kb.off{background:#e4e8ec;color:#9aa0aa}"
    css = css + ".kgap{display:inline-block;width:7px}"
    css = css + ".memv{margin:6px 0}.memrow{margin:2px 0;white-space:nowrap}"
    css = css + ".moff{display:inline-block;width:86px;color:#6a7079;font-size:11px}"
    css = css + ".mcell{display:inline-block;min-width:74px;text-align:center;padding:2px 8px;border-radius:3px;color:#fff;font-size:11px}"
    css = css + ".mnote{color:#9aa0aa;font-size:10px;margin-left:8px}"
    css = css + "table.runs,table.ports{border-collapse:collapse;margin:6px 0;font-size:13px}"
    css = css + ".runs td,.runs th{border:1px solid #d6dce2;padding:2px 9px;text-align:center}"
    css = css + ".runs th{background:#eef1f4}"
    css = css + ".runs caption{color:#6a7079;text-align:left;padding-bottom:4px}"
    css = css + ".runs tr.xlane td{color:#b06a00}.runs tr.inlane td{color:#1f7a4d}"
    css = css + ".ports td{padding:2px 0}.ptrk{width:300px;background:#e4e8ec;border-radius:2px}"
    css = css + ".pbar{height:15px;background:#4a7fc0;border-radius:2px}.pbar.hot{background:#d24b4b}"
    css = css + ".plbl{padding-right:12px;color:#2563a8}.pval{padding-left:12px;color:#6a7079}"
    css = css + ".verdict{color:#d24b4b}.kind{color:#b06a00;font-size:12px;font-weight:normal}"
    css = css + ".bits pre{background:#eef1f4;padding:10px;border-radius:4px;font-size:14px;margin:0}"
    css = css + ".bits .chg{color:#d24b4b;font-weight:bold}.bits .same{color:#aeb4bc}"
    css = css + ".step{border:1px solid #c2cad4;border-left:4px solid #2563a8;border-radius:6px;padding:10px 16px 14px;margin:20px 0;background:#fff;box-shadow:0 1px 3px rgba(40,60,90,0.10)}"
    css = css + ".step h3{margin-top:4px;border-bottom:1px solid #eef1f4;padding-bottom:5px}"
    css = css + ".step.compute{border-left-color:#cdd3da}.step.compute h3{color:#3f6388;font-size:14px}"
    css = css + ".mn{color:#1f7a4d}.port{color:#6a7079;font-size:12px;font-weight:normal}"
    css = css + ".role{color:#2b2f36;margin:2px 0}.ew{color:#6a7079;font-size:12px}"
    css = css + ".isa{background:#2563a8;color:#fff;font-size:11px;padding:2px 9px;border-radius:999px;white-space:nowrap}"
    css = css + ".legend{margin:8px 0;color:#2563a8;font-size:13px}.legend .by{width:26px}"
    css = css + ".dimnote{color:#9aa0aa;font-size:12px}"
    css = css + "pre.asm{background:#eef1f4;padding:10px;border-radius:4px;color:#1f7a4d;font-size:13px}"
    css = css + "pre.sect{background:#eef1f4;padding:10px;border-radius:4px;color:#2b2f36;font-size:12px;overflow:auto}"
    css = css + "h2.banner{color:#1f4e5f;background:#eaeef2;padding:8px 12px;border-radius:4px;margin-top:22px;border-bottom:none}"
    css = css + ".wline{display:flex;flex-wrap:wrap;gap:2px}"
    css = css + ".wd{display:inline-block;min-width:30px;text-align:center;padding:2px 0;border-radius:3px;color:#fff;font-size:10px}"
    css = css + ".wd sup{font-size:7px;opacity:.85}"
    css = css + ".wnote{color:#3f6388;font-size:12px;margin:4px 0 2px}"
    css = css + ".maskline{color:#3f6388;font-size:12px;margin:5px 0;font-family:Consolas,monospace}"
    css = css + "table.gantt{border-collapse:collapse;margin:6px 0;font-size:11px}"
    css = css + ".gantt th{border:1px solid #d6dce2;color:#6a7079;font-weight:normal}"
    css = css + ".gantt th.lbl{padding:1px 6px}"
    css = css + ".gantt th.cy{width:16px;padding:0;text-align:center;font-size:9px;box-sizing:border-box}"
    css = css + ".gantt td{border:1px solid #e4e8ec}"
    // every cycle cell is exactly 16x16 (border-box) so all cells line up uniformly
    css = css + ".gantt td.cy{width:16px;height:16px;padding:0;text-align:center;font-size:9px;line-height:16px;color:#fff;box-sizing:border-box;font-family:Consolas,monospace}"
    css = css + ".gantt td.gu{color:#6a7079;padding:0 6px}"
    css = css + ".gantt td.gmn{text-align:left;padding:0 8px;color:#1f7a4d;border-color:#d6dce2}"
    css = css + ".gantt td.gp{color:#6a7079;padding:0 6px}"
    css = css + ".gantt td.wt{background:#dbe3ec}"
    css = css + ".gantt td.fly{background:#efe6d0;color:#b0a576}"
    // cell BACKGROUND = execution port (p0..p7); cell NUMBER = iteration index
    css = css + ".gantt td.p0{background:#c4504f}.gantt td.p1{background:#dd8452}.gantt td.p2{background:#7f8c8d}.gantt td.p3{background:#9aa7ad}"
    css = css + ".gantt td.p4{background:#8d7355}.gantt td.p5{background:#4c72b0}.gantt td.p6{background:#55a868}.gantt td.p7{background:#b0905a}"
    css = css + ".gantt tr.isep td{background:#eef1f5;color:#54606e;text-align:left;font-size:10px;font-weight:bold;padding:2px 6px;border-color:#d6dce2}"
    css = css + "span.sw{display:inline-block;width:15px;height:12px;border-radius:2px;vertical-align:middle;border:1px solid #cfd6dd}"
    css = css + "span.sp0{background:#c4504f}span.sp1{background:#dd8452}span.sp2{background:#7f8c8d}span.sp3{background:#9aa7ad}"
    css = css + "span.sp4{background:#8d7355}span.sp5{background:#4c72b0}span.sp6{background:#55a868}span.sp7{background:#b0905a}"
    css = css + "span.wt{background:#dbe3ec}"
    css = css + ".gnote{color:#6a7079;font-size:12px;margin:4px 0}"
    // left navigation sidebar (gallery quick-jump); other pages have no .navbar/.content.
    css = css + ".navbar{position:fixed;left:0;top:0;bottom:0;width:204px;overflow-y:auto;background:#eef1f4;border-right:1px solid #c6cdd6;padding:10px 8px;font-size:12px;box-sizing:border-box}"
    css = css + ".navbar .navtitle{font-weight:bold;color:#1f4e5f;margin:2px 0 8px;font-size:13px}"
    css = css + ".navbar a{display:block;color:#2563a8;text-decoration:none;padding:3px 6px;border-radius:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    css = css + ".navbar a:hover{background:#dce3ea}.navbar a .cnt{color:#9aa0aa;font-size:11px}"
    css = css + ".content{margin-left:224px}"
    css = css + ".gantt td.pp{color:#6a7079;font-family:Consolas,monospace;text-align:center;padding:0 5px}"
    css = css + "</style>"
    Str h = f"<!doctype html><html><head><meta charset='utf-8'><title>{esc(title)}</title>"
    return f"{h}{css}</head><body>\n<h2>{esc(title)}</h2>\n{body}</body></html>\n"
}

// listing block (asm in a <pre>)
def h_listing(Str src) -> Str {
    return f"  <pre class='asm'>{esc(src)}</pre>\n"
}

// a titled monospace section: wrap an existing text view (Gantt, provenance, advisor)
// in a <pre> block so naturally-tabular stages render verbatim inside the page.
def h_section(Str title, Str monotext) -> Str {
    Str head = f"  <h3>{esc(title)}</h3>\n  <pre class='sect'>"
    return f"{head}{esc(monotext)}</pre>\n"
}

// a plain heading + paragraph (for the uarch summary line, etc.).
def h_para(Str title, Str text) -> Str {
    return f"  <h3>{esc(title)}</h3>\n  <p class='meta'>{esc(text)}</p>\n"
}

// a section divider banner (groups the page into simulation / analysis / movement).
def h_banner(Str text) -> Str {
    return f"  <h2 class='banner'>{esc(text)}</h2>\n"
}
// banner with an anchor id (for a left-nav jump target).
def h_banner_id(Str text, Str id) -> Str {
    return f"  <h2 class='banner' id='{esc(id)}'>{esc(text)}</h2>\n"
}

// engine-2 cycle Gantt as an ALIGNED TABLE: one row per uop, one column per clock
// cycle; a filled cell = executing on its port (issued..done), a light cell = waiting
// for its inputs (ready..issued). Reads each uop's ready/issue/done stamps + port.
// legend swatches for the ports that actually appear in a trace (color = port).
def _port_legend(&Vec(ir.UopTrace) tr) -> Str {
    Vec(bool) seen = {}
    for p in 0..8 { seen.push(false) }
    int n = tr.len()
    for i in 0..n {
        int p = tr.get_ref(i).port_used
        if p >= 0 { if p < 8 { seen.set(p, true) } }
    }
    Str s = ""
    for p in 0..8 {
        if seen.get!(p) { s = f"{s}<span class='sw sp{p}'></span> p{p} &nbsp;" }
    }
    return s
}

def h_gantt(&Vec(ir.UopTrace) tr, &Vec(ir.Inst) prog) -> Str {
    int n = tr.len()
    int total = 0
    for i in 0..n {
        &ir.UopTrace t = tr.get_ref(i)
        if t.cycle_done > total { total = t.cycle_done }
    }
    Str out = "  <h3>engine-2 cycle timeline (per-cycle out-of-order schedule)</h3>\n"
    out = f"{out}  <div class='gnote'>cell color = execution port: {_port_legend(tr)}<span class='sw wt'></span> waiting &nbsp; columns = clock cycles</div>\n"
    out = f"{out}  <table class='gantt'>\n    <tr><th class='lbl'>uop</th><th class='lbl'>instruction</th><th class='lbl'>port</th>"
    int c = 0
    while c < total { out = f"{out}<th class='cy'>{c}</th>"; c = c + 1 }
    out = f"{out}</tr>\n"
    for i in 0..n {
        &ir.UopTrace t = tr.get_ref(i)
        int ready = t.cycle_ready
        int iss = t.cycle_issued
        int dn = t.cycle_done
        int port = t.port_used
        Str mn = "uop"
        if t.uop_id < prog.len() {
            &ir.Inst ins = prog.get_ref(t.uop_id)
            mn = ins.mnemonic.copy()
        }
        out = f"{out}    <tr><td class='gu'>u{i}</td><td class='gmn'>{esc(mn)}</td><td class='gp'>p{port}</td>"
        int cc = 0
        while cc < total {
            Str cls = "cy"
            // 1-cycle port occupancy (solid), then in-flight latency (port free).
            if cc == iss { cls = f"cy p{port}" }
            else {
                if cc > iss { if cc < dn { cls = "cy fly" } }
                if cc < iss { if cc >= ready { cls = "cy wt" } }
            }
            out = f"{out}<td class='{cls}'></td>"
            cc = cc + 1
        }
        out = f"{out}</tr>\n"
    }
    return f"{out}  </table>\n"
}

// multi-iteration HTML cycle timeline: replicate the kernel `iters` times (independent
// iterations, same as engine2.replicate/steady) and draw the OVERLAPPED out-of-order
// schedule. DUAL ENCODING: each executing cell's BACKGROUND COLOR = the port it runs on
// (p0..p7), and the NUMBER inside the cell = which iteration it belongs to. So a column
// (cycle) carrying several numbers = those iterations running in parallel; the colors in
// that column show which ports they contend for. Waiting cells stay gray. `iters`
// defaults to 10 (enough to reach steady state for a short kernel). Same scheduler as
// h_gantt — this just feeds it the replicated stream. Rows grouped per iteration.
def h_gantt_iters(&Vec(ir.Uop) uops, &Vec(ir.Inst) prog, int nports, int iters = 10) -> Str {
    int n = uops.len()
    Vec(ir.Uop) rep = e2.replicate(uops, iters)
    Vec(ir.UopTrace) tr = e2.simulate(&rep, nports)
    int tn = tr.len()
    int total = 0
    for i in 0..tn {
        &ir.UopTrace t = tr.get_ref(i)
        if t.cycle_done > total { total = t.cycle_done }
    }
    Str out = f"  <h3>engine-2 multi-iteration timeline ({iters} iterations overlapped)</h3>\n"
    out = f"{out}  <div class='gnote'>cell color = execution port, cell number = iteration: {_port_legend(&tr)}<span class='sw wt'></span> waiting. A column with several numbers = those iterations executing in parallel (steady-state overlap).</div>\n"
    out = f"{out}  <table class='gantt'>\n    <tr><th class='lbl'>iter.uop</th><th class='lbl'>instruction</th><th class='lbl'>possible<br>ports</th><th class='lbl'>port</th>"
    int c = 0
    while c < total { out = f"{out}<th class='cy'>{c}</th>"; c = c + 1 }
    out = f"{out}</tr>\n"
    int colspan = total + 4
    for i in 0..tn {
        int it = i / n
        int idx = i - it * n
        if idx == 0 {
            out = f"{out}    <tr class='isep'><td colspan='{colspan}'>iteration {it}</td></tr>\n"
        }
        &ir.UopTrace t = tr.get_ref(i)
        int ready = t.cycle_ready
        int iss = t.cycle_issued
        int dn = t.cycle_done
        int port = t.port_used
        int de = dn                          // lat-0 micro-op still occupies a slot
        if de <= iss { de = iss + 1 }
        Str mn = "uop"
        if t.uop_id < prog.len() {
            &ir.Inst ins = prog.get_ref(t.uop_id)
            mn = ins.mnemonic.copy()
        }
        // possible-ports set straight from the seed table (the uop's port_mask)
        &ir.Uop urep = rep.get_ref(i)
        Str pps = "{"
        int km = urep.port_mask.len()
        for kk in 0..km {
            if kk > 0 { pps = pps + "," }
            pps = pps + f"{urep.port_mask.get!(kk)}"
        }
        pps = pps + "}"
        out = f"{out}    <tr><td class='gu'>{it}.u{idx}</td><td class='gmn'>{esc(mn)}</td><td class='pp'>{pps}</td><td class='gp'>p{port}</td>"
        int cc = 0
        while cc < total {
            Str cls = "cy"
            Str inner = ""
            // 1-cycle port occupancy (solid port color), then the result is in-flight
            // (latency, port already free -> the next iteration's uop can use it).
            if cc == iss { cls = f"cy p{port}"; inner = f"{it}" }
            else {
                if cc > iss { if cc < dn { cls = "cy fly"; inner = f"{it}" } }
                if cc < iss { if cc >= ready { cls = "cy wt" } }
            }
            out = f"{out}<td class='{cls}'>{inner}</td>"
            cc = cc + 1
        }
        out = f"{out}</tr>\n"
    }
    return f"{out}  </table>\n"
}

// a non-movement step (the caller supplies a class-appropriate description).
def h_compute(Str idx, Str asm, Str port, Str role) -> Str {
    Str head = f"  <div class='step compute'><h3>{esc(idx)} <span class='mn'>{esc(asm)}</span> <span class='port'>{esc(port)}</span></h3>\n"
    return f"{head}    <p class='role'>{esc(role)}</p>\n  </div>\n"
}

// lane color-family legend (the 4 source-lane hues used by h_lanes).
def h_legend() -> Str {
    Str out = "  <div class='legend'>source lane colors: "
    out = out + "<span class='by' style='background:hsl(210,52%,47%)'>L0</span>"
    out = out + "<span class='by' style='background:hsl(145,52%,47%)'>L1</span>"
    out = out + "<span class='by' style='background:hsl(30,52%,47%)'>L2</span>"
    out = out + "<span class='by' style='background:hsl(285,52%,47%)'>L3</span>"
    out = out + " <span class='dimnote'>(within a lane, lighter = higher byte; a byte keeps its source-lane color after moving)</span></div>\n"
    return out
}
