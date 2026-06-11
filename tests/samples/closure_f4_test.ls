/* Phase F.4 tests: Vec(Block) / Map(K, Block)
   Expected output (one per line):
     11
     20
     7
     add3
     sub2
     100
*/

import std.vec
import std.map
import std.str

type H = Block(int) -> int
type Namer = Block() -> Str

/* F.4.1: Vec(H) — push lambdas, iterate and call each */
fn test_vec_block() {
    Vec(H) handlers = {}
    handlers.push(|x| { return x + 1 })
    handlers.push(|x| { return x * 2 })
    handlers.push(|x| { return x - 3 })

    int i = 0
    while i < handlers.len() {
        print(handlers[i](10))
        i = i + 1
    }
}   /* handlers drops: 3 envs freed */

/* F.4.2: Vec(Namer) — push closures with string capture */
fn test_vec_namer() {
    Str s1 = "add3"
    Str s2 = "sub2"
    Vec(Namer) ns = {}
    ns.push([move s1] || { return s1 })
    ns.push([move s2] || { return s2 })

    int i = 0
    while i < ns.len() {
        print(ns[i]())
        i = i + 1
    }
}   /* ns drops: 2 envs freed (each holds a moved string) */

/* F.4.3: Map(string, H) — set lambdas, call by key */
fn test_map_block() {
    Map(Str, H) ops = {}
    ops.set("mul", |x| { return x * 10 })
    match ops.get("mul") {
        Some(f) => { print(f(10)) }
        None => { print(0) }
    }   /* 100 */
}

fn main() {
    test_vec_block()
    test_vec_namer()
    test_map_block()
}
