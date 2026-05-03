// Phase 8: enum with string payload — exercises has_drop + auto-cleanup

enum Event {
    Quit
    Click(int x, int y)
    Message(string text)
}

fn main() -> int {
    Event e1 = Quit
    Event e2 = Click(10, 20)
    Event e3 = Message("hello from enum payload")

    match e1 { Quit => print(0)  Click(x, y) => print(x + y)  Message(t) => print(t) }
    match e2 { Quit => print(0)  Click(x, y) => print(x + y)  Message(t) => print(t) }
    match e3 { Quit => print(0)  Click(x, y) => print(x + y)  Message(t) => print(t) }
    return 0
}
