// emit-c subset extension: POD structs (as C typedefs) + fixed arrays + a
// pointer-based forward reading layer->field. Fed to `ls emit-c`; the generated
// C is compiled with clang. (Also valid LS that type-checks.)

struct Dense { *f32 w; *f32 bias; int nin; int nout }   // POD weight struct
struct Pt { f64 x; f64 y }                              // POD value struct

def dense_forward(*Dense layer, *f32 x, *f32 y) {
    int n = layer.nout
    for j in 0..n {
        f32 acc = layer.bias[j]
        for k in 0..layer.nin {
            acc = acc + layer.w[j * layer.nin + k] * x[k]
        }
        y[j] = acc
    }
}

def midpoint(Pt a, Pt b) -> Pt {
    Pt m = Pt{ x: (a.x + b.x) / 2.0, y: (a.y + b.y) / 2.0 }
    return m
}

def array_sum() -> f32 {
    array(f32, 16) tmp
    int i = 0
    while i < 16 { tmp[i] = i as f32; i = i + 1 }
    f32 s = 0.0 as f32
    for j in 0..16 { s = s + tmp[j] }
    return s
}
