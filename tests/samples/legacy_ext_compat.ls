// legacy_ext_compat.ls — intentionally keeps the legacy .ls extension.
// Guards that the compiler still accepts `.ls` source (and that a .ls file can
// import a stdlib module now stored as `.lls`, e.g. std.sys.io → io.lls) after
// the official switch to `.lls`. DO NOT rename this file to .lls.
import std.sys.io

def main() -> int {
    @print(f"legacy .ls still runs: {6 * 7}")
    return 0
}
