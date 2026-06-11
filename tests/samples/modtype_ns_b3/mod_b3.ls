module mod_b3
import std.str

struct Widget {
    Str name
    int value
}

impl Widget {
    fn get_name(&self) -> Str {
        return self.name
    }

    fn get_value(&self) -> int {
        return self.value
    }

    fn set_value(&!self, int v) {
        self.value = v
    }

    static fn make(Str n, int v) -> Widget {
        Widget w
        w.name = n
        w.value = v
        return w
    }
}

fn make_widget(Str n, int v) -> Widget {
    return Widget.make(n, v)
}

fn describe(Widget w) -> Str {
    return f"{w.get_name()}={w.get_value()}"
}

fn drop_count() -> int {
    int drops = 0
    Widget w1 = Widget.make("a", 1)
    Widget w2 = Widget.make("b", 2)
    Widget w3 = Widget.make("c", 3)
    return 3
}
