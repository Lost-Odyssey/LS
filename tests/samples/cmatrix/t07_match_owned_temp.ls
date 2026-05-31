// L-012 ①/②: matching an OWNED rvalue-temp enum scrutinee must drop it —
// incl. a bare catch-all `_` arm and arms with unused bindings. Must be clean.
enum E { Link(string t, string u) Text(string c) }
fn mk_link() -> E { return Link("a".copy(), "url".copy()) }
fn mk_text() -> E { return Text("hi".copy()) }
fn url_of(E x) -> string {
    match x {
        Link(_, u) => { return u.copy() }
        Text(_)    => { return "" }
    }
}
fn main() {
    // bare catch-all `_` on an owned temp with a string payload
    match mk_text() { Link(t, u) => { print(u) } _ => { print("other") } }
    // owned-temp match passed through a helper, unused binding `t`
    string r = url_of(mk_link())
    print(r)
}
