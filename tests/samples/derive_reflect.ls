// Stage 3: @derive(Reflect) -> runtime TypeInfo { name, fields, funcs }.
// Fields come from the struct; method signatures are scanned from the program.
import std.core.reflect

@derive(Reflect)
struct Config { Str host; int port; bool tls }

methods Config {
    def area(&self) -> int { return self.port }
    static def make() -> Config { return Config { host: "", port: 0, tls: false } }
}

def main() {
    TypeInfo ti = Config.reflect()
    @print(ti.name)
    for fi in ti.fields {
        Str line = fi.name + ": " + fi.type_name
        @print(line)
    }
    for m in ti.funcs {
        @print(m.signature)
    }
    @print("DERIVE REFLECT DONE")
}
