fn main() -> int {
    string str
    print(str.at(0))
    for i in 0..4 {
        str += "a"
    }

    print(str.at(100))
    print(str.at(-1))
    print(str.at(0))
    print(str.at(3))

    return 0
}
