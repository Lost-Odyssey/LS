// Test: 编译器生成的 __drop() 应该可以被显式调用
struct Person {
    string name;
}
// Person 有 string 字段，has_drop=true，编译器应该自动生成 __drop 并注册为方法

struct Outer {
    Person person;
}

impl Outer {
    fn __drop() {
        print("Outer.__drop called")
        // 现在应该可以显式调用 Person.__drop() 了
        self.person.__drop()
    }
}

fn main() -> int {
    Outer o
    o.person.name = "Alice"
    print(o.person.name)
    print("main: exiting...")
    return 0
}