# LS String 语义完整分析

> 版本：Direction A 实现后（2026-05-16）  
> 涵盖：所有场景下的内存模型、与主流语言对比、未来改进计划

---

## 1. 内存模型：三态 cap 字段

LS string 的底层表示：

```c
typedef struct {
    char *data;   // 指向字符数据的指针
    int   len;    // 字符串长度（不含 '\0'）
    int   cap;    // 容量——同时充当所有权标记
} LsString;
```

`cap` 字段有三种语义状态：

| cap 值   | 状态       | 含义                          | 退出作用域时 |
|---------|----------|-------------------------------|------------|
| `== 0`  | Static / Borrowed | data 指向只读内存（`.rodata` 或 caller 的 buffer） | 跳过 free  |
| `> 0`   | Owned    | data 指向 malloc 的 buffer，当前变量持有所有权 | `free(data)` |
| `== -1` | Moved    | 所有权已转移，data 指针已失效（逻辑上） | 跳过 free  |

这个三态模型是整个 string 语义的基础，所有场景都从它推导。

---

## 2. 各场景下的语义

### 2.1 字面量与变量声明

```ls
string a = "hello"           // static:  cap=0, data→.rodata，永不 free
string b = "hello".upper()   // owned:   cap=8, data→malloc，退出时 free
string c = b                 // clone:   cap=8, data→新 malloc（深拷贝），退出时 free
```

赋值 `string c = b` 触发 `emit_string_clone_val`：
- `cap > 0` → malloc(cap) + memcpy → c 有独立 buffer
- b 本身不受影响（b 和 c 各自拥有独立副本）

### 2.2 函数参数（Direction A，2026-05-16 生效）

```ls
fn greet(string name) {     // name: is_borrow=true，cap 强制置 0
    print(f"hi {name}")     // ✅ 只读操作
    // v.push(name)         // ❌ 编译错误：cannot move into vec: borrow
    // name += "!"          // ❌ 编译错误：cannot assign: read-only borrow
}

fn main() {
    string s = "Alice".upper()   // cap=8
    greet(s)                     // s 传入后：callee 内 cap=0，s 本身 cap 不变
    print(s)                     // ✅ s 仍然可用，由 main 退出时 free
}
```

**codegen 路径**：
1. 调用者：LLVM by-value 传递 `{data, len, 8}`（struct 拷贝）
2. 被调方 entry block：`InsertValue(param_alloca, 0, field=2)` → cap 置 0
3. 被调方 scope cleanup：`cap == 0` → 跳过 free
4. 调用者 scope cleanup：`cap == 8 > 0` → `free(data)` ✅

**等价语义**：`string s` 参数 ≡ `&string s`（Direction A 后 checker 行为完全一致）。

### 2.3 显式只读借用 `&string`

```ls
fn print_len(&string s) {   // is_borrow=true，ABI 同 plain string（cap=0）
    print(s.len)             // ✅
}
```

Direction A 后，`string s` 和 `&string s` 语义完全统一，`&string` 更多是语义文档用途。

### 2.4 可写借用 `&!string`

```ls
fn append_bang(&!string s) {   // ABI: LsString*（指向 caller 的 struct 的指针）
    s += "!"                    // ✅ 通过指针修改 caller 的 buffer（realloc）
    // v.push(s)               // ❌ 编译错误
}

fn main() {
    string msg = "hello".upper()
    append_bang(&!msg)
    print(msg)   // "HELLO!" — 修改对 caller 可见
}
```

**codegen 路径**：
- 调用者：传 `&msg`（LsString* 指针）
- 被调方：`sym->value` 是存有指针的 alloca；`s += "!"` 通过指针解引用做 realloc
- `s += "!"` 修改 caller 的 `{data, len, cap}` 字段 in-place
- 被调方 scope cleanup：`is_mut_borrow=true` → 跳过 free（只是指针别名）

| 操作 | `string s` | `&string s` | `&!string s` |
|------|-----------|------------|-------------|
| ABI  | 16B struct copy，cap=0 | 同左 | `LsString*` 指针 |
| 读   | ✅ | ✅ | ✅（解引用） |
| `+=` / `.append` | ❌ checker 拒绝 | ❌ | ✅ realloc caller buffer |
| `v.push(s)` | ❌ | ❌ | ❌ |
| caller 可见改动 | ❌ | ❌ | ✅ |

### 2.5 返回值

```ls
fn make_tag(string prefix) -> string {
    return prefix + "_v1"    // 创建新 owned string（cap>0），转移给 caller
}

string tag = make_tag("foo")  // tag: cap>0，main 退出时 free
```

返回 owned string：移交所有权，callee scope cleanup 跳过（`return_alloca` skip 机制），caller 接管。

### 2.6 `vec.push` / `map.set`（所有权转移）

```ls
vec(string) v = []
string s = "hello".upper()   // cap=8
v.push(s)                    // s 转移所有权到 vec：s.cap → -1（MOVED）
// print(s)                  // ❌ 编译错误：use of moved variable
// v 退出时 free 元素
```

这是真正的 move 语义：
1. checker 标 `s.is_moved = true`（禁止后续使用）
2. codegen emit `mark_string_moved(s_alloca)` → cap = -1
3. vec element 存 `{data, len, cap}（原始 cap>0）`
4. vec drop 时 `free(element.data)`
5. `s` scope cleanup：cap == -1 → 跳过 ✅

### 2.7 闭包捕获（owned 局部变量）

```ls
type Fn = Block() -> string

fn make_tag(string suffix) -> Fn {
    string prefix = "TAG_".upper()   // 局部 owned，cap=8
    return || { return prefix + suffix }
}
```

`prefix` 是 owned 局部变量（cap=8）：
- env 存 `{data, len, 8}`（load 原值）
- outer alloca：`if cap > 0 { cap = -1 }` → 跳过 scope cleanup
- env_drop：`cap == 8 > 0` → `free(data)` ✅

### 2.8 闭包捕获（borrow 参数，Direction A + clone-on-capture）

```ls
fn make_greeter(string name) -> Greeter {
    return || { return "hi " + name }
    // name 是 borrow param（cap=0），捕获时 clone
}

fn get_greeter() -> Greeter {
    string n = "world".upper()   // cap=8
    return make_greeter(n)       // 闭包 env clone 了 n
    // n 在此退出，free(n.data)
    // env 有独立 clone，不受影响 ✅
}
```

**clone-on-capture 机制**（`cap = len` trick）：
```
param alloca: {data, len, cap=0}
↓ LoadLoad → str_val = {data, len, 0}
↓ InsertValue(str_val, len, field=2) → {data, len, cap=len}
↓ emit_string_clone_val：cap=len > 0 → malloc+memcpy → {data2, len, cap2>0}
→ env 持有独立 buffer，env_drop free(data2) ✅
```

| cap 情况 | 克隆结果 |
|---------|---------|
| len > 0 | malloc + memcpy → env 拥有 cap>0 的独立副本 |
| len = 0 | 空串，无 malloc，env 存 `{NULL, 0, 0}` |

### 2.9 闭包参数（Block 形参内的 string）

```ls
type Op = Block(string) -> string

fn apply(Op f, string x) -> string {
    return f(x)   // x 作为 borrow（cap=0）传入闭包 body
}
```

闭包 body 内接收到的 string 参数同样是 borrow（cap=0，is_borrowed=true）。body scope cleanup 跳过 free，caller（apply）保留所有权。

### 2.10 struct 字段中的 string

```ls
struct Person {
    string name
    int    age
}

Person p = Person { name: "Alice".upper(), age: 30 }
// name 字段：cap>0，p 的 __drop 负责 free
Person q = p   // 深拷贝：clone name 字段（emit_struct_clone_val）
```

struct 含 string 字段时自动标 `has_drop=true`，编译器合成 `Person.__drop` 调用 `free(self.name.data)`（cap > 0 时）。

---

## 3. 与主流语言对比

### 3.1 C++

```cpp
void greet(std::string name) { ... }           // 深拷贝，callee 完全拥有
void greet(const std::string& name) { ... }    // 只读引用，零拷贝
void greet(std::string&& name) { ... }         // move 引用，callee 接管

std::string s = "hello";
greet(s);                    // 拷贝
greet(std::move(s));         // move：s 置为空字符串（仍合法但为空）
```

| 特性 | C++ | LS |
|------|-----|-----|
| 默认传参 | 深拷贝（昂贵） | cap=0 借用（零拷贝）Direction A |
| move 语义 | `std::move()`，source 变为合法空串 | `v.push(s)`，source 标 MOVED 不可再用 |
| 可写引用 | `std::string&` | `&!string` |
| 只读引用 | `const std::string&` | `string s` / `&string s` |
| 字符串视图 | `std::string_view`（ptr+len，零拷贝） | 无（TODO） |
| move 后 source | 合法但空（可重用） | MOVED 不可用（更严格） |

**C++ 的问题**：`std::string name` 传参会隐式深拷贝，初学者常写出 `O(n)` 参数传递而不自知。

### 3.2 Go

```go
func greet(name string) { ... }         // 传 header 副本（16B），底层 []byte 共享
func greet(name *string) { ... }        // 指针（罕见，通常不需要）
func modify(buf []byte) { ... }         // 可变操作需要 []byte
```

| 特性 | Go | LS |
|------|-----|-----|
| string 类型 | 不可变值类型（ptr+len header） | 可变 LsString（ptr+len+cap） |
| 传参 | 拷贝 16B header，底层数据共享（无 free 问题） | cap=0 借用（Direction A） |
| 修改 | 必须转换为 `[]byte` | `&!string` |
| 所有权 | 无显式概念，GC 管理 | cap 三态 + RAII |
| 逃逸分析 | 编译器自动（GC 兜底） | 用户责任（无生命期系统） |
| 字面量 | 总是"借用" .rodata | cap=0 同 |

**Go 的优点**：string 不可变性消除了一大类 bug；GC 消除了所有权问题。  
**Go 的代价**：修改字符串需要 `[]byte` 转换；无法表达真正的 move 语义。

### 3.3 Rust

```rust
fn greet(name: String) { ... }           // 接管所有权（move）
fn greet(name: &str) { ... }             // 只读借用切片（ptr+len）
fn greet(name: &String) { ... }          // 只读引用（少用）
fn greet(name: &mut String) { ... }      // 可写引用

let s = String::from("hello");
greet(s);                                // s moved，不可再用（编译时检查）
let s2 = String::from("world");
greet(&s2);                              // 借用，s2 可继续用
```

| 特性 | Rust | LS |
|------|------|-----|
| 传参所有权 | `String` 参数 = move，`&str` = borrow | 目前无法 move 进函数（Direction A 全为 borrow） |
| 可变借用 | `&mut String`（生命期约束） | `&!string`（无生命期约束，用户责任） |
| 字符串切片 | `&str`（ptr+len，轻量，无堆）| 无独立切片类型（TODO） |
| 生命期 | 编译器强制（`'a`） | 无，靠约定 |
| move 后 source | 编译错误，不可访问 | 同（checker 拒绝使用 MOVED 变量） |
| 闭包 capture | `move |...| ...` 显式 move；默认 by-ref | Direction A：borrow 参数 clone-on-capture |

**Rust 的核心优势**：`&str` 和 `String` 的类型区分将"借用"和"拥有"编码进类型系统，生命期系统保证借用不会悬垂。  
**Rust 的代价**：学习曲线陡峭，借用检查器拒绝某些合理但难以验证的代码。

### 3.4 Zig

```zig
fn greet(name: []const u8) void { ... }   // 借用切片（ptr+len），只读
fn greet(name: []u8) void { ... }         // 借用切片，可写
fn greet(name: [:0]const u8) void { ... } // null-terminated 切片

// 所有权通过 allocator 显式管理
const s = try allocator.dupe(u8, "hello");  // 显式拷贝
defer allocator.free(s);
```

| 特性 | Zig | LS |
|------|-----|-----|
| 字符串类型 | `[]const u8` 切片（无堆概念） | `LsString`（含 cap 所有权标记） |
| 传参 | 切片（ptr+len），零拷贝借用 | cap=0 借用（Direction A） |
| 所有权 | allocator 显式管理 | cap 三态 + RAII |
| 可写 | `[]u8` | `&!string` |
| 隐式拷贝 | 无（必须显式 `allocator.dupe`） | `string t = s` 隐式 clone |
| 安全保证 | 无 GC，无隐藏分配 | RAII 自动释放，有隐式 clone |

**Zig 的哲学**：无隐藏控制流，无隐藏分配。任何 malloc 都必须显式看到 allocator。  
**LS 的哲学**：RAII + cap 三态让常见场景"自动正确"，复杂场景通过借用语法表达。

### 3.5 综合对比表

| 语言 | 默认传参语义 | 修改参数 | 移交所有权 | 生命期保证 | 隐式拷贝 |
|------|------------|---------|----------|----------|---------|
| C++ | 深拷贝（pass-by-value） | `std::string&` | `std::move()` | 无 | ✅（昂贵） |
| Go  | 16B header 拷贝（共享底层） | `[]byte` | 无概念（GC） | GC 兜底 | ❌（不可变） |
| Rust | 移交所有权 / `&str` 借用 | `&mut String` | fn 签名决定 | 编译器强制 | ❌（显式） |
| Zig | `[]const u8` 借用切片 | `[]u8` 切片 | allocator | 无（手动） | ❌（显式） |
| **LS** | **cap=0 借用**（Direction A） | `&!string` | 无（TODO） | **无（用户责任）** | **✅（赋值 clone）** |

---

## 4. 当前设计的局限

### 4.1 无法向函数传递所有权

```ls
// 目前不可能写出"函数接管 string"的语义
fn store(string s) {
    // s 是 is_borrow=true（cap=0），无法 v.push(s)
    // 只能 v.push(s.copy())，但这是在 callee 内 clone，不是 caller 转移
}
```

Rust 区分 `fn f(s: &str)` 和 `fn f(s: String)` 来解决此问题。LS 目前只有"借用"语义，没有"接管"语义的函数参数。

### 4.2 by-ref 捕获（vec/map）无生命期检查

string 参数的闭包捕获已通过 clone-on-capture 修复（env 持有 cap>0 独立副本，与 caller 生命期无关）。**但 vec/map 的默认 by-ref 捕获仍存在悬垂风险**：

```ls
fn make_summer() -> Block() -> int {
    vec(int) nums = [1, 2, 3]
    return || {                    // nums by-ref：env 存 &nums（栈帧指针）
        int s = 0; int i = 0
        while i < nums.length { s = s + nums[i]; i = i + 1 }
        return s
    }
}   // ← nums 退出作用域，env 里的指针悬空
// 调用返回的闭包 → UAF（编译器不检查）
```

vec/map 的 by-ref 捕获设计上要求"闭包不能 outlive 被捕获的容器"，但目前没有编译期强制。需要显式 `[move v]` 才能安全逃逸：

```ls
fn make_summer() -> Block() -> int {
    vec(int) nums = [1, 2, 3]
    return [move nums] || { ... }   // nums by-move，env 拥有，✅ 安全
}
```

### 4.3 `&string` 与 `string` 完全等价（Direction A 后）

Direction A 后两者 checker 行为一致，`&string` 的存在有些冗余。未来可以考虑让 `string s` 传参保留"可接管"语义，专门用 `&string` 表示借用。

### 4.4 无字符串切片类型

Rust 的 `&str`（ptr+len 无 cap）和 Go 的 `string`（ptr+len 不可变）都有独立的"字符串视图"类型。LS 目前只有 `LsString`，没有轻量的零拷贝切片。

---

## 5. 未来改进计划

### P0（核心语义修复）

#### 5.1 区分 `string`（move）与 `&string`（borrow）的参数语义

**目标**：Direction A 把 `string s` 变成了 borrow，但这损失了"callee 接管"的能力。

**方案**：引入 `own` 关键字或恢复 `string s` 为 move 语义，`&string s` 为 borrow：

```ls
// 方案 A：own 关键字
fn store(own string s) {     // callee 接管所有权，s 在 callee 内 cap>0
    v.push(s)                // ✅ 合法
}
store(my_str)                // caller: my_str 标 MOVED

// 方案 B：恢复 string = move，&string = borrow（Rust 风格）
fn store(string s) { ... }   // move
fn read(string s) { ... }    // 改为 (&string s) 才是借用
```

**当前策略**：维持 Direction A（`string s` = borrow），需要所有权时在 callee 内 `s.copy()`。

#### 5.2 `&string` 保留为显式借用（文档意义）

Direction A 后 `&string` 和 `string` 行为相同。可以选择：
- 保留两者（文档中明确 `&string` 更清晰）
- 或废弃 `&string`，只保留 `string`（简化语法）

### P1（安全性提升）

#### 5.3 by-ref 捕获逃逸检查

vec/map 的 by-ref 捕获是当前最主要的生命期安全漏洞。最小化的逃逸分析：检测"by-ref 捕获的闭包被 return 出函数"这类明显错误。

```ls
fn make_summer() -> Block() -> int {
    vec(int) nums = [1, 2, 3]
    return || { return nums[0] }   // ❌ 编译错误（目标）：
                                   // by-ref captured 'nums' escapes its scope
                                   // use [move nums] to transfer ownership
}
```

无需完整 Rust 生命期系统，仅需检测：含 by-ref capture 的 Block 被 `return` 出当前函数时报错，引导用户改用 `[move v]`。string 参数的捕获已通过 clone-on-capture 自动安全，不在此范围内。

#### 5.4 `&!string` 闭包捕获的明确禁止（已实现）

`&!string` 参数（pointer ABI）不允许被闭包捕获，防止悬垂指针。已在 Direction A 实现中加入。

### P2（表达力提升）

#### 5.5 字符串切片类型 `str`

引入 `str` 作为轻量只读字符串视图（ptr+len，不含 cap）：

```ls
str view = my_str.as_str()         // 零拷贝视图
fn process(str s) { ... }          // 接受字符串视图，零拷贝

// 与 string 的关系
string owned = view.to_string()    // 视图 → owned（clone）
str slice = owned[2..5]            // 切片（未来）
```

这对应 Go 的 `string`（不可变视图）或 Rust 的 `&str`。

#### 5.6 显式 clone 语法糖

目前 `string c = b` 触发隐式 clone。未来可引入 `clone` 关键字让拷贝显式：

```ls
string c = clone b     // 显式深拷贝（Zig 哲学：无隐藏分配）
string d = b           // 编译错误：string 赋值需要显式 clone 或 move
string e = move b      // move 语义
```

这是 Zig/Rust 的显式哲学，代价是更多语法噪声。

### P3（高级特性）

#### 5.7 小字符串优化（SSO）

对 len ≤ 15 的字符串内联存储（无 malloc），对应 C++ `std::string` 的 SSO 实现：

```
struct LsString {
    union {
        struct { char *data; int len; int cap; };   // heap (cap > 0)
        struct { char buf[12]; int len; int flag; }; // inline (flag == -2)
    };
};
```

#### 5.8 写时复制（Copy-on-Write）

赋值时共享底层 buffer（引用计数），写入时才 fork：

```
string a = b   // a 和 b 共享 data（引用计数 +1）
a += "!"       // COW：fork，a 得到独立 buffer
```

代价：引用计数原子操作开销；与现有 move 语义有冲突，需要设计权衡。

---

## 6. 设计决策总结

| 决策 | 现状 | 原因 |
|------|------|------|
| `string s` 参数 = borrow（Direction A） | ✅ 已实现 | 防止 `v.push(s)` 导致的内存泄漏；与 Go/Zig 对齐 |
| `&!string s` 参数 = pointer（可写借用） | ✅ 已实现 | 允许 callee 修改 caller 的字符串 |
| 赋值 `string c = b` = 隐式 clone | ✅ 已实现 | 简化用户代码；代价是隐藏 malloc |
| 闭包 borrow 参数 → clone-on-capture | ✅ 已实现 | 工厂模式安全，闭包与 caller 生命期解耦 |
| 无字符串切片类型 | ⚠️ TODO | 增加 `str` 类型需要系统性设计 |
| 无函数参数所有权转移 | ⚠️ TODO | 需要 `own` 关键字或类型区分 |
| 无生命期系统 | ⚠️ TODO | 完整实现复杂度极高；先做基础逃逸检查 |

---

## 7. 快速参考：string 传递场景一览

```ls
// ── 只读借用（推荐，零开销）──────────────────
fn read(string s)    { ... }   // 等价 &string，Direction A 后统一
fn read(&string s)   { ... }   // 明确只读借用

// ── 可写借用──────────────────────────────────
fn modify(&!string s) { s += "!" }   // 修改 caller 的 buffer

// ── callee 需要独立副本（当前惯用法）────────
fn store_copy(string s) {
    v.push(s.copy())    // 在 callee 内显式 clone
}

// ── 闭包捕获（安全）──────────────────────────
fn make_greeter(string name) -> Greeter {
    return || { return "hi " + name }
    // name 在 capture 时被 clone（cap=len trick）
    // 闭包可安全逃逸，与 name 的 caller 生命期无关
}

// ── 所有权转移到容器──────────────────────────
string s = "hello".upper()
v.push(s)               // s MOVED，不可再用
map.set("k", t)         // t MOVED

// ── struct 字段──────────────────────────────
struct Foo { string bar }
Foo f = Foo { bar: "hello".upper() }   // bar: owned，f.__drop 负责 free
```
