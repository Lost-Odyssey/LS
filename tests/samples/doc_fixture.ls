//! doc_fixture — tiny module for the `ls doc` regression test.

/// Add two integers and return the sum.
def add(int a, int b) -> int { return a + b }

/// A 2D point with integer coordinates.
struct Point { int x; int y }

methods Point {
    /// Manhattan distance from the origin.
    def manhattan(&self) -> int { return self.x + self.y }
}

// internal helper — must NOT appear in the generated docs (`_` prefix)
def _scratch() -> int { return 0 }
