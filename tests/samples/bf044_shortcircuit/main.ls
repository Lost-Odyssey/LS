/* BF-044 regression: has_drop struct `vec[i].field` on the RHS of a short-circuit
   && / || tripped "Instruction does not dominate all uses" — the M-4.5 spilled
   struct-clone alloca lived in the conditionally-executed RHS block but its drop
   was emitted at the enclosing statement boundary (not dominated by that block).
   Fix flushes RHS temps inside the RHS block before merging. */

int drop_count = 0

struct Tag { string label }
impl Tag {
    fn __drop() { drop_count = drop_count + 1 }
}

fn main() -> int {
    vec(Tag) t = [Tag { label: "A" }, Tag { label: "B" }, Tag { label: "C" }]

    /* && with field access on the RHS (the crashing case) */
    if t[0].label == "A" && t[1].label == "B" {
        print("AND_OK")
    }

    /* || with field access on the RHS */
    if t[0].label == "no" || t[2].label == "C" {
        print("OR_OK")
    }

    /* three-way && chain — multiple conditional RHS blocks */
    if t[0].label == "A" && t[1].label == "B" && t[2].label == "C" {
        print("CHAIN_OK")
    }

    /* The exact __drop count depends on M-4.5 spill-clone behaviour (an impl
       detail); what matters for BF-044 is: no dominance crash, correct branching,
       and memcheck stays clean. Print the count for diagnostics only. */
    print(f"drops={drop_count}")
    print("BF044 PASS")
    return 0
}
