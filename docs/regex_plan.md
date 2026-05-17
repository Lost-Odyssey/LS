# LS 正则表达式标准库实现计划（单一阶段）

## 1. 设计目标

- **跨平台**：纯 C99，无平台条件编译，Windows/Linux/macOS 一致
- **零新依赖**：代码放入 `runtime/ls_regex.c`，与现有 runtime 一起编译
- **捕获组**：编号捕获 `(...)` + 命名捕获 `(?<name>...)`
- **Lookahead**：正向 `(?=...)` + 负向 `(?!...)`
- **线性时间**：Pike VM NFA 模拟，O(n×m)，无回溯，永不 ReDoS
- **Ruby-like API**：覆盖日常 90% 以上用例
- **三问规则**：无平台差异 → `runtime/ls_regex.c`，`__ls_regex_*` 前缀

---

## 2. 功能矩阵（与 Ruby 对比）

| 特性 | Ruby | LS | 说明 |
|------|------|----|----|
| `.` 任意字符 | ✅ | ✅ | 默认不匹配 `\n`；`(?s)` 后匹配 |
| `^` `$` 行首/尾 | ✅ | ✅ | |
| `\A` `\Z` 字符串首/尾 | ✅ | ✅ | |
| `*` `+` `?` 贪婪量词 | ✅ | ✅ | |
| `*?` `+?` `??` 懒惰量词 | ✅ | ✅ | |
| `{n}` `{n,m}` `{n,}` | ✅ | ✅ | |
| `{n,m}?` 懒惰计数 | ✅ | ✅ | |
| `[abc]` `[^abc]` `[a-z]` | ✅ | ✅ | |
| `\d` `\D` `\w` `\W` `\s` `\S` | ✅ | ✅ | |
| `\b` `\B` 词边界 | ✅ | ✅ | |
| `\t` `\n` `\r` 转义 | ✅ | ✅ | |
| `a\|b` 交替 | ✅ | ✅ | |
| `(...)` 编号捕获组（最多16）| ✅ | ✅ | |
| `(?:...)` 非捕获组 | ✅ | ✅ | |
| `(?<name>...)` 命名捕获 | ✅ | ✅ | 返回 map(string,string) |
| `(?i)` `(?m)` `(?s)` 内嵌标志 | ✅ | ✅ | 作用于后续整个 pattern |
| `(?=...)` 正向 lookahead | ✅ | ✅ | 零宽断言，不消耗字符 |
| `(?!...)` 负向 lookahead | ✅ | ✅ | 零宽断言，不消耗字符 |
| `(?<=...)` lookbehind | ✅ | ❌ | 实现代价高，极少使用 |
| `\1` 反向引用 | ✅ | ❌ | NFA 理论不支持线性时间 |
| Unicode `\p{Lu}` | ✅ | ❌ | 需要 Unicode 表，超出定位 |
| POSIX `[:alpha:]` | ✅ | ❌ | 少用，后续可补 |

**结论**：覆盖 Ruby regex 约 90% 的日常使用场景。
不支持的三项（lookbehind / 反向引用 / Unicode属性）在脚本场景中极少使用；
Go 的 `regexp` 包同样不支持这三项，且被证明是合理的工程取舍。

---

## 3. LS API 设计（std/regex.ls）

### 3.1 基础匹配

```ls
import std.regex as re

// 文本中是否含有匹配（非锚定搜索）
bool ok = re.matches("hello world", "\\w+")              // true

// 完整字符串是否匹配（等同于 \A...\Z）
bool full = re.full_match("2024-01-15", "\\d{4}-\\d{2}-\\d{2}")  // true
```

### 3.2 查找

```ls
// 返回第一个匹配的字符串
Option(string) m = re.find("price: 42.5 USD", "\\d+\\.\\d+")
// Some("42.5")

// 返回所有匹配
vec(string) all = re.find_all("a1 b2 c3", "\\d+")
// ["1", "2", "3"]
```

### 3.3 编号捕获组

```ls
// 第一个匹配；caps[0] = 完整匹配，caps[1..] = 各捕获组
Option(vec(string)) caps = re.capture("2024-01-15", "(\\d{4})-(\\d{2})-(\\d{2})")
// Some(["2024-01-15", "2024", "01", "15"])

// 所有匹配的捕获组列表
vec(vec(string)) all = re.capture_all("a=1 b=2 c=3", "(\\w+)=(\\d+)")
// [["a=1","a","1"], ["b=2","b","2"], ["c=3","c","3"]]
```

### 3.4 命名捕获组

```ls
// 返回 map(string, string)，key = 组名
Option(map(string, string)) m = re.capture_named(
    "2024-01-15",
    "(?<year>\\d{4})-(?<month>\\d{2})-(?<day>\\d{2})"
)
// Some({"year":"2024", "month":"01", "day":"15"})

// 所有匹配的命名捕获
vec(map(string, string)) all = re.capture_named_all(
    "alice=30 bob=25",
    "(?<name>\\w+)=(?<age>\\d+)"
)
// [{"name":"alice","age":"30"}, {"name":"bob","age":"25"}]
```

### 3.5 替换

```ls
// 替换第一个匹配
string r1 = re.replace("hello world", "\\bworld\\b", "LS")
// "hello LS"

// 替换所有匹配
string r2 = re.replace_all("2024/01/15", "/", "-")
// "2024-01-15"
```

### 3.6 分割

```ls
// 按模式分割（连续分隔符视为一个）
vec(string) parts = re.split("one,,two,,,three", ",+")
// ["one", "two", "three"]
```

### 3.7 内嵌标志 + Lookahead

```ls
// 大小写不敏感
bool ok = re.matches("Hello World", "(?i)hello")         // true

// 正向 lookahead：匹配 foo 但只在后面跟着 bar 时
vec(string) v = re.find_all("foobar foobaz", "foo(?=bar)")
// ["foo"]  — 只有第一个 foo

// 负向 lookahead：匹配 foo 但后面不是 bar
vec(string) v2 = re.find_all("foobar foobaz", "foo(?!bar)")
// ["foo"]  — 只有第二个 foo
```

---

## 4. 架构分层

```
std/regex.ls              ← 纯 LS，高层 API
    ↓ c.__ls_regex_*
std/c.ls                  ← extern fn 声明（新增 ~10 个符号）
    ↓
runtime/ls_regex.c        ← NFA 编译器 + Pike VM（~500 行纯 C99）
runtime/ls_regex.h        ← C API 声明
src/jit.c                 ← AbsoluteSymbols 注册
CMakeLists.txt            ← 加入 ls_regex.c 到 LS_SOURCES
```

---

## 5. C API（runtime/ls_regex.h）

```c
/* ---- 生命周期 ---- */

/* 编译 pattern，返回 handle（0..31），-1 表示编译错误 */
int  __ls_regex_compile(const char *pattern, int flags);

/* 释放 handle */
void __ls_regex_free(int handle);

/* 最近一次 compile 的错误描述 */
const char *__ls_regex_last_error(void);

/* ---- 执行 ---- */

/* 在 text[start..text_len) 上执行一次匹配
   返回捕获组个数（含 group0=完整匹配），0 = 无匹配 */
int  __ls_regex_exec(int handle, const char *text, int text_len, int start);

/* 查询上次 exec 的结果（组索引 0=完整匹配，1..n=捕获组） */
int  __ls_regex_cap_start(int group);  /* 字节偏移，-1=未参与 */
int  __ls_regex_cap_len(int group);    /* 字节长度 */

/* ---- 命名捕获查询 ---- */

/* 上次 exec 中共有多少个命名组 */
int         __ls_regex_named_count(int handle);

/* 第 i 个命名组的名字（用于 capture_named 构建 map） */
const char *__ls_regex_named_name(int handle, int i);

/* 第 i 个命名组对应的编号（1-based） */
int         __ls_regex_named_index(int handle, int i);
```

flags 位定义：
```c
#define LS_RE_IGNORECASE  0x01   /* (?i) 大小写不敏感 */
#define LS_RE_MULTILINE   0x02   /* (?m) ^ $ 匹配行首尾 */
#define LS_RE_DOTALL      0x04   /* (?s) . 匹配 \n */
```

handle 池：静态数组 `g_re_pool[32]`，int 是索引，避免在 LS 侧传 pointer。

---

## 6. NFA 字节码指令集

```c
typedef enum {
    /* 字符匹配 */
    OP_CHAR,        /* 匹配单个字面字符（带 case-fold 标志） */
    OP_ANY,         /* . ：匹配任意字符（DOTALL 控制是否含 \n） */
    OP_CLASS,       /* [abc] 字符类，后跟 bitmap 或 range 列表 */
    OP_NCLASS,      /* [^abc] 否定字符类 */
    /* 速记字符类 */
    OP_DIGIT,       /* \d  [0-9] */
    OP_NDIGIT,      /* \D */
    OP_WORD,        /* \w  [0-9A-Za-z_] */
    OP_NWORD,       /* \W */
    OP_SPACE,       /* \s  [ \t\n\r\f\v] */
    OP_NSPACE,      /* \S */
    /* 零宽断言 */
    OP_ANCHOR_BOL,  /* ^  行首 */
    OP_ANCHOR_EOL,  /* $  行尾 */
    OP_ANCHOR_BOS,  /* \A 字符串首 */
    OP_ANCHOR_EOS,  /* \Z 字符串尾 */
    OP_WORDBND,     /* \b  词边界 */
    OP_NWORDBND,    /* \B */
    /* 捕获 */
    OP_SAVE,        /* 记录捕获组开始/结束位置（Pike VM 核心）
                       operand: group_id<<1 | (0=open,1=close) */
    /* 控制流 */
    OP_SPLIT,       /* NFA 分叉；operands: offset_a, offset_b
                       先尝试 a（贪婪），懒惰时先尝试 b */
    OP_JUMP,        /* 无条件跳转；operand: offset */
    /* Lookahead（零宽，不消耗字符） */
    OP_LOOKAHEAD,   /* operands: sub_end_offset, is_negative
                       spin up sub-VM at current pos；
                       pos lookahead: 子匹配成功则继续；
                       neg lookahead: 子匹配失败则继续 */
    OP_MATCH,       /* 匹配成功，终止 */
} ReOpCode;
```

共 **21 条指令**，覆盖所有目标特性。

---

## 7. Pike VM 核心设计

### 7.1 线程结构

```c
typedef struct {
    int  pc;                         /* 当前指令指针 */
    int  saved[MAX_GROUPS * 2];      /* 各捕获组的 open/close 位置 */
} ReThread;
```

每个线程独立携带一份 `saved[]` 快照。这是 Pike VM 区别于普通 NFA 的关键：
捕获组状态随线程传播，而非共享。

### 7.2 主循环伪码

```
current_threads = [(pc=0, saved=[-1,...])]
for pos = start to text_len (含 text_len 用于 $ / \Z 断言):
    next_threads = []
    visited = {}          // 同一 pc 只保留最早到达的线程（去重）

    for each thread t in current_threads:
        loop:  // epsilon 闭包：处理不消耗字符的指令
            switch instr[t.pc]:
                OP_CHAR c   → if text[pos]==c: next_threads.add(t@pc+1)
                              break loop
                OP_ANY      → if not('\n') or DOTALL: next_threads.add(t@pc+1)
                              break loop
                OP_CLASS    → if char_in_class: next_threads.add(t@pc+1)
                              break loop
                OP_SAVE n   → t.saved[n]=pos; t.pc++; continue loop
                OP_SPLIT a,b→ add_thread(t@a); t.pc=b; continue loop
                OP_JUMP off → t.pc+=off; continue loop
                OP_ANCHOR   → check condition(pos); if ok t.pc++ else die
                              continue loop
                OP_LOOKAHEAD→ run sub_vm(t.pc+1..sub_end, text, pos)
                              if (result XOR is_negative): t.pc=sub_end+1
                              else die; continue loop
                OP_MATCH    → record t.saved; return SUCCESS

    current_threads = next_threads
return FAILURE
```

### 7.3 命名捕获实现

编译期：`(?<year>...)` 等价于 `(...)` + 额外记录一个 name→group_id 映射：

```c
typedef struct { char name[64]; int group_id; } NamedGroup;
```

存储在 handle 的 `named[]` 数组中，最多 16 个命名组。
VM 执行时完全等同于普通编号组，`__ls_regex_named_name/index` 只是查表。

LS 侧 `capture_named` 实现：
```ls
fn capture_named(string text, string pattern) -> Option(map(string, string)) {
    int h = c.__ls_regex_compile(pattern, 0)
    if h < 0 { return None }
    int n = c.__ls_regex_exec(h, text, text.length, 0)
    if n == 0 { c.__ls_regex_free(h); return None }
    map(string, string) m = {}
    int nc = c.__ls_regex_named_count(h)
    int i = 0
    while i < nc {
        string name = from_cstr(c.__ls_regex_named_name(h, i))
        int idx  = c.__ls_regex_named_index(h, i)
        int s    = c.__ls_regex_cap_start(idx)
        int l    = c.__ls_regex_cap_len(idx)
        if s >= 0 { m.set(name, text.substr(s, l)) }
        i = i + 1
    }
    c.__ls_regex_free(h)
    return Some(m)
}
```

### 7.4 Lookahead 实现

`OP_LOOKAHEAD` 触发一次嵌套 VM 调用（sub-simulation）：

```c
static int vm_run(ReHandle *re, const char *text, int len,
                  int start, int pc_start, int pc_end,
                  int *saved);  // 前向声明

// 在 VM 主循环遇到 OP_LOOKAHEAD 时：
case OP_LOOKAHEAD: {
    int sub_end    = instr[t.pc].operand_a;  // lookahead 内容结束处
    int is_neg     = instr[t.pc].operand_b;
    int tmp[MAX_GROUPS*2];
    // 从当前位置 pos 运行子 VM，只跑 [t.pc+1 .. sub_end] 段字节码
    int ok = vm_run(re, text, len, pos, t.pc+1, sub_end, tmp);
    if (ok != is_neg) {   // pos lookahead: ok==1; neg lookahead: ok==0
        t.pc = sub_end + 1;
        continue loop;    // 不消耗字符，继续 epsilon 闭包
    }
    // 条件不满足，线程死亡
    break;
}
```

关键：sub-VM 调用后 **pos 不前进**（零宽），`saved[]` 也不从子调用中继承（lookahead 内部的捕获组按 Ruby 语义不暴露给外部）。

---

## 8. Pattern 编译器

递归下降解析器，优先级从低到高：

```
expr      = concat ('|' concat)*
concat    = piece piece*
piece     = atom quantifier?
quantifier = ('*'|'+'|'?'|'{n,m}') '?'?    ← 末尾 '?' 变懒惰
atom      = '(' group_prefix expr ')'
           | '[' class_body ']'
           | '(?=' expr ')'                  ← 正向 lookahead
           | '(?!' expr ')'                  ← 负向 lookahead
           | '(?i)' | '(?m)' | '(?s)'        ← 内嵌标志
           | '.'
           | '\' escape_char
           | literal_char

group_prefix = '?:' → 非捕获
             | '?<name>' → 命名捕获（记录 name→id 映射）
             | ε → 编号捕获（id = ++group_counter）
```

`{n,m}` 展开方式：`a{3,5}` 编译为 `a a a a? a?`（NFA 直接展开，避免特殊指令），
对大 m 值加上限（m ≤ 255）防止字节码膨胀。

---

## 9. 文件清单

| 文件 | 改动类型 | 预估行数 |
|------|----------|---------|
| `runtime/ls_regex.c` | **新建** | ~500 行 |
| `runtime/ls_regex.h` | **新建** | ~40 行 |
| `std/regex.ls` | **新建** | ~130 行 |
| `std/c.ls` | **修改** | +12 行 |
| `src/jit.c` | **修改** | +12 行 |
| `CMakeLists.txt` | **修改** | +1 行 |
| `tests/samples/regex_test.ls` | **新建** | ~120 行 |
| `tests/test_regex.cmake` | **新建** | ~50 行 |

---

## 10. 测试覆盖计划（regex_test.ls）

| 测试组 | 用例 |
|--------|------|
| 基础匹配 | `matches` 正/负、`full_match` 锚定 |
| 量词 | `*` `+` `?` `{n,m}` 贪婪与懒惰 |
| 字符类 | `[a-z]` `[^0-9]` `\d` `\w` `\s` `\b` |
| 交替 | `cat\|dog` `(red\|blue) sky` |
| 编号捕获 | `capture` 单次、`capture_all` 多次 |
| 命名捕获 | `capture_named` 日期/URL 解析 |
| Lookahead | `(?=...)` `(?!...)` 正/负 |
| 内嵌标志 | `(?i)` 大小写、`(?m)` 多行、`(?s)` dotall |
| 替换 | `replace` `replace_all` |
| 分割 | `split` 多分隔符、边界情况 |
| 错误处理 | 非法 pattern 返回 None 不崩溃 |

---

## 11. 不支持的特性（明确边界）

| 特性 | 原因 |
|------|------|
| 反向引用 `\1` | Pike VM / NFA 理论上无法 O(n) 支持 |
| Lookbehind `(?<=...)` | 需要反向 NFA，实现复杂度不成比例 |
| Unicode 属性 `\p{Lu}` | 需要 Unicode 表（数十 KB） |
| POSIX 字符类 `[:alpha:]` | 少用，可后续补 |
| 原子组 `(?>...)` / 占有量词 `*+` | 高级特性 |

对于需要上述特性的场景：`proc.exec("grep -P ...")` 是实用替代方案。

---

## 12. 实施步骤（约 2 天）

**Day 1 上午** — `runtime/ls_regex.c` 基础框架
- 字节码结构体、handle 池、字符类 bitmap
- Pattern 编译器（不含 lookahead / 命名捕获）
- Pike VM 主循环（不含 OP_LOOKAHEAD）

**Day 1 下午** — 扩展特性
- 命名捕获 `(?<name>...)`：编译器 + name_table
- Lookahead `(?=...)` `(?!...)`：OP_LOOKAHEAD + sub-VM
- 内嵌标志 `(?i)` `(?m)` `(?s)`

**Day 2 上午** — LS 集成
- `std/c.ls` extern fn、`src/jit.c` 注册
- `std/regex.ls`：全部 8 个公开函数

**Day 2 下午** — 测试与验证
- `tests/samples/regex_test.ls`（JIT + AOT）
- ctest 注册，目标 33/33
