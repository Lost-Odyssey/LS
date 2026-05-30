import mod_b3

fn main() -> int {
    Widget w = mod_b3.make_widget("hello", 42)
    string desc = mod_b3.describe(w)
    print(f"{desc}\n")

    Widget w2 = Widget.make("world", 100)
    w2.set_value(200)
    print(f"{w2.get_name()}={w2.get_value()}\n")

    int n = mod_b3.drop_count()
    print(f"drops={n}\n")

    return 0
}
