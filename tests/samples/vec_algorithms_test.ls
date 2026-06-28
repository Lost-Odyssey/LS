// vec_algorithms_test.ls — practical algorithms using Vec(T)
import std.core.vec

// ---- Bubble sort ----
def bubble_sort(&!Vec(int) v) {
    int n = v.len()
    for i in 0..n {
        for j in 0..(n - 1) {
            if (v[j] > v[j + 1]) {
                int tmp = v[j]
                v[j] = v[j + 1]
                v[j + 1] = tmp
            }
        }
    }
}

// ---- Linear search: return index or -1 ----
def find_first(&Vec(int) v, int target) -> int {
    for i in 0..v.len() {
        if (v[i] == target) { return i }
    }
    return -1
}

// ---- Filter: return new vec with elements > threshold ----
def filter_gt(&Vec(int) v, int threshold) -> int {
    Vec(int) result = {}
    for (int i = 0; i < v.len(); i = i + 1) {
        int x = v[i]
        if (x > threshold) { result.push(x) }
    }
    int s = 0
    for (int i = 0; i < result.len(); i = i + 1) {
        int x = result[i]
        s = s + x
    }
    return s
}

// ---- Map: multiply each element by factor, return sum ----
def map_sum(&Vec(int) v, int factor) -> int {
    int s = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        int x = v[i]
        s = s + x * factor
    }
    return s
}

// ---- Reverse in place ----
def reverse_vec(&!Vec(int) v) {
    int lo = 0
    int hi = v.len() - 1
    for i in 0..(v.len() / 2) {
        int tmp = v[lo]
        v[lo] = v[hi]
        v[hi] = tmp
        lo = lo + 1
        hi = hi - 1
    }
}

def main() -> int {
    // === Bubble sort ===
    Vec(int) data = {}
    data.push(5)
    data.push(2)
    data.push(8)
    data.push(1)
    data.push(9)
    data.push(3)
    bubble_sort(&!data)
    @print(data[0])   // 1
    @print(data[1])   // 2
    @print(data[2])   // 3
    @print(data[3])   // 5
    @print(data[4])   // 8
    @print(data[5])   // 9

    // === Linear search ===
    int idx = find_first(data, 8)
    @print(idx)       // 4

    int miss = find_first(data, 99)
    @print(miss)      // -1

    // === Filter (elements > 4, sum them) ===
    int fsum = filter_gt(data, 4)
    @print(fsum)      // 5+8+9 = 22

    // === Map sum (each * 2) ===
    int msum = map_sum(data, 2)
    @print(msum)      // (1+2+3+5+8+9)*2 = 56

    // === Reverse ===
    Vec(int) rev = {}
    rev.push(1)
    rev.push(2)
    rev.push(3)
    rev.push(4)
    rev.push(5)
    reverse_vec(&!rev)
    @print(rev[0])   // 5
    @print(rev[1])   // 4
    @print(rev[2])   // 3
    @print(rev[3])   // 2
    @print(rev[4])   // 1

    // === Reserve + batch push ===
    Vec(int) big = {}
    big.reserve(32)
    for i in 0..10 { big.push(i) }
    @print(big.len())      // 10
    @print(big.cap() >= 32)   // 1

    return 0
}
