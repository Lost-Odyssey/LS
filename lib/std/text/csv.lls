// std/csv.ls — CSV reader/writer for LS (RFC 4180).
//
// Pure LS: a byte-level recursive state-machine parser + a quoting serializer,
// over Str / Vec / Map / Result / Option. No compiler support, no FFI.
//
// Data model: `Csv` = headers (empty when parsed without a header row) + rows
// (each row a Vec(Str) of fields). Access by index or by header name; missing
// index / unknown column name return Option, file errors return Result.
//
// v1 scope: whole-buffer parse/serialize + file convenience wrappers. No
// streaming Reader (deferred to v2), no type inference (cells stay Str; convert
// with Str.to_int / to_float / to_bool).

import std.core.vec
import std.core.str
import std.sys.io as io

// ---- Types ----

// A parsed CSV table. has_drop: owns headers + rows (auto-destroyed).
struct Csv {
    Vec(Str) headers          // empty Vec when there is no header row
    Vec(Vec(Str)) rows        // data rows (excludes the header row)
}

// Parse options. POD (int/bool fields), value semantics, no destructor.
struct CsvOpts {
    int delimiter             // field separator byte, default ',' = 44
    int quote                 // quote byte, default '"' = 34
    bool has_header           // treat the first line as headers, default false
    bool trim                 // trim whitespace around each field, default false
}

methods CsvOpts {
    static def default() -> CsvOpts {
        return CsvOpts{ delimiter: 44, quote: 34, has_header: false, trim: false }
    }
}

// ---- Internal serializer helpers ----

// True if a field must be quoted: it contains the delimiter, a quote, CR or LF.
def _needs_quote(&Str f, int delim, int quote) -> bool {
    int n = f.len()
    int i = 0
    while i < n {
        int b = f.byte_at!(i)
        if b == delim { return true }
        if b == quote { return true }
        if b == 10 { return true }
        if b == 13 { return true }
        i = i + 1
    }
    return false
}

// Append one field to `out`, quoting (and doubling embedded quotes) if needed.
def _write_field(&!Str out, &Str f, int delim, int quote) {
    if _needs_quote(f, delim, quote) {
        out.push_byte(quote)
        int n = f.len()
        int i = 0
        while i < n {
            int b = f.byte_at!(i)
            if b == quote { out.push_byte(quote) }   // "" escape
            out.push_byte(b)
            i = i + 1
        }
        out.push_byte(quote)
    } else {
        out.push_str(f)
    }
}

// Append one row (fields joined by delimiter) plus a CRLF line terminator.
def _write_row(&!Str out, &Vec(Str) row, int delim, int quote) {
    int n = row.len()
    int j = 0
    while j < n {
        if j > 0 { out.push_byte(delim) }
        Str f = row.get(j)!
        _write_field(&!out, &f, delim, quote)
        j = j + 1
    }
    out.push_byte(13)   // CR
    out.push_byte(10)   // LF
}

// ---- Csv methods ----

methods Csv {
    def nrows(&self) -> int { return self.rows.len() }

    // Column count: header width if present, otherwise the first row's width.
    def ncols(&self) -> int {
        if self.headers.len() > 0 { return self.headers.len() }
        if self.rows.len() > 0 { return self.rows.get(0)!.len() }
        return 0
    }

    def has_header(&self) -> bool { return self.headers.len() > 0 }

    // Whole row, cloned. Out-of-range → None.
    def row(&self, int i) -> Option(Vec(Str)) {
        if i < 0 { return None }
        if i >= self.rows.len() { return None }
        return Some(self.rows.get(i)!)
    }

    // Single cell, cloned. Out-of-range (row or col) → None.
    def cell(&self, int i, int j) -> Option(Str) {
        if i < 0 { return None }
        if i >= self.rows.len() { return None }
        &Vec(Str) r = self.rows.get_ref(i)
        if j < 0 { return None }
        if j >= r.len() { return None }
        return Some(r.get(j)!)
    }

    // Index of a header column by name. Unknown name → None.
    def col_index(&self, &Str name) -> Option(int) {
        int n = self.headers.len()
        int i = 0
        while i < n {
            &Str h = self.headers.get_ref(i)
            if h.eq?(name) { return Some(i) }
            i = i + 1
        }
        return None
    }

    // Cell by row index + column name. Missing row or column → None.
    def get(&self, int i, &Str name) -> Option(Str) {
        match self.col_index(name) {
            Some(j) => { return self.cell(i, j) }
            None => { return None }
        }
    }

    // Whole column (all data rows) by name, cloned. Unknown name → None.
    // Rows shorter than the column index contribute an empty field.
    def col(&self, &Str name) -> Option(Vec(Str)) {
        match self.col_index(name) {
            Some(j) => {
                Vec(Str) out = {}
                int n = self.rows.len()
                int i = 0
                while i < n {
                    &Vec(Str) r = self.rows.get_ref(i)
                    if j < r.len() {
                        out.push(r.get(j)!)
                    } else {
                        Str empty = ""
                        out.push(empty)
                    }
                    i = i + 1
                }
                return Some(out)
            }
            None => { return None }
        }
    }

    // Append a data row (moved into the table).
    def push_row(&!self, Vec(Str) r) { self.rows.push(r) }

    // Serialize to a Str (RFC 4180: CRLF lines, quoting where required).
    // The header row is emitted first when headers is non-empty.
    def to_str(&self) -> Str {
        Str out = ""
        int d = 44
        int q = 34
        if self.headers.len() > 0 {
            _write_row(&!out, &self.headers, d, q)
        }
        int n = self.rows.len()
        int i = 0
        while i < n {
            &Vec(Str) r = self.rows.get_ref(i)
            _write_row(&!out, r, d, q)
            i = i + 1
        }
        return out
    }

    def clone(&self) -> Csv {
        Csv c = {}
        c.headers = self.headers.copy()
        int n = self.rows.len()
        int i = 0
        while i < n {
            &Vec(Str) r = self.rows.get_ref(i)
            c.rows.push(r.copy())
            i = i + 1
        }
        return c
    }
}

// ---- Parser (RFC 4180 state machine) ----

def parse_opts(&Str text, CsvOpts opts) -> Result(Csv, Str) {
    int d = opts.delimiter
    int q = opts.quote
    int n = text.len()

    // Skip a leading UTF-8 BOM (EF BB BF).
    int start = 0
    if n >= 3 {
        if text.byte_at!(0) == 239 {
            if text.byte_at!(1) == 187 {
                if text.byte_at!(2) == 191 { start = 3 }
            }
        }
    }

    Vec(Vec(Str)) records = {}
    Vec(Str) cur_row = {}
    Str cur = ""
    bool in_quotes = false
    bool field_start = true     // at the start of a field (quote may open here)
    bool line_has = false       // this line has content (skip otherwise = blank line)

    int i = start
    while i < n {
        int b = text.byte_at!(i)

        if in_quotes {
            if b == q {
                if i + 1 < n {
                    if text.byte_at!(i + 1) == q {
                        cur.push_byte(q)        // "" → literal quote
                        i = i + 2
                        continue
                    }
                }
                in_quotes = false               // closing quote
                field_start = false
                i = i + 1
                continue
            }
            cur.push_byte(b)                     // literal (commas/newlines included)
            i = i + 1
            continue
        }

        // not in quotes
        if field_start {
            if b == q {
                in_quotes = true
                field_start = false
                line_has = true
                i = i + 1
                continue
            }
        }

        if b == d {
            // finalize the current field
            if opts.trim {
                Str tf = cur.trim()
                cur_row.push(tf)
            } else {
                cur_row.push(cur)
            }
            cur = ""
            field_start = true
            line_has = true
            i = i + 1
            continue
        }

        if b == 13 {
            // CR (consume a following LF as one CRLF terminator)
            if i + 1 < n {
                if text.byte_at!(i + 1) == 10 { i = i + 1 }
            }
            if line_has {
                if opts.trim {
                    Str tf = cur.trim()
                    cur_row.push(tf)
                } else {
                    cur_row.push(cur)
                }
                cur = ""
                records.push(cur_row)
                cur_row.clear()
            }
            line_has = false
            field_start = true
            i = i + 1
            continue
        }

        if b == 10 {
            // LF
            if line_has {
                if opts.trim {
                    Str tf = cur.trim()
                    cur_row.push(tf)
                } else {
                    cur_row.push(cur)
                }
                cur = ""
                records.push(cur_row)
                cur_row.clear()
            }
            line_has = false
            field_start = true
            i = i + 1
            continue
        }

        // ordinary byte
        cur.push_byte(b)
        field_start = false
        line_has = true
        i = i + 1
    }

    if in_quotes {
        return Err("csv: unterminated quoted field")
    }

    // EOF: flush a trailing record that had content but no final newline.
    if line_has {
        if opts.trim {
            Str tf = cur.trim()
            cur_row.push(tf)
        } else {
            cur_row.push(cur)
        }
        records.push(cur_row)
        cur_row.clear()
    }

    Csv t = {}
    if opts.has_header {
        if records.len() > 0 {
            t.headers = records.remove(0)
        }
    }
    t.rows = records
    return Ok(t)
}

def parse(&Str text) -> Result(Csv, Str) {
    CsvOpts o = CsvOpts.default()
    return parse_opts(text, o)
}

def parse_header(&Str text) -> Result(Csv, Str) {
    CsvOpts o = CsvOpts.default()
    o.has_header = true
    return parse_opts(text, o)
}

// ---- Construction ----

def from_rows(Vec(Vec(Str)) rows) -> Csv {
    Csv t = {}
    t.rows = rows
    return t
}

def with_headers(Vec(Str) headers) -> Csv {
    Csv t = {}
    t.headers = headers
    return t
}

// ---- File I/O convenience wrappers (names avoid clashing with io.ls) ----

def read_file(Str path) -> Result(Csv, Str) {
    match io.read_file(path) {
        Ok(content) => { return parse(&content) }
        Err(e) => { return Err(e.copy()) }
    }
}

def read_file_header(Str path) -> Result(Csv, Str) {
    match io.read_file(path) {
        Ok(content) => { return parse_header(&content) }
        Err(e) => { return Err(e.copy()) }
    }
}

def write_file(Str path, &Csv t) -> Result(int, Str) {
    Str content = t.to_str()
    return io.write_file(path, content)
}
