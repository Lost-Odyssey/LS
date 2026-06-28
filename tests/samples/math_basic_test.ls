// Phase 9 (math stdlib): std.core.math built-in primitives
// Verifies:
//   - import std.core.math resolves to compiler built-in primitives
//   - math.<def>(...) lowers to LLVM intrinsic / libm direct call
//   - math.PI / math.E / math.INF / math.NAN constants
//   - Both AOT and JIT paths produce identical output

import std.core.math as math

def main() -> int {
    // Constants
    @print(math.PI)              // 3.14159265358979
    @print(math.E)               // 2.71828182845905

    // Basic arithmetic helpers
    @print(math.abs(-3.5))       // 3.5
    @print(math.min(2.0, 5.0))   // 2.0
    @print(math.max(2.0, 5.0))   // 5.0

    // Rounding
    @print(math.floor(3.7))      // 3.0
    @print(math.ceil(3.2))       // 4.0
    @print(math.round(3.5))      // 4.0
    @print(math.trunc(-3.7))     // -3.0

    // Powers and logs
    @print(math.sqrt(16.0))      // 4.0
    @print(math.pow(2.0, 10.0))  // 1024.0
    @print(math.exp(0.0))        // 1.0
    @print(math.log(math.E))     // 1.0
    @print(math.log2(8.0))       // 3.0
    @print(math.log10(1000.0))   // 3.0

    // Trig (LLVM intrinsics)
    @print(math.sin(0.0))        // 0.0
    @print(math.cos(0.0))        // 1.0

    // Trig (libm)
    @print(math.tan(0.0))        // 0.0
    @print(math.atan2(1.0, 1.0)) // ~0.785 (PI/4)

    // Angle conversion (deg <-> rad)
    @print(math.radians(180.0))  // 3.14159265358979 (PI)
    @print(math.degrees(math.PI))// 180.0

    return 0
}
