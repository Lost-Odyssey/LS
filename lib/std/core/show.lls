// std/core/show.ls — the `Show` interface: render a value into a byte Sink.
//
// Stage 3 (docs/plan_print_sink.md): `show` writes into a `&!Sink` instead of
// returning a Str, so nested rendering appends into ONE buffer with zero
// intermediate Str allocations. `to_str(x)` is the Str-producing front (Buffer
// sink, then harvest) — the replacement for the old `x.show() -> Str`.
//
// Target of @derive(Show) (docs/plan_static_reflection.md Stage 1): the compiler
// synthesizes `def show(&self, &!Sink out)` field-by-field, every field via
// `self.f.show(&!out)` — primitives, Str and nested struct/enum all impl Show.
//
// `Sink` is imported UNQUALIFIED so the bare type name resolves both here and in
// to_str's monomorphized body at the call site (a generic free function does not
// inherit this module's import aliases when instantiated elsewhere; a bare type
// name resolves via the @derive-injected `import std.core.sink`). Str's own Show
// impl lives HERE (not str.ls) to keep the dependency one-way (show -> {sink,str};
// sink -> str) with no import cycle.

import std.core.str
import std.core.sink

interface Show {
    def show(&self, &!Sink out)
}

// Render any Show value to an owned Str: write into a Buffer sink, harvest buf.
// Replaces the old `x.show() -> Str`. T is inferred from the argument.
def to_str(T)(&T x) -> Str where T: Show {
    Sink s = {}
    x.show(&!s)
    return s.buf
}

// Str + primitive Show impls — all write into the sink (no Str alloc for POD).
// @derive(Show) lowers every field to `self.f.show(&!out)`, so each field type
// (incl. Str and the scalars below) needs an impl. Builtin targets carry no
// module prefix (symbol `int.show`); Str dispatches as std_core_str__Str.show
// (B-3 imported-struct impl), matching the old inherent method's symbol.
methods Str: Show  { def show(&self, &!Sink out) { out.write(self) } }
methods int: Show  { def show(&self, &!Sink out) { out.write_int(self) } }
methods i64: Show  { def show(&self, &!Sink out) { out.write_i64(self) } }
methods f64: Show  { def show(&self, &!Sink out) { out.write_f64(self) } }
methods bool: Show { def show(&self, &!Sink out) { out.write_bool(self) } }
methods char: Show { def show(&self, &!Sink out) { out.write_byte(self as int) } }
// Sized integer / f32 scalars (so Box(i16)/Box(u32)/... show too). Signed small
// ints widen to int (signed decimal); unsigned widen to u64 (unsigned decimal) —
// matching the old print/f-string %d/%u behaviour.
methods i8: Show   { def show(&self, &!Sink out) { out.write_int(self as int) } }
methods i16: Show  { def show(&self, &!Sink out) { out.write_int(self as int) } }
methods i32: Show  { def show(&self, &!Sink out) { out.write_int(self as int) } }
methods u8: Show   { def show(&self, &!Sink out) { out.write_u64(self as u64) } }
methods u16: Show  { def show(&self, &!Sink out) { out.write_u64(self as u64) } }
methods u32: Show  { def show(&self, &!Sink out) { out.write_u64(self as u64) } }
methods u64: Show  { def show(&self, &!Sink out) { out.write_u64(self) } }
methods f32: Show  { def show(&self, &!Sink out) { out.write_f64(self as f64) } }
