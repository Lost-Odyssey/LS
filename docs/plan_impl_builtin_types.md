# 设计与实现：`impl` 内建类型（扩展方法）

> 状态：✅ **已实现 2026-06-07**（Phase 2.5 完成）。`impl string` 语言特性落地
> （parser/checker/codegen 三层 + `&string` by-value self ABI 复用），
> `split`/`lines`/`chars`/`join` 已迁出编译器到纯 LS `std/string.ls`（返回 `Vec(T)`），
> 内建分支 + checker 分支删除；调用方解析「内建优先→回退用户 impl」。验收测试
> `impl_string_test`（JIT+AOT+memcheck 0/0/0，24 项），迁移文件
> `string_loop`/`string_utils`/`std.md`/`std.plottl` 全绿。
>
> 实现补记（落地时新增/修复）：
> 1. **import 路径段**接受 `TOKEN_TYPE_STRING`（`import std.string` 原被 `string`
>    关键字截断）。
> 2. **rvalue 接收者 spill**：`"a,b".split(",")` 的 self 为字面量 rvalue，pointer-self
>    ABI 需地址——`codegen` 在 `codegen_addr_of` 失败时求值并 spill 到 alloca（仅
>    `&self` 只读安全）。
> 3. **跨模块前向声明**：内建类型扩展方法符号用裸名 `string.split`（不做 B-3 模块前缀），
>    且 `std.string` 可能在调用方模块之后发射——调用点按 `callee->resolved_type` 前向
>    声明（同泛型方法兜底），body 后续复用该声明。
> 4. **已知限制 VR-LIM-002**：导入模块函数内的纯 LS `Vec` 局部不自动 `__drop`——
>    `std.md`/`std.plottl` 把 split/lines 结果拷进内建 `vec` 后显式
>    `clear()`+`shrink_to_fit()` 绕行（boundary 转换是临时桥，Phase 2 桶 F 全量迁移时移除）。
>
> 起草 2026-06-07（分支 `feat/rawvec`）。
> 关联：[plan_vec_replacement.md](plan_vec_replacement.md) —— 本特性是其 **Phase 2.5** 的前置
> 语言能力（把 `string.split` 等返回内建 `vec` 的方法迁出编译器、改返回纯 LS `Vec(T)`，
> 必须先让标准库能给 `string` 定义方法）。
> 前置阅读：CLAUDE.md §5.1（命名）、§7（所有权/借用 ABI）、`docs/ownership.md`。

---

## 1. 要解决的问题

### 1.1 直接动因（vec 替换的拦路石）
内建 `string` 的若干方法**在编译器里用 C 直接构造内建 `vec` 结构体返回**：

| 方法 | 位置 | 现返回/接收 |
|------|------|------------|
| `s.split(sep)` | `codegen.c:6618`（+ runtime `__ls_str_split`） | → `vec(string)` |
| `s.lines()` | `codegen.c:6938` | → `vec(string)` |
| `s.chars()` | `codegen.c:7199` | → `vec(int)` |
| `sep.join(v)` | `codegen.c:6654`（+ `__ls_str_join`） | 接收 `vec(string)` |

Phase 3 要删除内建 `vec`，这些方法就**无法再返回 `vec`**。把它们改成纯 LS、返回
`Vec(T)`，最自然的载体是给 `string` 定义方法——但 **LS 当前不允许 `impl` 内建类型**
（`check_impl_decl` 只在 struct/enum 注册表里查名字，`string` 两者都不是 →
"impl for undefined type 'string'"）。

### 1.2 更一般的价值
"给已有类型挂方法"是通用能力（Rust 的 `impl`、Swift 的 extension、Kotlin 的扩展函数、
C# 的 extension method）。一旦支持，用户也能 `impl string { fn shout(&self)->string }`、
`impl int { fn times(&self, Block(int) f) }` 等，符合 LS "C + Ruby 混合风" 的表达力取向，
且让编译器**不必内建所有便利方法**——核心原语留在编译器，丰富 API 下沉标准库。

### 1.3 设计目标
1. `s.split(sep)` **调用写法不变**，仅返回类型由 `vec` 变 `Vec`（对调用方透明）。
2. 把 split/lines/chars/join 及其 runtime helper **整体移出编译器**。
3. 复用既有 struct 方法机器（`impl_registry`/`find_method`/方法调用 codegen）与 `&string`
   借用 ABI，**最小化新代码路径**。
4. 可扩展到其他内建类型（`int`/`char`/`bool`/`f64`/`object`），但**首期只做 `string`**。

---

## 2. 语法与语义

### 2.1 语法
```ls
impl string {
    fn split(&self, string sep) -> Vec(string) { ... }
    fn shout(&self) -> string { return self.upper() + "!" }
}
```
- 目标类型是内建类型关键字（`string` / 将来 `int` / `char` / …）。
- 方法签名与 struct `impl` 完全一致：`&self`（只读借用）/ `&!self`（可写借用）/ 旧式隐式
  `self` / `static fn`。
- `self` 的类型即该内建类型（`string`），在方法体内可调用其**内建**方法（`self.upper()`、
  `self.find(...)`、`self.len`），形成"基于核心原语搭建高层 API"的分层。

### 2.2 方法解析顺序（关键规则）
对 `s.<m>(...)`（`s : string`）：
1. **先查内建方法**（`check_string_method` 的既有 if 链）。命中即用——**内建优先，不可被
   用户 impl 覆盖**（避免静默改变 `upper`/`find` 等核心语义）。
2. 未命中 → 查 `find_method(c, "string", m)`（用户/标准库 `impl string` 注册的方法）。
   命中即按 struct 方法调用路径类型检查 + codegen。
3. 仍未命中 → 报错 "string has no method 'm'"。

> 迁移 split 时：**删除** `check_string_method` 与 codegen 里的 `split`/`lines`/`chars`/
> `join` 内建分支，使步骤 1 落空、步骤 2 命中标准库定义。新旧不并存，无优先级歧义。

### 2.3 `self` 的借用 ABI（复用既有 `&string` 规则）
CLAUDE.md §7：`&string` 是 **by-value**（传 `{data,len,cap}` 值，`cap=0` 标记借用、被调方
不 free）；`&!string` 与其他 `&T`/`&!T` 是 pointer。`impl string` 方法据此：

| self 形式 | ABI | 体内可变性 |
|-----------|-----|-----------|
| `&self` | by-value `LsString`（cap 强制 0） | 只读 |
| `&!self` | pointer to caller 的 string | 可原地改（如 `self.append(...)`） |
| 旧式 `self` | by-value（拥有，退出时 drop） | 拥有 |

> split 用 `&self`（只读借用，by-value）——与现有 `&string` 参数 ABI 完全一致，零新路径。

### 2.4 命名 / mangling（与模块 B-3 的差异）
内建类型是**全局**的（不属于任何模块），故其方法符号用**裸名** `string.split`，
**不**做模块前缀化（对比 struct 的 `<mod>__Struct.method`，见 plan_module_namespace B-3）。
理由：`std.string` 定义、却要在任意 import 了它的文件里以 `s.split()` 解析——符号必须全局唯一。
**约束**：同一内建方法名全局只能定义一次；多模块重复 `impl string { fn split }` → checker 报
"duplicate method 'split' on builtin type 'string'"。

### 2.5 可用性 / import 策略（✅ 已定：(a) 显式 import）
方法随 `impl string` 所在模块加载而注册。文件要用 `s.split()` 必须先
`import std.string`（触发加载 + 注册）。

**最终决策：(a) 显式 `import std.string`。** 与"编译器不硬编码 stdlib 依赖"一致（正是 vec
替换要消除的反模式）。代价：核心内建方法（`upper`/`find`，零 import）与下沉方法（`split`，
需 import）混用时需记得 import——但迁出方法集很小（4 个），负担可控。

> 被否决的 (b)：编译器隐式自动 `import std.string`——保零-import 体验，但编译器又多一处
> 对特定 stdlib 模块名的硬编码，与本特性的解耦初衷冲突，不采用。
>
> **推论（实现须遵守）**：未 `import std.string` 的文件调用 `s.split()` 应得到清晰的
> "string has no method 'split'（did you forget `import std.string`?）"，而非内部错误。

---

## 3. 实现计划（落点带 file:line）

### 3.1 Parser —— 允许 `impl <内建类型关键字>`
`parse_impl_decl`（`parser.c:3004`）要求 `TOKEN_IDENTIFIER`。新增谓词（与 import 路径补丁
同源思路）：
```c
static bool is_impl_target_token(TokenType t) {
    return t == TOKEN_IDENTIFIER ||
           t == TOKEN_TYPE_STRING;   /* 首期仅 string；将来加 TOKEN_TYPE_INT 等 */
}
```
`if (!check(p, TOKEN_IDENTIFIER))` → 用该谓词；`name = str_dup_n(prev.start, prev.length)`
照旧（关键字 token 的源文本即 `"string"`）。同样核对 `for` 形式的 trait impl 目标
（`parser.c:2931` 一带）若也要支持 `impl Trait for string` 后续再扩。

### 3.2 Checker —— 注册与解析
**(a) `check_impl_decl`（`checker.c:8100`）**：在 `find_struct_type`/`find_enum_type` 都未命中时，
判断 `name` 是否内建类型名（复用 `resolve_type_node` step 11 的 `"string"→type_string()`，
`checker.c:181`）。是 → `self_type = type_string()`；`impl_idx = find_or_create_impl(c, "string")`
（impl_registry 以裸名 `"string"` 为 key）；按现有循环注册每个方法（含 `self_borrow_kind`、
`is_static`、签名 Type）。**禁止** `impl(T) string`（内建类型非泛型）→ 报错。
**(b) 调用解析**：`check_string_method` 尾部（`checker.c:2838`）由"报错"改为：
```c
Type *um = find_method(c, "string", method);
if (um) { /* 校验 argc/arg 类型 vs um->as.function.params（跳过 self）；
             标记 call_node 为 string 用户方法（method_struct="string"）；
             return um->as.function.return_type; */ }
checker_error(... "string has no method '%s'" ...);
```
> 可将 string 用户方法的校验**收敛到既有 struct 方法调用校验函数**（`checker.c:5440+`
> 那段），避免重复实现 argc/borrow 检查——首选复用。

### 3.3 Codegen —— 定义与调用
**(a) `codegen_impl_decl`**：当 impl 目标是内建类型（`current` 名为 `"string"`）时，发射
函数名 `string.<method>`（**不**模块前缀化，§2.4），self 参数按 §2.3 的 `&string` by-value
ABI 生成（复用既有 `&string` 参数 lowering）。`__drop`/`__clone` 等保留协议**不适用**于内建
类型（string 自有 RAII），如出现则忽略或报错。
**(b) 调用 dispatch**：string 方法调用现走 `codegen.c:6501+` 的内建 if 链。删除被迁出方法的
分支后，让 string 方法调用**回退到 struct 方法调用 codegen 路径**（`codegen.c:11783+`）：
- 构造 `qualified_name = "string.<method>"`，`LLVMGetNamedFunction` 取 callee。
- **self 传参特例**：struct 用 `codegen_addr_of`(指针)；string `&self` 是 by-value →
  改为对 receiver 求值得到 `LsString` 值、`cap` 置 0（借用标记），作 args[0] 值传入
  （与 `&string` 实参 lowering 同一套，`codegen.c` 既有逻辑可抽用）。`&!self` 则传指针。

### 3.4 标准库 —— `std/string.ls`
```ls
import std.vec
impl string {
    fn split(&self, string sep) -> Vec(string) {
        Vec(string) out = {}
        // 用 self.len / self.find(sep) / self.substr(a,b) 等内建原语纯 LS 拆分
        ...
        return out
    }
    fn lines(&self) -> Vec(string) { ... }
    fn chars(&self) -> Vec(int)    { ... }
}
// join 反向：接收 Vec。可作 impl string { fn join(&self, &Vec(string) parts)->string }
// 或 std.string 自由函数 join(&Vec(string), string sep)->string——二选一，建议前者保持对称
```
实现复用现存内建 string 原语（`find`/`substr`/`len`/索引），这些**留在编译器**不动。

---

## 4. 测试

- **语言特性单测** `tests/samples/impl_string_test.ls`：`impl string { fn shout(&self)->string;
  fn repeat_n(&self,int n)->string }`，断言 `"hi".shout() == "HI!"` 等。`&!self` 变体 +
  借用 negative（只读 self 调用 `self.append` 应拒绝）。JIT+AOT+memcheck。
- **迁移验收**：split/lines/chars/join 迁到 `std/string.ls` 后，原 `str_split_*`、
  `string_utils_test`、`std/md.ls`/`std/plottl.ls` 等（§plan_vec_replacement 桶 F）改
  `import std.string` 后全绿 + memcheck 0/0/0；输出与内建实现逐字一致（round-trip）。
- **解析顺序**：确认内建 `upper()` 仍走内建、用户 `shout()` 走 impl。

---

## 5. 风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| string `&self` by-value 传参在 dispatch 特例里出错（误当指针） | 中 | 复用既有 `&string` 实参 lowering；`impl_string_test` 专测 `&self`/`&!self` |
| 删内建 split 后遗漏调用点 → "no method split" | 低 | 迁移前 grep 全部 `.split(`/`.lines(`/`.chars(`/`.join(`（已知 ~10 文件） |
| 内建方法被用户 impl 静默覆盖 | 低 | §2.2 规则：内建优先、不可覆盖；同名内建方法 impl 报错 |
| 多模块重复 impl string 同名方法 | 低 | §2.4：全局唯一，重复报 duplicate |
| 模块前缀化误施于内建方法符号 | 中 | §3.3：内建类型 impl 显式走裸名分支，不进 B-3 前缀逻辑 |
| join 的 `&Vec(string)` 借用参数 ABI | 低 | struct 借用（enum-borrow Phase B 风）已成熟 |

---

## 6. 在 vec 替换中的位次

```
Phase 1+1.5  重命名 RawVec→Vec + API 精简 + ?/! 后缀
Phase 2      迁移测试到 Vec（桶 A/B/C/D/E/F）
Phase 2.5    ← 本特性：impl 内建类型 + 迁移 string.split/lines/chars/join 到 std.string
Phase 3      拆除内建 vec（此时已无内建方法返回 vec，可安全删除 TYPE_VECTOR）
```

> 顺序硬约束：Phase 2.5 必须在 Phase 3 **之前**——只有 string 方法不再返回内建 vec，
> Phase 3 删除 `TYPE_VECTOR` 才不会留下悬空返回类型。

---

## 7. 后续可扩展（非本期）
- `impl int`/`impl char`/`impl f64`：数值/字符扩展方法（`5.times{}`、`'a'.is_digit?`）。
  需把 §3.1 谓词扩到对应类型关键字、§3.2 内建名识别扩到 `int`/`char`/…、§3.3 self ABI
  按值类型（POD 直接值传）。
- `impl Trait for <builtin>`：让内建类型实现用户 trait（泛型约束可命中 int/string）。
- 内建方法**可覆盖**策略（若将来需要）——当前明确禁止，保语义稳定。
