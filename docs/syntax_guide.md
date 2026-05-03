# LS 语法示例速查

## 变量与基本类型

```ls
int x = 42
string name = "hello"
f64 pi = 3.14159
bool flag = true
*int ptr = malloc(sizeof(int))
```

## 函数

```ls
// 有返回值
fn factorial(int n) -> int {
    match n {
        0 => 1,
        _ => n * factorial(n - 1),
    }
}

// 无返回值（默认 void）
fn greet(string name) {
    print(f"Hello, {name}!")
}

// 闭包（匿名 fn）
fn(int) -> int double = fn(int x) -> int { x * 2 }
```

## print 与格式化字符串

```ls
print(42)                          // 42
print(3.14)                        // 3.140000
print(true)                        // true
print("hello", 42, 3.14)          // hello 42 3.140000

int age = 30
print(f"Age: {age}, double: {age * 2}")   // Age: 30, double: 60
string msg = f"result = {1 + 2}"          // 赋值给变量
```

## String 方法

```ls
string s = "hello"
int len = s.length          // 5（O(1)）
bool empty = s.empty()      // false
int ch = s.at(0)            // 104 (ASCII 'h')
int pos = s.find("llo")     // 2（未找到返回 -1）
bool has = s.contains("ell")
bool sw = s.starts_with("hel")
bool ew = s.ends_with("llo")
int cmp = s.compare("world")        // < 0

string up = s.upper()               // "HELLO"
string lo = "HELLO".lower()         // "hello"
string sub = s.substr(1, 3)         // "ell"
string trimmed = "  hi  ".trim()    // "hi"
string rep = s.replace("l", "r")    // "herro"

string full = "hello" + " world"    // 拼接
bool eq = ("abc" == "abc")          // true（值比较）

s.append(" world")                  // 原地追加（string | char | int）
s += "!"                            // 等价：原地 append

// 查找扩展
int last = s.rfind("l")             // 最后一次出现的位置（-1 = 未找到）
int n    = s.count("l")             // 非重叠子串出现次数

// 拆分与拼接
vec(string) parts = "a,b,c".split(",")    // ["a", "b", "c"]
string joined = "-".join(parts)            // "a-b-c"
// 边界：sep="" 返回 [src]；空 src 返回 [""]；相邻 sep 产生空段
```

## 借用：`&T` / `&!T`

借用是"不转移所有权"的函数传参方式，形参作用域结束时不释放源内存。当前支持 `string` 和 `vec(T)` 两种借用，仅用于函数参数位置。

### `&string` / `&!string`

```ls
// 只读借用：形参不能修改内容
fn show(&string s) { print(s) }

// 可写借用：形参可修改内容（= / += / .append），但不能 move 走
fn grow(&!string s) {
    s += " world"
    s.append('!')
}

fn main() -> int {
    string x = "hello".upper()
    show(x)          // auto-borrow：无需写 &x
    grow(&!x)        // 可写借用必须显式 &!x
    print(x)         // HELLO world!
    return 0
}
```

### `&vec(T)` / `&!vec(T)`

```ls
// 只读借用 vec：可读不可写
fn sum(&vec(int) v) -> int {
    int s = 0
    for (int i = 0; i < v.length; i = i + 1) { s = s + v[i] }
    return s
}

// 可写借用 vec：push / v[i] = x 均写穿回 caller
fn double_all(&!vec(int) v) {
    for (int i = 0; i < v.length; i = i + 1) { v[i] = v[i] * 2 }
    v.push(999)
}

fn main() -> int {
    vec(int) v
    v.push(3); v.push(5)
    print(sum(v))        // auto-borrow: 8
    double_all(&!v)
    print(v.length)      // 3
    print(v[0])          // 6
    print(v[2])          // 999
    return 0
}
```

### `&map(K,V)` / `&!map(K,V)`

```ls
// 只读借用 map：可读不可写
fn total(&map(string, int) m) -> int {
    int s = 0
    for k in m.keys() { s = s + m.get(k) }
    return s
}

// 可写借用 map：set / remove / clear 均写穿回 caller
fn populate(&!map(string, int) m) {
    m.set("a", 1)
    m.set("b", 2)
    m.remove("stale")
}

fn main() -> int {
    map(string, int) m
    populate(&!m)
    print(total(m))           // auto-borrow: 3
    print(m.contains_key("a")) // true
    return 0
}
```

### `&struct` / `&!struct`（仅 POD struct）

```ls
struct Point { f64 x; f64 y; }

// 只读借用 struct：可读字段
fn dist_sq(&Point p) -> f64 {
    return p.x * p.x + p.y * p.y
}

// 可写借用 struct：字段写穿回 caller
fn translate(&!Point p, f64 dx, f64 dy) {
    p.x = p.x + dx
    p.y = p.y + dy
}

fn main() -> int {
    Point p = Point { x: 3.0, y: 4.0 }
    print(dist_sq(p))      // auto-borrow: 25
    translate(&!p, 1.0, 1.0)
    print(p.x)             // 4
    return 0
}
```

> **限制**（Phase 5.8 first slice）：
> - 仅支持 **POD struct**（无 `string` / `vec` / `map` / 自定义 `__drop` 字段）。含 drop 字段的 struct 借用待后续 phase 实现。
> - 不支持在借用上调用实例方法（如 `p.show()`）—— 需要 `&!self` 机制，独立议题。
> - 静态方法（`StructName.method()`）和字段读写均可正常使用。

### 关键规则

- 只读借用支持 auto-borrow（实参写 `x` 即可）；可写借用**必须显式** `&!x`
- `&!T` 可向 `&T` 降级；`&T` 不能升级为 `&!T`
- 借用禁止转移所有权：`vec.push(s)` / `__move(s)` / `T t = borrow`（copy out）全部被拒
- `&vec(T)` 禁止所有 mutating 方法（`push/pop/clear/set/...`）与 `v[i] = x`；`&!vec(T)` 全部允许
- `&map(K,V)` 禁止 `set` / `remove` / `clear`；`&!map(K,V)` 全部允许
- `&struct` 禁止 `s.field = x` 与实例方法调用；`&!struct` 允许字段写但仍禁实例方法调用
- 同一 call 内不允许同名变量多次借用（`f(&!x, x)` / `f(&!x, &!x)` 均报错）
- **ABI 细节**：`&string` 走 by-value（16 字节 POD），其余 `&!string` / `&vec(T)` / `&!vec(T)` / `&map(K,V)` / `&!map(K,V)` / `&struct` / `&!struct` 均走 pointer

## Struct + impl

```ls
struct Point {
    f64 x;
    f64 y;
}

impl Point {
    // 实例方法：self 自动注入为 *Point
    fn distance(Point other) -> f64 {
        f64 dx = self.x - other.x
        f64 dy = self.y - other.y
        sqrt(dx * dx + dy * dy)
    }

    // 静态方法
    static fn origin() -> Point {
        Point p
        p.x = 0.0
        p.y = 0.0
        return p
    }
}

Point p
p.x = 3.0
p.y = 4.0
Point o = Point.origin()
f64 d = p.distance(o)      // 5.0
```

### 显式 self 借用：`&self` / `&!self`（Phase A1 / B）

实例方法可在第一参数位置显式声明 self 的借用形态：

| 写法 | 语义 | 调用对象允许 |
|---|---|---|
| `fn m()`（旧式） | self 是 `*Struct` 伪指针 | 只能 owned；不可在 `&Struct`/`&!Struct` 上调用 |
| `fn m(&self, ...)` | self 是只读借用，禁止 `self.field = ...` | owned / `&Struct` / `&!Struct`（自动降级）|
| `fn m(&!self, ...)` | self 是可写借用，允许 `self.field = ...` | owned / `&!Struct`（不能通过 `&Struct` 调用）|

```ls
struct Point { f64 x; f64 y; }

impl Point {
    fn show(&self) { print(self.x) }
    fn shift(&!self, f64 dx) { self.x = self.x + dx }
}

fn bump(&!Point p) { p.shift(5.0) }     // 显式 &! 借用 + &!self 方法
fn peek(&Point p)  { p.show() }          // &Struct 上调用 &self 方法

Point p = Point { x: 1.0, y: 2.0 }
p.shift(10.0)   // owned 上调用 &!self
bump(&!p)
peek(p)         // 自动 &Struct
```

限制：
- `&self`/`&!self` 仅限 `impl` 块内的实例方法；顶层函数与 `static fn` 不允许
- 含 `string`/`vec`/`map` 字段的 struct 也支持（Phase B 解除 POD 限制）；`&self.field` 读 string/drop 字段会返回 owned clone
- 旧式 `fn m()` 形式保留，但无法在借用对象上调用（需迁移到 `&self`/`&!self`）

## 控制流

```ls
// C 风格 for
int sum = 0
for (int i = 0; i < 10; i = i + 1) {
    sum = sum + i
}
for (;;) { break }   // 无限循环

// for-in range
for i in 0..10 { print(i) }    // [0, 10)
for i in 5 { print(i) }        // 等价于 for i in 0..5

// while
while sum > 0 { sum = sum - 1 }

// match
match n {
    0 => print("zero"),
    1 => print("one"),
    _ => print("other"),
}
```

## enum (Phase 8)

`enum` 定义代数数据类型（tagged union），支持无 payload / 带 payload / 自递归三种变体形态。
体内分隔符 `;` / `,` / 换行任一可省（与 struct 字段一致）。
变体名首字母大写（与 binder 区分）。

```ls
// C 风格 + payload variants 混用
enum Shape {
    Point
    Circle(int r)                    // 命名 payload
    Rect(int w, int h)
}

// 自递归（编译器自动堆 boxing）
enum Tree {
    Leaf
    Node(int value, Tree left, Tree right)
}

fn area(Shape s) -> int {
    match s {                         // 强制穷尽性：缺 variant 编译错
        Point => 0
        Circle(r) => r * r * 3
        Rect(w, h) => w * h
    }
}

fn sum(Tree t) -> int {
    match t {
        Leaf => 0
        Node(v, l, r) => v + sum(l) + sum(r)
    }
}

// 含 string payload 的 enum：自动生成 drop 函数 + 作用域 RAII 清理
enum Event {
    Quit
    Message(string text)
}
```

### 内建 Option(T) / Result(T, E)

按需单态化（同 `vec(T)` / `map(K,V)` 风格）。构造表达式按上下文类型自动消歧。

```ls
Option(int) a = Some(42)
Option(int) b = None
Option(string) c = Some("hi")           // 同时存在 Option(int)/Option(string) → 按声明类型选择

Result(int, string) r1 = Ok(100)
Result(int, string) r2 = Err("not found")

match a {
    Some(x) => print(x)                 // 42
    None => print(-1)
}
match r2 {
    Ok(v) => print(v)
    Err(msg) => print(msg)              // "not found"
}

// 典型错误传播模式
fn safe_div(int n, int d) -> Result(int, string) {
    match d {
        0 => Err("divide by zero")
        _ => Ok(n / d)
    }
}
```

详细设计与实现说明见 [docs/enum_design.md](enum_design.md)。

## 固定大小数组

```ls
array(int, 3) nums = [10, 20, 30]
int first = nums[0]
nums[1] = 99
int len = nums.length           // 3（编译期常量）
for x in nums { print(x) }
print(nums)                     // [10, 99, 30]
```

## vec(T) 动态数组

```ls
vec(int) v
v.push(1)
v.push(2)
v.push(3)
int top = v.pop()           // 3
int val = v.get(0)          // 1（越界返回零值 + warning）
v.set(0, 99)
int n = v.len()             // 2
bool empty = v.is_empty()

// 字面量初始化：vec(T) v = [..]
vec(int) nums = [1, 2, 3]              // 等价于 push(1); push(2); push(3)
vec(string) tags = ["alpha", "beta"]   // string 字面量直接进 vec
vec(int) empty_v = []                  // 空向量字面量需要显式类型上下文

vec(string) sv
sv.push("hello".upper())    // owned string，push 后 move
```

## map(K, V) 哈希映射

```ls
map(string, int) scores
scores.set("Alice", 95)
scores.set("Bob", 87)
int s = scores.get("Alice")        // 95
bool has = scores.contains_key("Bob")
scores.remove("Bob")
int count = scores.length          // 1
bool empty = scores.is_empty()
scores.clear()
```

## 全局变量

```ls
int counter = 0

fn increment() {
    counter = counter + 1
}
```

## 模块系统

```ls
// math.ls
module math
fn add(int a, int b) -> int { a + b }
int MAGIC = 42

// main.ls
import math
int result = math.add(1, 2)
print(math.MAGIC)
```

## C FFI

```ls
lib msvcrt = load("msvcrt.dll")

// 方式 1：extern fn（有类型检查）
extern fn puts(string s) -> int from msvcrt
extern fn printf(string fmt, ...) -> int from msvcrt
puts("Hello from FFI!")

// 方式 2：动态调用（unsafe）
msvcrt.call("puts", "hello dynamic")
```

## object 类型擦除

```ls
*int p = malloc(sizeof(int))
object obj = p           // *T → object 隐式转换
*int q = obj as *int     // object → *T 需显式 as

free(p)
```

## 析构函数（__drop）

```ls
struct Person {
    string name;
    int age;
}

// 自动生成 __drop（含 string 字段时）
// 也可自定义：
impl Person {
    fn __drop() {
        print("Person dropped")
        // string 字段由编译器自动释放，无需手动 free
    }
}
```
