// csv_test.ls — std.text.csv: parse / serialize / file round-trip.
// Self-verifying; prints "PASS N". Driver runs JIT + AOT + memcheck (0/0/0).

import std.text.csv as csv
import std.core.str
import std.core.vec
import std.sys.io as io

def check(bool c, Str label) {
    if c { @print(f"PASS {label}") } else { @print(f"FAIL {label}") }
}

def main() -> int {
    // ---- 1. plain parse (no header) ----
    Str a = "1,2,3\n4,5,6\n"
    Csv t1 = csv.parse(&a)!
    check(t1.nrows() == 2, "1a")
    check(t1.ncols() == 3, "1b")
    check(t1.cell(0, 0)!.eq?("1"), "1c")
    check(t1.cell(1, 2)!.eq?("6"), "1d")
    check(t1.has_header() == false, "1e")

    // ---- 2. header parse + by-name access ----
    Str b = "name,age,city\nAlice,30,Beijing\nBob,25,Shanghai\n"
    Csv t2 = csv.parse_header(&b)!
    check(t2.nrows() == 2, "2a")          // header excluded
    check(t2.has_header(), "2b")
    check(t2.get(0, "name")!.eq?("Alice"), "2c")
    check(t2.get(1, "city")!.eq?("Shanghai"), "2d")
    Vec(Str) ages = t2.col("age")!
    check(ages.len() == 2, "2e")
    check(ages.get(0)!.eq?("30"), "2f")
    check(t2.col_index("age")! == 1, "2g")

    // ---- 3. RFC 4180: quoted field with embedded comma ----
    Str c = "name,note\nSmith,\"Beijing, CN\"\n"
    Csv t3 = csv.parse_header(&c)!
    check(t3.get(0, "note")!.eq?("Beijing, CN"), "3a")

    // ---- 4. quoted field with embedded newline + "" escape ----
    Str d = "k,v\nx,\"line1\nline2\"\ny,\"a\"\"b\"\n"
    Csv t4 = csv.parse_header(&d)!
    check(t4.get(0, "v")!.eq?("line1\nline2"), "4a")
    check(t4.get(1, "v")!.eq?("a\"b"), "4b")

    // ---- 5. CRLF line endings + empty trailing field ----
    Str e = "a,b,c\r\np,q,\r\n"
    Csv t5 = csv.parse(&e)!
    check(t5.nrows() == 2, "5a")
    check(t5.cell(1, 2)!.eq?(""), "5b")    // trailing empty field kept

    // ---- 6. blank lines skipped; a,, keeps 3 fields ----
    Str f = "a,,\n\n\nb,c,d\n"
    Csv t6 = csv.parse(&f)!
    check(t6.nrows() == 2, "6a")           // two blank lines dropped
    check(t6.row(0)!.len() == 3, "6b")     // a,, → 3 fields

    // ---- 7. trim option ----
    Str g = "  x  , y \n"
    CsvOpts opt = CsvOpts.default()
    opt.trim = true
    Csv t7 = csv.parse_opts(&g, opt)!
    check(t7.cell(0, 0)!.eq?("x"), "7a")
    check(t7.cell(0, 1)!.eq?("y"), "7b")

    // ---- 8. unterminated quote → Err ----
    Str h = "a,\"oops\n"
    match csv.parse(&h) {
        Ok(_) => { @print("FAIL 8a") }
        Err(_) => { @print("PASS 8a") }
    }

    // ---- 9. serialize with quoting + round-trip ----
    //   array literals passed DIRECTLY at call args (from_list coercion at
    //   the call-arg position — module free fn + method).
    Csv w = csv.with_headers(["a", "b"])
    w.push_row(["x,1", "y\"2"])              // fields needing comma-quote and quote-double
    Str s = w.to_str()
    check(s.contains?("\"x,1\""), "9a")
    check(s.contains?("\"y\"\"2\""), "9b")
    Csv back = csv.parse_header(&s)!
    check(back.get(0, "a")!.eq?("x,1"), "9c")
    check(back.get(0, "b")!.eq?("y\"2"), "9d")

    // ---- 10. out-of-range / unknown column → None ----
    check(t1.cell(99, 0).is_none?(), "10a")
    check(t2.get(0, "nope").is_none?(), "10b")
    check(t2.col("nope").is_none?(), "10c")

    // ---- 11. file round-trip via io ----
    Str path = "csv_rt_tmp.csv"
    csv.write_file(path.copy(), &w)!
    Csv fr = csv.read_file_header(path.copy())!
    check(fr.get(0, "a")!.eq?("x,1"), "11a")
    io.remove(path.copy())

    // ---- 12. from_list coercion at a plain free-function call arg ----
    check(sum_vec([1, 2, 3, 4]) == 10, "12a")
    check(sum_vec([]) == 0, "12b")

    @print("ALL DONE")
    return 0
}

// helper for #12: array literal coerces to Vec(int) at the call-arg position
def sum_vec(Vec(int) v) -> int {
    int s = 0
    int i = 0
    while i < v.len() { s = s + v.get(i)!; i = i + 1 }
    return s
}
