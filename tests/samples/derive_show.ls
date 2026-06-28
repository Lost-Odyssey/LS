// Stage 1: @derive(Show) synthesizes def show(&self, &!Sink) field-by-field.
// Each field renders via its own Show impl into a shared sink. to_str(x) is the
// Str-producing front. Format matches @print(): Name { f: v, ... }.
@derive(Show)
struct Point { int x; int y }

@derive(Show)
struct Named { Str tag; int n }

@derive(Show)
struct Line { Point a; Point b }

def main() {
    Point p = Point { x: 3, y: 4 }
    @print(to_str(p))
    Named nm = Named { tag: "hi", n: 5 }
    @print(to_str(nm))
    Line ln = Line { a: Point { x: 1, y: 2 }, b: Point { x: 3, y: 4 } }
    @print(to_str(ln))
    Str msg = "got " + to_str(p)      // composable into a larger Str
    @print(msg)
    @print("DERIVE SHOW DONE")
}
