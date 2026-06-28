// Negative: carving past a Pool's reserved capacity must abort (deterministic
// hard-real-time failure, not silent UB). Expected to exit non-zero before OK.
import std.sci.nn as nn

def main() {
    nn.Pool p = {}
    p.reserve(16)                 // room for 16 f32
    *f32 a = p.tensor(16)         // ok
    *f32 b = p.tensor(16)         // overflow -> "nn.Pool out of capacity" + abort
    @print("POOL OOB UNREACHABLE")
}
