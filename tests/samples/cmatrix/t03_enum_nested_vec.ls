// F: enum payload with NESTED Vec — clone + drop
import std.vec
enum E { Rows(Vec(Vec(Str))) None }
fn dump(E e) { match e { Rows(rs) => print(rs.len()), None => print(0) } }
fn main() {
    Vec(Vec(Str)) m = {}
    Vec(Str) r = ["a","b"]; m.push(r)
    E e = Rows(m)
    dump(e)
}
