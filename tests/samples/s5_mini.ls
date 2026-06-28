type Greeter = Block() -> Str

def make_opt_getter(Option(Str) val) -> Greeter {
    return || {
        match val {
            Some(s) => { return s }
            None    => { return "none" }
        }
    }
}

def main() {
    int i = 0
    Str payload = f"msg{i}"
    Option(Str) opt = Some(payload)
    @print("before make_opt_getter")
    Greeter og = make_opt_getter(opt)
    @print("after make_opt_getter")
    @print("before og()")
    Str ov = og()
    @print("after og()")
    @print(ov)
}
