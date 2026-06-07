// rawvec_realloc_test.ls — Step 1: expose realloc() to LS surface.
// Verifies malloc -> realloc (grow) -> realloc (shrink) -> free type-checks,
// and that memcheck tracks the realloc chain as a SINGLE migrating object
// (no leak, no double-free, no invalid free). Pure *u8 — no indexing/sizeof/cast,
// so this isolates Step 1 from later steps.

fn main() {
    // allocate, then grow twice, then shrink — all through realloc
    *u8 p = malloc(16)
    p = realloc(p, 64)
    p = realloc(p, 256)
    p = realloc(p, 8)
    free(p)

    // realloc(NULL, n) must behave like malloc(n)
    *u8 q = nil
    q = realloc(q, 32)
    free(q)

    print("RAW1 PASS")
}
