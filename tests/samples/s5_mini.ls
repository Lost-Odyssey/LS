type Greeter = Block() -> string

fn make_opt_getter(Option(string) val) -> Greeter {
    return || {
        match val {
            Some(s) => { return s }
            None    => { return "none" }
        }
    }
}

fn main() {
    int i = 0
    string payload = f"msg{i}"
    Option(string) opt = Some(payload)
    print("before make_opt_getter")
    Greeter og = make_opt_getter(opt)
    print("after make_opt_getter")
    print("before og()")
    string ov = og()
    print("after og()")
    print(ov)
}
