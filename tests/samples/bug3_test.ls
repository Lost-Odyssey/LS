struct Person {
    int age;
    int id;
    Str name;
}

def main() -> int {
    Person p1 = Person{age:10, id:1, name:"Kimy"}
    @print(p1)
    return 0
}
