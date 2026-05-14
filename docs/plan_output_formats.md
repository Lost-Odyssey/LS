# LS Output Formats — Implementation Plan

## Overview

This plan covers structured data I/O and document generation:
JSON (already in `plan_stdlib.md` Phase S.5), HTML, SVG/charts, Excel/CSV,
PlantUML/Mermaid diagrams, and Markdown.

All formats are implemented as **pure-LS modules** in `stdlib/`, using only
the existing `string`, `vec`, `map`, `io`, and `fs` primitives.
No external C libraries required unless noted as an optional FFI backend.

---

## Phase OF.1 — JSON (cross-reference with `plan_stdlib.md` Phase S.5)

JSON is the gateway format — it is a prerequisite for many of the other modules
below (HTML template data, chart data, config files).

See `plan_stdlib.md §Phase S.5` for the full API and implementation design.

**Effort**: 7–10 days

---

## Phase OF.2 — CSV

### API

```ls
import csv

// --- Reading ---
Result(vec(vec(string)), string) r = csv.read_file("data.csv")
// rows = r.unwrap()   // vec of rows, each row is a vec of fields

// Typed reading with header row:
Result(vec(map(string,string)), string) r = csv.read_with_header("data.csv")
// records = r.unwrap()  // each record is {column_name: value}

// Streaming (large files):
Result(csv.Reader, string) rdr = csv.open("huge.csv")
while csv.has_next(rdr) {
    vec(string) row = csv.next_row(rdr)
    // process row...
}
csv.close_reader(rdr)

// --- Writing ---
csv.Writer w = csv.writer("out.csv")
csv.write_header(w, ["name", "age", "score"])
csv.write_row(w, ["Alice", "30", "9.5"])
csv.write_row(w, ["Bob",   "25", "8.1"])
csv.flush(w)
csv.close_writer(w)

// One-shot write:
vec(vec(string)) rows = [["a","b"],["c","d"]]
csv.write_file("out.csv", rows)

// --- Options ---
csv.Options opts = csv.Options {
    delimiter: ','      // default ','
    quote_char: '"'     // default '"'
    has_header: true
    trim: true          // trim whitespace from fields
}
```

### Implementation

Pure LS. The parser handles:
- Quoted fields with embedded commas and `""` escaping
- `\r\n` and `\n` line endings
- Optional BOM stripping

**Effort**: 3–4 days

---

## Phase OF.3 — HTML Generation

### API

Two complementary interfaces:

#### 3A: Builder API (programmatic, safe)

```ls
import html

html.Doc doc = html.document()
html.Element body = html.body(doc)

html.Element div = html.div(body, class: "container")
html.h1(div, "Page Title")
html.p(div, "Hello, world!")

// Table:
html.Element tbl = html.table(div)
html.Element row = html.tr(tbl)
html.th(row, "Name")
html.th(row, "Score")
row = html.tr(tbl)
html.td(row, "Alice")
html.td(row, "9.5")

// Attributes:
html.attr(div, "id", "main")
html.attr(div, "style", "color:red")

// Output:
string s = html.render(doc)        // compact
string s = html.render_pretty(doc) // indented
io.write_file("out.html", s)
```

#### 3B: Template engine (data-driven)

```ls
import html

string tmpl = "
<html><body>
  <h1>{{title}}</h1>
  {{#each items}}
    <li>{{name}} — {{score}}</li>
  {{/each}}
</body></html>
"

map(string, json.Value) ctx = json.object()
json.set(ctx, "title", json.str("Results"))
json.Value items = json.array()
// ... populate items ...

string output = html.render_template(tmpl, ctx)
```

Template syntax (Mustache-inspired):
- `{{key}}` — interpolate and HTML-escape
- `{{{key}}}` — raw interpolation (no escaping)
- `{{#each list}} ... {{name}} ... {{/each}}`
- `{{#if cond}} ... {{else}} ... {{/if}}`

### Implementation

Pure LS. The builder builds an internal tree of `HtmlNode` structs (tag, attrs, children),
then serialises. The template engine is a simple recursive descent parser over the template string.

**Effort**: OF.3A: 4–6 days · OF.3B: 4–5 days additional

---

## Phase OF.4 — SVG & Basic Charting

### 4A: SVG builder

```ls
import svg

svg.Doc doc = svg.document(width: 800, height: 600)

// Primitives:
svg.rect(doc,   x: 10, y: 10, w: 100, h: 50, fill: "steelblue")
svg.circle(doc, cx: 200, cy: 100, r: 40, fill: "coral", stroke: "black")
svg.line(doc,   x1: 0, y1: 0, x2: 800, y2: 600, stroke: "gray", width: 1.0)
svg.text(doc,   x: 50, y: 300, content: "Hello SVG", font_size: 16)
svg.path(doc,   d: "M 0 0 L 100 100 Z", fill: "none", stroke: "black")

// Groups (for transforms):
svg.Group g = svg.group(doc)
svg.translate(g, 100, 50)
svg.rotate(g, 45.0)
svg.rect(g, x: 0, y: 0, w: 50, h: 50, fill: "gold")

string output = svg.render(doc)
io.write_file("chart.svg", output)
```

### 4B: Chart library (built on 4A)

```ls
import chart

// Line chart:
chart.Chart c = chart.line_chart(
    title: "CPU Usage",
    x_label: "Time (s)",
    y_label: "Usage (%)"
)
chart.add_series(c, label: "Core 0", x: time_vec, y: cpu0_vec, color: "steelblue")
chart.add_series(c, label: "Core 1", x: time_vec, y: cpu1_vec, color: "coral")
chart.render_svg(c, "cpu_usage.svg", width: 1200, height: 400)

// Bar chart:
chart.Chart bc = chart.bar_chart(title: "Scores", x_label: "Name", y_label: "Score")
chart.add_bars(bc, labels: ["Alice","Bob","Carol"], values: [9.5, 8.1, 7.3])
chart.render_svg(bc, "scores.svg")

// Scatter plot:
chart.Chart sc = chart.scatter(title: "Latency vs Load")
chart.add_points(sc, x: load_vec, y: latency_vec)
chart.render_svg(sc, "scatter.svg")

// Heatmap (useful for profiling / matrix visualisation):
chart.Chart hm = chart.heatmap(data: matrix_2d, color_scheme: chart.VIRIDIS)
chart.render_svg(hm, "heatmap.svg")
```

### Implementation

Pure LS. The chart library maps data to SVG primitives via the OF.4A builder.
Axis scaling, tick generation, legend placement all in LS.

**Effort**: OF.4A: 5–7 days · OF.4B: 7–10 days additional

---

## Phase OF.5 — PlantUML & Mermaid Diagrams

### API

```ls
import puml    // PlantUML text generation

// Sequence diagram:
puml.Seq s = puml.sequence()
puml.participant(s, "Client")
puml.participant(s, "Server")
puml.arrow(s, from: "Client", to: "Server", label: "HTTP GET /api")
puml.arrow(s, from: "Server", to: "Client", label: "200 OK", style: "-->")
string text = puml.render(s)    // emits @startuml ... @enduml text

// Class diagram:
puml.Class cls = puml.class_diagram()
puml.add_class(cls, "Animal", fields: ["name: string"], methods: ["speak() void"])
puml.add_class(cls, "Dog",    fields: [],               methods: ["speak() void"])
puml.inheritance(cls, child: "Dog", parent: "Animal")
string text = puml.render(cls)

// Write and optionally render (if plantuml.jar is available):
io.write_file("diagram.puml", text)
puml.render_to_png(text, "diagram.png")   // calls plantuml.jar via process.run
```

```ls
import mermaid

// Flowchart:
mermaid.Flow f = mermaid.flowchart(direction: "TD")
mermaid.node(f, "A", "Start")
mermaid.node(f, "B", "Process")
mermaid.node(f, "C", "End")
mermaid.edge(f, "A", "B", label: "begin")
mermaid.edge(f, "B", "C", label: "done")
string text = mermaid.render(f)   // emits ```mermaid ... ``` block
```

### Implementation

Pure LS string generation. No external dependency for text output.
Rendering to image requires `plantuml.jar` (Java) or `mmdc` (Mermaid CLI)
invoked via `process.run()` (Phase S.6).

**Effort**: OF.5: 3–5 days

---

## Phase OF.6 — Excel / XLSX

### API

```ls
import xlsx

xlsx.Workbook wb = xlsx.workbook()

xlsx.Sheet sh = xlsx.add_sheet(wb, "Results")
xlsx.write(sh, row: 0, col: 0, value: "Name")   // string cell
xlsx.write(sh, row: 0, col: 1, value: "Score")
xlsx.write(sh, row: 1, col: 0, value: "Alice")
xlsx.write(sh, row: 1, col: 1, value: 9.5)       // numeric cell
xlsx.write(sh, row: 2, col: 1, value: true)       // bool cell

// Formulas:
xlsx.formula(sh, row: 3, col: 1, formula: "=AVERAGE(B2:B2)")

// Styling:
xlsx.Format bold = xlsx.bold_format(wb)
xlsx.set_format(sh, row: 0, col: 0, fmt: bold)

// Chart in sheet:
xlsx.Chart c = xlsx.add_chart(wb, xlsx.CHART_BAR)
xlsx.chart_add_series(c, values: "=Results!$B$2:$B$3")
xlsx.insert_chart(sh, c, row: 5, col: 0)

xlsx.save(wb, "output.xlsx")
```

### Implementation options

**Option A** (recommended): FFI wrapper around `libxlsxwriter` (C library, MIT license).
The `extern` FFI is mature enough to wrap it.
No pure-LS implementation needed — the C library handles the OOXML zip format.

**Option B**: Pure-LS XLSX writer.
XLSX is a ZIP file containing XML. Requires a zip library (either FFI `libzip` or
a pure-LS inflate/deflate implementation). High complexity.

**Recommended**: Option A.

```bash
# Build dependency (Linux)
sudo apt-get install -y libxlsxwriter-dev

# Windows: vcpkg install xlsxwriter
```

**Effort**: Option A: 5–7 days · Option B: 21+ days

---

## Phase OF.7 — Markdown Document Generation

### API

```ls
import md

md.Doc doc = md.document()

md.h1(doc, "Report Title")
md.h2(doc, "Section 1")
md.p(doc, "This is a paragraph with **bold** and _italic_ text.")

// Code block:
md.code(doc, language: "ls", content: "fn main() { print(42) }")

// Table:
md.Table t = md.table(doc, headers: ["Name", "Score", "Grade"])
md.row(t, ["Alice", "9.5", "A"])
md.row(t, ["Bob",   "8.1", "B"])

// List:
md.List l = md.ul(doc)             // unordered list
md.item(l, "First item")
md.item(l, "Second item")

// Callout (GitHub-flavored):
md.note(doc, "This is an important note.")
md.warning(doc, "Be careful here.")

string text = md.render(doc)
io.write_file("report.md", text)
```

**Effort**: 2–3 days

---

## Phase Summary & Priority Order

| Phase | Feature | Depends on | Effort | Value |
|-------|---------|-----------|--------|-------|
| OF.1 | JSON | stdlib S.5 | 7–10 d | ★★★★★ |
| OF.2 | CSV | none | 3–4 d | ★★★★☆ |
| OF.3A | HTML builder | none | 4–6 d | ★★★☆☆ |
| OF.3B | HTML templates | OF.1, OF.3A | 4–5 d | ★★★★☆ |
| OF.4A | SVG | none | 5–7 d | ★★★☆☆ |
| OF.4B | Charts | OF.4A | 7–10 d | ★★★★☆ |
| OF.5 | PlantUML / Mermaid | stdlib S.6 | 3–5 d | ★★★☆☆ |
| OF.6 | Excel / XLSX | FFI maturity | 5–7 d | ★★★★☆ |
| OF.7 | Markdown | none | 2–3 d | ★★★☆☆ |

**Recommended order**: OF.2 (CSV, quick win) → OF.7 (Markdown, quick win) →
OF.1 (JSON, high value) → OF.4A+B (SVG+charts) → OF.3A+B (HTML) → OF.6 (Excel) → OF.5 (diagrams)
