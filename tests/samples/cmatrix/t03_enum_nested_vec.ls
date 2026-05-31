// F: enum payload with NESTED vec — clone + drop
enum E { Rows(vec(vec(string))) None }
fn dump(E e) { match e { Rows(rs) => print(rs.length), None => print(0) } }
fn main() {
    vec(vec(string)) m = []
    vec(string) r = ["a","b"]; m.push(r)
    E e = Rows(m)
    dump(e)
}
