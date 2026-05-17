// Phase V.2 — vec functional methods: filter, find, find_index

fn main() {
    vec(int) nums = [3, 1, 4, 1, 5, 9, 2, 6]

    // === filter: keep elements > 3 ===
    vec(int) big = nums.filter(|x| x > 3)
    if big.length != 4 {
        print("FAIL: filter(x > 3) expected len 4 got")
        print(big.length)
        return
    }
    // Verify contents: 4, 5, 9, 6
    if big.get(0) != 4 {
        print("FAIL: filter[0] expected 4")
        return
    }
    if big.get(1) != 5 {
        print("FAIL: filter[1] expected 5")
        return
    }
    if big.get(2) != 9 {
        print("FAIL: filter[2] expected 9")
        return
    }
    if big.get(3) != 6 {
        print("FAIL: filter[3] expected 6")
        return
    }
    print("PASS: filter(x > 3) = [4,5,9,6]")

    // === filter: none match ===
    vec(int) none = nums.filter(|x| x > 100)
    if none.length != 0 {
        print("FAIL: filter(x > 100) expected empty")
        return
    }
    print("PASS: filter(x > 100) = []")

    // === filter: all match ===
    vec(int) all = nums.filter(|x| x > 0)
    if all.length != 8 {
        print("FAIL: filter(x > 0) expected len 8")
        return
    }
    print("PASS: filter(x > 0) keeps all")

    // === filter on empty vec ===
    vec(int) empty = []
    vec(int) ef = empty.filter(|x| x > 0)
    if ef.length != 0 {
        print("FAIL: empty.filter expected empty")
        return
    }
    print("PASS: empty.filter = []")

    // === filter with string elements (clone test) ===
    vec(string) names = []
    string n1 = "alice"
    string n2 = "bob"
    string n3 = "charlie"
    names.push(n1)
    names.push(n2)
    names.push(n3)
    vec(string) long_names = names.filter(|s| s.length > 3)
    if long_names.length != 2 {
        print("FAIL: string filter expected 2 got")
        print(long_names.length)
        return
    }
    print("PASS: string filter(len > 3) = 2 elements")

    // === find_index: first > 4 ===
    int idx = nums.find_index(|x| x > 4)
    if idx != 4 {
        print("FAIL: find_index(x > 4) expected 4 got")
        print(idx)
        return
    }
    print("PASS: find_index(x > 4) = 4")

    // === find_index: not found ===
    int idx2 = nums.find_index(|x| x > 100)
    if idx2 != -1 {
        print("FAIL: find_index(x > 100) expected -1 got")
        print(idx2)
        return
    }
    print("PASS: find_index(x > 100) = -1")

    // === find: first > 7 ===
    Option(int) found = nums.find(|x| x > 7)
    match found {
        Some(v) => {
            if v != 9 {
                print("FAIL: find(x > 7) expected Some(9)")
                return
            }
            print("PASS: find(x > 7) = Some(9)")
        }
        None => {
            print("FAIL: find(x > 7) expected Some, got None")
            return
        }
    }

    // === find: not found ===
    Option(int) nf = nums.find(|x| x > 100)
    match nf {
        Some(v) => {
            print("FAIL: find(x > 100) expected None, got Some")
            return
        }
        None => {
            print("PASS: find(x > 100) = None")
        }
    }

    // === find with string (clone test) ===
    Option(string) fs = names.find(|s| s.length == 3)
    match fs {
        Some(s) => {
            if s.compare("bob") != 0 {
                print("FAIL: find(len==3) expected 'bob'")
                return
            }
            print("PASS: find(len==3) = Some(bob)")
        }
        None => {
            print("FAIL: find string expected Some, got None")
            return
        }
    }

    print("ALL PASS")
}
