// Helper module for reflect_imported.ls (cross-module @derive(Reflect) regression).
// Defines a deriving type in a SEPARATE module; importing this before
// std.core.reflect used to break reflection in the consumer.
import std.core.reflect

@derive(Reflect)
struct Widget(T) { T value; int id }

methods(T) Widget(T) {
    def kind(&self) -> int { return self.id }
    static def make(T v) -> Widget(T) { return Widget(T){ value: v, id: 0 } }
}

// A user destructor: reflection must surface this as `~`, never the internal
// `__drop` name (mirrors `ls inspect`).
methods(T) Widget(T): Destroy {
    def ~(&!self) { self.id = 0 }
}
