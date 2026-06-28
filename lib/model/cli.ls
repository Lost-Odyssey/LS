// model/cli.ls — runnable CLI driver for the .lsm toolchain (pure LS, no compiler change).
//
//   ls run lib/model/cli.ls viz   <file.lsm> [--format=ascii|dot|html|svg]
//   ls run lib/model/cli.ls check <file.lsm>
//
// (A thin `ls model ...` subcommand can later spawn this; args() already works.)

import model.lsm as m
import std.sys.proc as proc
import std.sys.io as io
import std.core.str
import std.core.vec

def arg_count() -> int {
    Vec(Str) a = proc.args()
    return a.len()
}

def arg_cmd() -> Str {
    Vec(Str) a = proc.args()
    if a.len() < 1 { return "" }
    return a.get!(0)
}

def arg_fmt() -> Str {
    Vec(Str) a = proc.args()
    int i = 2
    while i < a.len() {
        Str arg = a.get!(i)
        if arg.starts_with?("--format=") {
            return arg.substr(9, arg.len() - 9)
        }
        i = i + 1
    }
    return "ascii"
}

def arg_file() -> Str {
    Vec(Str) a = proc.args()
    if a.len() >= 2 { return a.get!(1) }
    return ""
}

// value after -o (output path for `build`); "" if absent
def arg_out() -> Str {
    Vec(Str) a = proc.args()
    int i = 2
    while i < a.len() {
        Str arg = a.get!(i)
        if arg.eq?("-o") {
            if i + 1 < a.len() { return a.get!(i + 1) }
        }
        i = i + 1
    }
    return ""
}

// true if --simd flag present (build to the SIMD nn.sgemm f32 backend)
def arg_simd() -> bool {
    Vec(Str) a = proc.args()
    int i = 2
    while i < a.len() {
        Str arg = a.get!(i)
        if arg.eq?("--simd") { return true }
        i = i + 1
    }
    return false
}

// true if a bare flag is present
def arg_flag(Str flag) -> bool {
    Vec(Str) a = proc.args()
    int i = 2
    while i < a.len() {
        Str arg = a.get!(i)
        if arg.eq?(flag) { return true }
        i = i + 1
    }
    return false
}

// value after a --flag, "" if absent
def arg_val(Str flag) -> Str {
    Vec(Str) a = proc.args()
    int i = 2
    while i < a.len() {
        Str arg = a.get!(i)
        if arg.eq?(flag) {
            if i + 1 < a.len() { return a.get!(i + 1) }
        }
        i = i + 1
    }
    return ""
}

if arg_count() < 2 {
    @print("usage: model <viz|plan|lower|check> <file.lsm> [--format=ascii|dot|html|svg]")
} else {
    Str cmd = arg_cmd()
    Str fmt = arg_fmt()
    Str file = arg_file()

    match io.read_file(file) {
        Err(e) => { @print(f"error: cannot read '{file}': {e}") }
        Ok(src) => {
            match m.parse(src) {
                Err(pe) => { @print(f"parse error: {pe}") }
                Ok(model) => {
                    if cmd.eq?("check") {
                        match m.validate(&model) {
                            Ok(z) => { @print(f"ok: {file} is valid") }
                            Err(ve) => { @print(f"invalid: {ve}") }
                        }
                    } else if cmd.eq?("viz") {
                        if fmt.eq?("dot") {
                            @print(m.viz_dot(&model))
                        } else if fmt.eq?("html") {
                            @print(m.viz_html(&model))
                        } else if fmt.eq?("svg") {
                            @print(m.viz_svg(&model))
                        } else {
                            @print(m.viz_ascii(&model))
                        }
                    } else if cmd.eq?("plan") {
                        @print(m.plan_memory(&model))
                        @print(m.plan_latency(&model))
                    } else if cmd.eq?("lower") {
                        @print(m.plan_lowering(&model))
                    } else if cmd.eq?("build") {
                        Str out = arg_out()
                        if out.len() == 0 {
                            @print("build needs -o <out.ls>")
                        } else {
                            Str gen = ""
                            Str lswp = arg_val("--lsw")
                            if lswp.len() > 0 {
                                Str gi2 = arg_val("--gin")
                                Str go2 = arg_val("--gout")
                                gen = m.build_forward_lsw(&model, lswp, gi2, go2)
                            } else if arg_flag("--real") {
                                Str wp = arg_val("--weights")
                                Str gi = arg_val("--gin")
                                Str go = arg_val("--gout")
                                if m.has_transformer_op(&model) {
                                    gen = m.build_forward_transformer(&model, wp, gi, go)
                                } else {
                                    gen = m.build_forward_real(&model, wp, gi, go)
                                }
                            } else if arg_simd() {
                                gen = m.build_forward_simd(&model)
                            } else {
                                gen = m.build_forward(&model)
                            }
                            match io.write_file(out, gen) {
                                Ok(nbytes) => { @print(f"wrote {out}") }
                                Err(we) => { @print(f"error writing '{out}': {we}") }
                            }
                        }
                    } else {
                        @print("unknown subcommand (use viz, plan, lower, build, or check)")
                    }
                }
            }
        }
    }
}
