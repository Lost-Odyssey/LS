# LS 已知限制与未来改进方向

记录当前实现中已知的限制、权衡取舍，以及将来可以改进的方向。  
每条记录注明 **原因**（为什么现在这样做）和 **改进路径**（将来怎么改）。

---

## L-001 · vec.sort_by 内联插入排序（O(n²)）　✅ 已解决（2026-06-10）

> **已解决**：内建 `vec` 已于 Phase 3 拆除，动态数组改为纯 LS `std.vec` 的
> `Vec(T)`。其 `sort_by` 已从 O(n²) 插入排序换成 **O(n log n) 稳定归并排序**
> （bugs/27_vec #2，提交 `5cc6af5`）——正是下方「改进路径·长期」预言的
> 「用纯 LS 实现通用排序函数」。下文保留作历史背景。

**现状**  
`vec.sort_by(|a, b| a - b)` 使用编译器直接 emit 的插入排序循环（O(n²)），而非 `qsort`。

**原因**  
`qsort` 的比较函数签名固定为 `int (*)(const void*, const void*)`，无法携带闭包的 `env_ptr`（Block 是 `{fn_ptr, env_ptr}` 16 字节胖指针）。线程局部全局中转（trampoline）虽然可行，但不可重入（嵌套 `sort_by` 会破坏全局状态）。插入排序可以直接在循环体内调用 Block，env_ptr 自然传递。

**限制**  
- 数组较大时（n > 几百）性能明显劣于 `qsort` 的 O(n log n)
- 无捕获的 `sort_by(plain_fn)` 仍走 `qsort` + 函数指针（不受影响）

**改进路径**  
1. **短期**：emit 归并排序 IR（O(n log n)，需要额外 O(n) 临时 buffer）
2. **长期**：等用户自定义泛型落地后，用纯 LS 实现通用排序函数，替换 codegen 内联版本

---

## L-002 · 同名 interface 方法消歧　✅ 已解决（2026-06-30，含泛型 v2）

> 术语：de-rust 换皮后关键字是 **`interface`**（旧 `trait`）、**`methods Type: Interface`**
> （旧 `impl Trait for Type`）。下文统一用 interface；语义不变。

**已解决**（分支 `feat/interface-method-disambig`，设计见
[plan_interface_method_disambiguation.md](plan_interface_method_disambiguation.md)）：
一个类型现在可以拥有来自不同来源（固有 + 多个 interface）的同名方法，配套**固有优先派发**、
**同名歧义编译报错**、以及**显式限定调用** `Interface.method(recv, args)`。三者一体（见设计文档
§3 为何不可分割）。`test_iface_method_disambig`（JIT+AOT+memcheck + 4 负向）。

**用法**

```ls
// 场景 A：两个 interface 同名方法（无固有）
interface Source { def close(&!self) -> int }
interface Sink   { def close(&!self) -> int }
struct Pipe { int n }
methods Pipe: Source { def close(&!self) -> int { return 100 } }
methods Pipe: Sink   { def close(&!self) -> int { return 200 } }
// p.close()         → 编译报错（歧义，提示用限定调用）
// Source.close(&!p) → 100；Sink.close(&!p) → 200

// 场景 B：固有 + interface 同名 → 固有优先
struct Doc { Str body }
methods Doc { def render(&self) -> Str { return self.body } }            // 固有
methods Doc: Show { def render(&self) -> Str { return f"<{self.body}>" } }
// d.render()      → 固有；Show.render(&d) → Show 版
```

**实现要点（Option B：只 mangle 撞名者）**  
- 方法注册表条目加 `origin_iface`（NULL=固有，否则 interface 名）；`register_method` 重复检测
  改为「同名 + **同来源**」才报 `conflicting method`，cross-origin 同名共存。`__drop` 仍是
  origin-agnostic 单例（Destroy 的 `~` 替换 auto 版）。
- **符号命名**：固有方法永远 `T.m`；interface 方法仅在「该名在该类型上有 ≥2 提供者（contended）」
  时 mangle 成 `T.<Iface>.m`，否则保持 `T.m`。⟹ 单一提供者（现存所有 interface 方法
  `show`/`hash`/`next`/`clone`…）符号不变，派发零改动。
- **限定调用零 parser 改动**：`Iface.method(recv,...)` 的 token 形态 = 静态调用，checker 识别
  leading IDENT 是已知 interface 名后 rewrite 成 `recv.method(...)`（剥借用壳）+ origin override。

**泛型类型（v2，2026-06-30 同日完成）**  
- 泛型类型现在也支持同名共存（`iface_disambig_generic_test`，JIT+AOT+memcheck）。实现＝
  fold 时把每个折叠进固有 impl_node 的 interface 方法 stamp `origin_iface`（AST fn_decl 新字段）；
  单态化循环用它注册带 origin 的方法、按「impl_node 同名计数 ≥2」判 contended、给 contended
  interface 方法的 mangled 符号加 `.<Iface>` 段（`T(args).<Iface>.m`）。codegen 零改动（复用
  非泛型的 dispatch 拼接）。
- **前置约束**（非 L-002 引入、既有 fold 机制）：泛型类型实现 interface 需要存在一个固有
  `methods(T) X(T) { ... }` 块来承载折叠（可只含无关方法）；完全无固有块的纯 interface 泛型
  报 `requires an inherent 'methods(T) X(T)' block`。
- 运算符 interface（Add/Equal/Order…）用内部名 `$op_*`、生命期钩子 `__drop`/`__clone`/`__from_*`
  （含 Destroy 折叠的 `~`）是单例，contended 判定显式排除 `$`/`__` 前缀 → 符号永不 mangle，零回归。

---

## L-003 · 行首 `*` 被并入上一行当二元乘法（无换行语句终止）　✅ 已解决（2026-06-15）

> **已解决**（commit 见 `fix/l004-cond-paren`）：在 Pratt 循环的 `*` 中缀处理里，
> 给「`*Ident Ident` 指针声明断句」补了**换行旁路**——`*` 开新行（`current.line >
> previous.line`）时即便不在语句顶层（如赋值 RHS `self.n = 8` ⏎ `*K p`，旧逻辑
> `allow_decl_break==false`）也断句，镜像紧邻的 `&` 借用声明判据。**假阳性防护**：
> 变量名那个 IDENT 必须与 `*` **同行**（`*K p` 全在一行；跨行乘法 `a` ⏎ `* b` ⏎
> `print` 的 `print` 在下一行 → 不误判为声明）。回归 `test_stmt_boundary`（泛型
> `*K`/`*V` 声明 + 同行/跨行乘法守护，JIT+AOT）。下文保留原始分析。

**现状**  
当一条语句以 `*` 开头（典型是指针类型的局部声明 `*T p = ...`），而上一条语句以一个**值表达式**结尾时，
parser 会把行首的 `*` 当作上一行表达式的二元乘法运算符，导致两条语句被错误地黏成一条。

```ls
// ✗ 第 2 行的 *K 被并到第 1 行 → 解析成 (realloc(z,8) as *u8) * K
self.ctrl = realloc(z, 8) as *u8
*K zk = nil                 // 报：undefined variable 'K' / 'zk'

// ✗ 连续的指针声明同样中招 → nil * K
*u8 a = nil
*K   b = nil                // 第 2 行被吃成 (nil) * K
```

**原因**  
LS 用「换行不是语句分隔符、表达式自然终止」的无分号文法。`X` 后跟换行再跟 `* Y`，与
「`X * Y` 跨行书写」在 token 流上完全相同（`*` 既是解引用前缀也是乘法中缀）。Pratt parser
在解析完上一行的表达式后，看到中缀 `*` 会继续吃下去，无法区分这是新语句的指针声明还是续行乘法。
`*u8 a`（`u8` 是类型关键字）能被 `starts_var_decl` 的指针分支识别，但只在它**位于语句起点**时——
而这里它没机会成为语句起点，因为上一行的表达式先把 `*` 抢走了。

**限制 / 现有规避**  
- 仅当上一条语句以「值」结尾时触发。若上一条以 `}`（块尾）结尾则天然安全
  （std.vec `shrink_to_fit` 的单条 `*T p = nil` 正因前面是 `}` 才一直没踩雷）。
- **规避①**：给上一条（及连续指针声明的每一条）显式加 `;` 终止：
  `*u8 a = nil;` `*K b = nil;` —— `;` 是硬终止，`*` 不会再续接。
- **规避②**：尽量避免行首 `*` 声明。例：`realloc` 收 `*u8`，故复用单个 `*u8 z = nil`
  给多个不同类型的 buffer 当 malloc（`realloc(z, n*sizeof(K)) as *K`），就不必写 `*K`/`*V` 局部。
- std.map M-0 两种规避都用到了（见 docs/plan_std_map.md §4 实测纪要）。

**改进路径**  
1. **短期**：在 `starts_var_decl` / 续行判定里，当 `*` 出现在**新的一行**（token 行号大于上一 token）
   且其后跟「类型关键字 / `Ident Ident` / `Ident(...) Ident`」形态时，优先按指针声明的语句起点处理，
   不让上一行的中缀 `*` 吃掉它。需谨慎，勿破坏真正的跨行 `a *\n b` 乘法（虽然这种写法本就罕见）。
2. **长期**：引入「显式语句终止」或基于缩进/换行的 ASI（automatic semicolon insertion）规则，
   系统性消除「换行 vs 续行」歧义（不止 `*`，`-`/`(` 等前缀/中缀两义 token 都有类似隐患）。

---

## L-004 · `if`/`while` 条件以 `(` 开头时，尾部中缀运算被丢弃（`expected '{'`）　✅ 已解决

> **已解决**（随 std.complex/fft 工作根治，本条目此前未同步）：`parse_if_stmt`/
> `parse_while_stmt` 已删掉「吃一对外层括号」特例，统一 `cond = parse_expr_prec(
> PREC_NONE)`，前导 `(` 回归普通分组表达式（`while (a) {}` 与 `while (a&b)!=0 {}`
> 都正确）。正是下文「改进路径」预言的推荐改法。回归 `test_stmt_boundary`（`while
> (e)<x`/`if (e)!=x`/cast/嵌套分组/裸括号，JIT+AOT）。下文保留原始分析。

**现状**  
当 `if` / `while` 的条件**以左括号开头**、且括号闭合后还跟着二元运算时，报 `expected '{'`：

```ls
// ✗ 全部报 expected '{'（与 cast 无关）
while (x as int) != 255 { ... }
while (x + 0)    != 255 { ... }
if    (x + 0)    != 255 { ... }

// ✓ 规避①：整个条件再包一层括号
while ((x as int) != 255) { ... }
// ✓ 规避②：不要让条件以 '(' 开头（去掉最外层括号）
while x as int != 255 { ... }
// ✓ 规避③：cast/子表达式先落临时变量（std.map M-0 采用，最稳）
int c = x as int
while c != 255 { ...; c = x as int }
```

**原因**（已定位，2026-06-09 实测）  
`if`/`while` 支持**可选条件括号** `while (cond) { }`。`parse_while_stmt`/`parse_if_stmt` 的逻辑是
`has_paren = match '('; cond = parse_expr; if (has_paren) consume ')'`。当条件恰好以 `(` 开头时，
这个 `(` 被当成「可选条件括号」吃掉，于是 `cond` 只解析到括号内的 `x as int`（或 `x + 0`），
闭括号后剩下的 `!= 255` 无人认领 → parser 转去找 `{` 而报错。
即 parser 无法区分「`while (` 的 `(` 是可选条件括号」还是「条件表达式自身的分组括号」。

**限制 / 现有规避**  
- 见上三种规避；其中**临时变量**最稳，**整体再包一层括号**最省事。
- 影响 `if` 与 `while`（二者同款可选括号逻辑）；`for` 的 C 式子句不受此影响（分号定界）。

**改进路径**  
1. **推荐**：去掉 `if`/`while` 的「吃掉一对外层括号」特例——不再 `match '(' ... consume ')'`，
   而是统一 `cond = parse_expr_prec(...)`，让括号回归为普通分组表达式（`while (a) { }` 里的 `(a)`
   自然被 primary 分组解析，闭括号后若还有中缀照常延续）。需回归无括号/有括号两种既有写法。
2. 同步处理 `parse_if_stmt`，并加 `if (e) op x` / `while (e) op x` 正向用例防回归。

---

## L-005 · 同一行多语句 + M-DEF 限定泛型声明歧义（`obj.method(args) var = ...`）— 大部分已修（2026-06-26）

> **部分修复（2026-06-26）**：`starts_var_decl` 的限定泛型分支现要求 `(...)` 内是
> **非空且无值字面量**的类型实参形态——`recv.method()`（空括号）与 `recv.method(1, 2)`
> （含字面量实参）不再被误判为泛型类型声明，正确解析为「方法调用 + 后续语句」。
> 因为合法泛型类型参列表必非空、且绝不含值字面量（`int`/`Str`/用户类型/`*T`/`&T`/`,`）。
> `test_stmt_boundary` 加 L-005 用例（`qq.build() c = …` / `m.set(1,2) c = …` + `Map(Str,int)`
> 仍正确为声明）。**残留歧义**：实参全是裸标识符的罕见情形（`ss.set(skk, svv) a = …`，下例）
> 在 parse 期无类型信息、无法与 `mod.Type(UserT) v` 区分，仍须分行——但这是真正无解的
> corner（同行多语句本就违背 LS 一行一句惯例）。下文保留原始分析。

**现状**  
把两条语句写在同一行，当第一条形如 `recv.method(args)`、紧接着第二条以 `标识符 =`（或 `标识符`）
开头时，解析器会把它误判为「限定泛型类型声明」`recv.method(args) var`：

```ls
// ✗ 解析成「类型 ss.set(skk,svv) 的变量 a」→ 报 "unknown module 'ss' in qualified type 'ss.set(...)'"
ss.set(skk, svv) a = a + 1

// ✓ 拆成两行即可
ss.set(skk, svv)
a = a + 1
```

**原因**  
M-DEF（隐式空初始化，docs/plan_std_map.md §F2）放宽了 `starts_var_decl` 的限定泛型分支：
`mod.Type(typeargs) varname` 在同一行（varname 后随 `=`/`;`/换行）即视为变量声明。
但 `recv.method(args) othervar = ...` 与之同形（`recv.method` 像 `mod.Type`，`(args)` 像泛型实参，
`othervar` 像变量名，`=` 像初始化器），于是被吞成一条声明。`print(a1) print(a2)` 不中招是因为第二段
后随 `(`（不是 `=`/换行），已被 same-line 守卫排除；本例第二段后随 `=` 落入了声明判定。

**限制 / 现有规避**  
- 仅在「同一行写多条语句、且第二条紧跟 `ident =`」时触发；**每条语句各占一行**（LS 惯用风格）完全无此问题。
- 规避：换行分隔，或在第一条末尾加 `;`。

**改进路径**  
1. 收紧限定泛型声明判定：要求 `(...)` 内确实是**类型实参**形态（类型关键字 / 已知类型名），
   而非任意表达式实参；`recv.method(expr)` 的实参是值表达式，应排除。
2. 或要求泛型类型声明的 `(...)` 与 varname 之间无「方法调用」语义线索（接收者是已知模块/类型名）。

---

## L-006 · Vec(T) 函数式方法与排序对 has_drop 元素「读即克隆」　✅ 已解决（2026-06-15）

> **已解决**（分支 `feat/l006-vec-borrow`，承 `feat/field-borrow`）。三步：
> 1. **只读 `&field`/`&element` 借用原语**（`feat/field-borrow`）：`&obj.field` /
>    `&v[i]` 传只读 `&T` 形参零拷贝借用（checker §13 剥壳扩到 `AST_FIELD`/`AST_INDEX`，
>    codegen_block_call 字段/元素实参改走 `codegen_lvalue_ptr` 取址，不 codegen_expr 克隆）。
> 2. **`Block(&scalar)` ABI 修复**：闭包签名（codegen_closure_literal）与调用点
>    （codegen_block_call）的 `&T` 参数统一用 pointer ABI（此前对 `&scalar` 退化按值
>    与 body 的指针处理冲突→"Load operand must be a pointer"）。顺带修好 `RwLock(int)`。
> 3. **std/vec.lls 迁移**：`map`/`filter`/`reduce`/`find`/`each`/`any`/`all`/`count`/
>    `pos`/`sort`/`sort_by` 全部 `Block(T)`→`Block(&T)`，内部 `T e=self.data[i]; f(e)`
>    →`f(&self.data[i])`（零拷贝借元素）。filter/find 的**输出**克隆保留（Some/out 需拥有）。
>
> **caller 影响极小**：闭包 `|x| x.method()`/`|a,b| a<b` 的 `&T` 参数经 auto-deref
> 多数不变。唯一破坏＝**命名 fn 作 POD 元素方法的比较器/谓词**（`fn desc(&int,…)`
> 不支持＝`&scalar` fn 参数限制；has_drop 元素的 `fn pred(&Str)` 仍可用）→ POD 用
> 闭包（`test_fn_as_block` 据此更新）。全量 ctest 253/253。下文保留原始分析。

**现状**  
`Vec(T)` 的所有遍历式方法——`map` / `filter` / `reduce` / `find` / `each` /
`any` / `all` / `count` / `pos` / `index_of` / `has?` / `count_eq`，以及
`sort` / `sort_by` 的每次比较——都通过 `T e = self.data[i]` **读取元素**。
而 `*T` 的下标读 `p[i]` 对 owned 数据是**深克隆**（见 std/vec.lls 头注：
「p[i] (read) — DEEP-CLONES owned data」）。

因此当 `T` 是 has_drop（`string` / 嵌套 `Vec` / `Map` / has_drop struct·enum）时，
**遍历一次 = N 次 malloc+深拷贝+随后 drop**；排序比较 = O(n log n) 次克隆。
对 POD `T`（int/f64/…）则是平凡值拷贝，**LLVM O2 能完全消除**——所以这只在
has_drop 元素上是真实开销。

**原因**  
1. 这些方法把元素**按值**交给用户的 `Block`（`Block(T)->bool` 等）或比较器
   （`Block(T,T)->int`）。Block 的形参是按值的 `T`，要喂给它就得有一个 `T` 值，
   于是从缓冲深克隆一份。
2. 元素访问的统一语义就是「读即克隆」（owned 缓冲保留自己的副本，绝不把内部
   指针借出去），这条让所有权清晰、避免双释放；代价就是这里的克隆。

**限制**  
- 仅 has_drop 元素受影响；POD 无开销（O2 优化掉）。
- `reduce(U)` 的 `f(acc, e)` 也是按值传 `e`，同样每轮克隆一个元素。
- `#2` 归并排序已把比较次数从 O(n²) 降到 O(n log n)，**顺带减少了克隆次数**，
  但每次比较仍各克隆两个操作数。

**改进路径**  
1. **借用元素路径**：给这些方法一条让 `Block` 收 `&T`（只读借用）而非 `T` 的
   形态，遍历时用 `codegen_lvalue_ptr` 取元素地址直传、零克隆。**破坏性**：要把
   `Block(T)->R` 改/重载为 `Block(&T)->R`，并依赖闭包借用语义（`enum 借用 Phase B`
   已铺的 `&T` 绑定路径可复用）。
2. **比较器借用**：`sort_by` 的比较器改 `Block(&T,&T)->int`，比较零克隆——
   这是 has_drop 大数组排序的主要收益点。
3. 内部谓词类（`index_of`/`has?`/`count_eq` 用 `==`）可先做**不暴露元素给用户
   代码**的内部借用比较，属非破坏子集，可独立先行。

**关联**  
bugs/27_vec #3。`#1`（内存原语入 std.c）、`#2`（归并排序）、`#4`（mutator 越界
检查）已完成；本条 `#3` 择期做（需借用 API 设计）。

---

## L-013 · match 结果所有权不一致（yield owned 载荷：string 泄漏 / has_drop 双释）— ✅ 已修复（2026-06-10）

> **L-013 后续（2026-06-26，旧账清理）**：P5-4 拆 builtin string 时把
> `cg_register_result_temp` 整删后，has_drop（Str）的 match 结果改靠「消费侧无条件
> move」接管。这对 var-decl / assign / return / by-value 用户函数 call-arg 正确
> （这些消费点按 move 接管），但 **owned 组合子结果作裸 rvalue 被 print / 丢弃 /
> 链式接收者消费时无人接管** → 泄漏。组合子 `unwrap_or`/`ok_or`/`map`/`!`/`unwrap`/
> `expect` 等编译器降级成 `AST_MATCH` / `AST_FORCE_UNWRAP`，而四个 owned-rvalue 消费
> 侧白名单（print Str 路径、print struct 路径、f-string 内插、表达式语句丢弃、链式
> 接收者 `codegen_addr_of`）历史上只列 `AST_CALL`、漏了这两个降级节点。
> **修**：新增谓词 `cg_is_owned_combinator_rvalue`（`codegen_internal.h`）= 这两个
> 节点恒为 fresh-owned，按 `AST_CALL` 同等对待，加进各消费侧白名单。
> 另修 **identity 闭包 `map(|x| x)` 双释**：组合子把闭包 body 包进 ctor
> `Some({ x })`，`cg_store_owned` 的 source 解析只认裸 IDENT、不穿透块表达式 → binder
> 未标 moved → 既 move 进 payload 又在臂末 drop → double-free。修＝`cg_store_owned`
> 解析 source 时穿透 block-expr 到 tail。`test_opt_owned_rvalue`（JIT+AOT+memcheck
> 0/0/0；含 print/discard/chain/f-string/identity-map bound+chained）。ctest 300/300。
>
> **关联缺口已一并修复（2026-06-26）**：直接 `print(整个 Option/enum)` 原先既渲染成
> 原始字节（`0000000000000001`）又泄漏 owned 载荷。新增 `codegen_print_enum_value`
> （switch 判别式 → `Variant` / `Variant(payload, …)`，Str 载荷加引号、嵌套 struct/enum
> 递归）+ 提取共享 `cg_print_one_value`（顺带让 struct 里的 enum 字段也可读，原也乱码）
> + print 循环加 TYPE_ENUM 分支并对 owned enum rvalue drop（同 struct 分支白名单）。
> `test_enum_print`（`Some("s7")`/`Circle(5)`/`Rect(3, 4)`/`Wrap{sh=Rect(1, 2), c=Blue}`
> 等精确断言 + memcheck 0/0/0）。ctest 303/303。

> **✅ 已修复**（2026-06-10，分支 `feat/match-result-ownership`，按
> [plan_match_result_ownership.md](plan_match_result_ownership.md) 「match 作
> owned-rvalue」落地）。string 泄漏与 has_drop 双释两类全部消除，
> `test_match_result_own`（JIT+AOT+memcheck 0/0/0）守护。
>
> **实现要点**（`src/codegen.c` AST_MATCH，三 helper）：
> - `cg_match_arm_own_tail`：令 result 独占其值。**统一 clone 判据**——owned-heap
>   结果且臂体未产生新临时且非「移出的 binder」且 tail 是拥有堆的 IDENT → clone。
>   一条同时覆盖「外层局部」「borrow-match binder」（须 clone），排除「已移出 owned
>   binder」「rvalue 临时」（转移而非 clone）。修掉 has_drop 外层局部双释。
> - `cg_match_arm_encapsulate`：臂末把 tail 临时转移进 result、释放其余臂内临时；
>   subject drop（L-012）与外层临时保留不误删。
> - `cg_register_result_temp`：merge 点**只登记 string** result（消费侧经既有
>   `count>mark → mark_last_moved → flush(skip_last)` 转移，消除 binder 孤儿泄漏）。
>   **has_drop 不登记**——其消费侧对非-IDENT 初始化器无条件 move，再登记会 result
>   既被变量 move 又被 flush drop → double-free（实测 0xc0000374）；has_drop 靠
>   own_tail 的 clone + encapsulate 转移即可。故计划 §5.3 的消费侧改动实际不需要。
>
> 以下为修复前的历史分析，留档。

> **根因已精确定位，且范围比标题最初设想的更广**：不止 string 泄漏，**互补角落
> 还有 has_drop（struct/enum/Vec/Map）的 double-free**（内存损坏，更严重）。
> 与最初猜测的 `try` 无关（`try`/`ok_or` 只是常见触发器）。
> 尝试过两种 string-向修法均不达标，**已回退到稳定基线**。
> **整改设计方案见 [plan_match_result_ownership.md](plan_match_result_ownership.md)**
> （「match 作 owned-rvalue」：单结果临时 + 臂封装，六步施工）。
>
> **完整现象矩阵（已实测）**——`match` 臂体 yield 一个 owned 堆值，结果被消费：
>
> | 臂体 yield | string 载荷 | has_drop（struct/enum/Vec/Map）载荷 |
> |-----------|-----------|-----------------------------------|
> | `=> binder`（payload 绑定，可移出） | ❌ **泄漏** | ✅ 干净 |
> | `=> 外层 owned 局部`（不可静默移出） | ✅ 干净 | ❌ **double-free** |
>
> 两类**各押一个消费侧启发式、各对一半**：string 消费侧 clone AST_MATCH 结果
> （对 outer-local、漏 binder——binder 被 move-out 标 borrowed 又被 clone 而非接管，
> 成孤儿）；has_drop 消费侧 take AST_MATCH 结果（对 binder、双释 outer-local——把
> 外层仍拥有的 buffer 也接管）。**string 特有性是偶然**，根因是消费方无法判定
> match 结果所有权（取决于命中臂 yield 什么，消费方只见 `AST_MATCH`）。
> string 消费侧差异另见：var-decl / call-arg 漏，assign / return 不漏。

**现象**
`match` 一个**成功载荷是 owned 堆值（string 等）**的 Result/Option，**臂体直接
yield 该 payload 绑定**（`Ok(v) => v` / `Some(v) => v`，或 block 末尾为该绑定），
结果（一份 clone）泄漏约 16 字节（memcheck `string.clone`）。最小复现：

```ls
fn mk() -> Result(string, string) { return Ok("h" + "1") }   // owned 堆载荷
fn main() -> int {
    string s = match mk() { Ok(v) => v  Err(e) => e }   // ← 漏 16 字节
    print(s)
    return 0
}
```

**二分定位（已确认，零 `try`、零组合子）**

| 情形 | 结果 |
|------|------|
| `match x { Ok(v) => v ... }` 直接 yield binder（owned 堆） | ❌ 泄漏 |
| `match x { Ok(v) => v + "!" ... }` yield 新 owned 值（concat） | ✅ 干净 |
| `match x { Ok(v) => "c" ... }`  不 yield binder | ✅ 干净 |
| 载荷是字面量 `Ok("hi")`（static，无堆） | ✅ 干净 |
| subject 是命名变量还是 rvalue（`mk()`） | 均泄漏（与 subject 无关） |

→ **预先存在的 match codegen bug**。`try` / `ok_or` / `unwrap_or` 都只是「产出一个
owned 堆载荷的 Result/Option 再被 match 消费」的常见路径，故容易撞上；**它们本身无 bug**。

**根因**（`src/codegen.c` AST_MATCH，move-out 优化 ~第 8857 行，源自 BF-026/029）
臂体 tail 是 payload 绑定时，codegen 把该 binder 标 `is_borrowed=true` 跳过其
arm-scope drop（「移出」），**假设 match 结果的消费方会拥有并释放它**。但消费方
（var-decl / print / 实参）**不会**接管一个「移出的 binder」的所有权——它既不是
被追踪的 owned temp（不像 string concat 的 rvalue），也没经 move 进消费变量 →
binder 的 clone 无人 drop → 泄漏。

**为何 `=> v + "!"` 干净**：concat 产出的是**被 `cg_push_temp_string` 追踪的 owned
temp**，标准 temp 生命周期（flush 或 move-elision 交给消费方）正确释放它。binder
yield 缺这层追踪。

**修复尝试（均不达标，已回退）**
1. **把结果 clone 一份**（`emit_clone_value`）让其独立 → **仍泄漏**：clone 出的值
   同样不被消费方追踪。
2. **把移出的 string 注册为 owned temp**（`cg_push_temp_string`，仿 concat）→
   **修好泄漏但引入 double-free**：① 消费方接管时（`s = match`）move-elision **未
   摘除** arm 块内注册的该 temp → temp-flush 与消费方各 drop 一次；② 多臂时另一臂
   `Err(e) => e` 也命中此分支，其 temp 在 flush 处**无条件** drop，而该臂运行时未必
   执行。→ 说明 **match 臂内注册 temp 与 move-elision/flush 的整合是脆弱点**。

**正确修法方向**：让 match 结果成为**统一被追踪的 owned 值**——所有臂（binder /
新 temp / static / borrow）产出的结果在 merge 点归一为「单一 owned temp」语义，
且与 move-elision 正确去重（move 进消费变量则摘除 temp，否则 flush 落 drop 一次）。
这要重做 match-result 的 temp/所有权整合，是聚焦的 codegen 任务，非小补丁。

**影响**：中等。`match`/`try`/`unwrap_or`/`ok_or` 等凡是「owned 堆载荷被 yield 出
match」且结果被绑定/打印/传参的代码，按执行次数累积泄漏；**不损坏内存**（无 double-free）。

**覆盖盲区**：`test_opt_combinator` 的相关用例用 POD（`Some(5)`）或 yield 非 binder，
触发不了；修复时须补「owned string 经 `match … => v` / `try x.ok_or(...)`」回归。

---

## L-014 · 编译器自身类型节点泄漏（`Type *` 只分配不释放）　✅ 已缓解（2026-07-02，arena 受控）

> **已缓解**（C1 Task 3，[plan_checker_type_interning.md](plan_checker_type_interning.md)）：
> 所有 `type_*()` 工厂的 Type 节点 + struct 字段 / enum 变体 / enum 名现从一个
> **进程级 bump arena**（`src/types.c` `type_arena_alloc`）分配。空 `def main(){}`
> 的泄漏从 ~1168 个无主散块降到 **491 块**，其中 Type 相关全部落进 **8 个 16KB arena
> 块**（`LS_TYPE_STATS=1` 可打印 objs/payload/blocks）——「集中分配、单点可回收
> （`type_arena_free_all`）」。L-014 由「无主散泄漏」降级为「受控常驻」。
> **仍留**：① 残余 ~480 块是其它 checker scratch（params 数组等，非 arena 目标）；
> ② REPL 长会话的真回收（per-snapshot 世代截断）留 plan §3.4（接口已备，本期未接线）。
> **红线**：`type_clone`/`type_free` 仍走 malloc/free（唯一会被析构的 Type 类别，
> 见 `AstNode.coerce_block_type`）——工厂 Type 永不 `free()`（Task 3 曾因 register_method
> 的 `__drop` 替换路径 free 工厂 Type 节点触发堆损坏，已修）。下文保留原始分析。

**现状**
编译器进程（`lls.exe`）在类型检查阶段创建的 `Type *` 节点**从不释放**。用
`-DLS_LEAKCHECK=ON` 构建并 `lls check` 测量（见下）：连空 `def main()->int{}`
都泄漏约 **1168 块 / ~84KB**，全部来自 `src/types.c` 的类型构造器
（`type_function` / `type_pointer` / `type_reference` / `type_struct` / …）；
随程序内容按比例增长（enum/vec 样本 ~114KB）。其中约 84KB 是固定开销——
prelude `import std.core.str` + builtin 类型 setup，每次编译都触发。

**分阶段归属**（`LS_LEAKCHECK` 实测）：
- **parser**：`lls parse`（含畸形/错误输入）**0 泄漏**——AST 经 `ast_free` 全回收，
  错误恢复路径也干净。
- **checker / 类型系统**：泄漏的全部来源（`Type *` 节点）。
- **codegen**：自身堆可忽略（`emit-ir` 比 `check` 仅多 1 块/256B；LLVM 用自己的堆，
  不在追踪范围）。

**原因**
LS 的类型对象没有 arena / 引用计数 / interning：`types.c` 的每个构造器都
`malloc_safe` 一个 `Type`，调用方从不拥有/释放它（类型被 AST、符号表、其它 Type
等多处别名引用，没有单一所有者，逐个 free 容易 double-free，故干脆不 free）。
这是**批处理编译器的常见取舍**——"类型节点只分配不回收，靠进程退出时 OS 统一回收"。

**影响**
- **一次性 `compile` / `run`**：进程编译完即退出，OS 回收全部 → **实际无害**
  （绝大多数用户路径）。
- **长寿命 REPL**：每条语句都重新 check（且 prelude 每快照重新 import）→
  约 **84KB+/快照**持续累积，长会话内存单调增长。与 REPL 第三刀（增量编译，
  [plan_repl_ux.md](plan_repl_ux.md) §5）同源，互补。

**测量工具**（已入库，opt-in，默认 OFF，对正常构建零影响）
```
cmake -B build-lc -G "Visual Studio 17 2022" -A x64 \
      -DLLVM_DIR=C:\llvm\lib\cmake\llvm -DLS_LEAKCHECK=ON
cmake --build build-lc --config Release --target ls
LS_HOME=. build-lc/Release/lls.exe check file.lls      # 退出时打印 [leakcheck] 报告
```
`src/leakcheck.{c,h}` + `common.h` 宏重定向编译器自身 malloc/free 到追踪层（坐在真
CRT 堆上，LLVM 预编译不受影响）。用 `parse`/`check` 隔离前端泄漏。`_CrtDumpMemoryLeaks`
不可用——它需 Debug CRT，与 Release-CRT 静态 LLVM 是 `_ITERATOR_DEBUG_LEVEL`/
`RuntimeLibrary` 硬冲突（LNK2038，链不起来）。

**改进路径**
1. **类型 arena**：所有 `Type *` 从一个 arena 分配，编译结束（或每个 REPL 快照结束）
   整块释放——一次解决"不释放"与 REPL 累积，避开逐个 free 的 double-free 风险。
   工作量集中在 `types.c` 分配路径 + 一个 arena 生命周期挂钩。
2. 或类型 interning（去重 + 单一所有者）——更大改动，附带省内存/加速比较。
3. **一次性编译无害，故非紧急**；认真打磨 REPL 长会话体验时再做（与第三刀一起）。

---

## L-015 · 闭包 env 移入 worker 线程后从不释放（每 spawn 一个小泄漏）✅ 已解决（2026-06-22）

**曾经的现状**
`t.run(|| ...)` / `parallel_for(... |i| ...)` 把一个**带堆 env 的闭包**移入 worker
线程时,线程安全 memcheck 报「每 spawn 一份 `closure.env` 泄漏」:
`nested_closure_thread` 3–4 leak、`par_for_test` 6 leak（≈ worker 线程数）。

**真正的根因（修复时查清，与原记录的「共享所有权」诊断相反）**
其实**内存早已被释放**——`ls_thread_trampoline`（`runtime/os_win32.c` / `os_posix.c`）
在 thunk 跑完后用 **裸 `free(env)`** 释放了 env（先 `drop_fn(env)` 再 `free`）。但 env
是经 **memcheck 追踪的 `ls_mc_alloc`** 分配的,裸 `free` 不通知 tracker → tracker 见
alloc 不见 free,报**假泄漏**。所以这从来不是真泄漏,而是「worker 端的释放未走追踪
free 包装」的追踪缺口。

**之前 rc=139 的真相**
旧尝试在 thunk 里**新增** `cg_emit_block_env_drop(t_env)` 时**没有移除 trampoline 的
drop+free** → 同一 env 指针被 thunk（追踪 free）和 trampoline（裸 free）各释放一次 =
**纯双释**。原记录把它误判为「捕获值跨线程共享所有权」,实则只是两处都 free。

**修复（一处移动,非新增）**
把 env 的 drop+free 从 trampoline **移进** thunk（`src/codegen_expr.c` `__task_spawn`,
store 结果后、`ret` 前 `cg_emit_block_env_drop(ctx, t_env)`）,并**删除**两个
trampoline 里的 drop+free。这样:① 全程只 free 一次（无双释）② free 由 LS 发射的代码
走 **追踪 free 包装**（`ls_mc_free`）→ tracker 见得到 → 假泄漏消失。所有权语义不变
（env 仍是「move 进线程,worker 单一所有者」,只是释放点从 C 后端挪到 LS 前端）。

**验收**
`par_for_test` / `nested_closure_thread` 在 `run --memcheck` 下 0 leak / 0 double-free /
rc=0,多跑稳定;对照组 `guard_thread`/`atomic_thread`/`chan_mpmc` 不变。**已解除「线程
测试不跑 memcheck」旧约定**:这两个样本已纳入 `test_thread_memcheck.cmake` 作为 L-015
回归守卫。ctest 282/282。

---

## L-016 · REPL 不支持函数重定义（保留首个定义，给提示）

**现状**
在 `lls repl` 里重新 `def NAME(...)` 一个已定义的函数：
- 改签名（如 void → `-> int`）→ 曾报 codegen 校验错
  `Found return instr that returns non-void in Function of void return type`。
- 同签名 → 曾静默保留旧定义（第二次 `def` 看似成功但调用仍走旧体）。

**已缓解**（2026-06-22）：检测到重定义（`def NAME` 且 NAME ∈ 已发射函数集
`emitted`）时清晰提示并跳过，不再发射坏 IR、不再静默：
```
note: 't' is already defined — the REPL keeps the first definition.
Redefining a function isn't supported yet; restart the REPL to change it.
```
旧定义保持可用。检测在 `src/jit.c jit_repl`（`repl_def_fn_name` 提取函数名 +
`emitted_contains`），仅对函数 `def`（struct/enum/methods 的重定义尚未拦截，属同源）。

**原因**
REPL 每条 snippet 是独立 JIT 模块；首个 `def t` 的符号在 main_dylib 落地后，
后续 snippet 的同名 `t` 无法替换它——`emitted` strip 把重定义降级为「解析到旧
符号」的声明（同签名→静默旧；改签名→声明类型与旧符号不符→codegen 校验失败）。
LLJIT 当前未用 ORC ResourceTracker，无法移除/替换已落地符号。

**改进路径**
真重定义需 ORC **ResourceTracker** 方案：每 snippet 的模块挂自己的 RT，重定义时
`LLVMOrcResourceTrackerRemove` 旧 `t` 的 RT 再加新模块（新 `t` 不再被 strip）。
属 REPL 模块发射重构（与 cut-3 增量编译、L-010 模块生命周期同一片区域），独立排期。

---

## L-017 · Block 容器所有权协议按方法名字符串驱动，对用户代码暴露（lint 缓解）

**现状**（2026-07-04，审查 B-2 / 整改阶段 5）
纯 LS 容器（Vec/Map/…）对 Block 参数/返回值的所有权处理由**方法名字符串匹配**
决定，名单单一权威在 `src/block_protocol.h`：
- **store sink**（env 所有权移交容器）：`push/insert/set/__index_set/
  __from_list/extend/_insert_no_grow`（另 `run` 仅限 Task 接收者，判定在
  codegen_expr.c 调用点）；
- **alias source**（返回的 Block 别名容器 env，绑定点深拷）：
  `get/get!/__index/first/last`。

用户 struct 若恰好定义了名单内方法名且签名含 Block，会**无差别获得容器语义**：
- `def get() -> Getter` 工厂：返回的 fresh env 被当别名 → 绑定点克隆，
  原 env **泄漏**；
- `def push(Getter b)` 非存储方法：调用方 env 被提前移交 → 具名实参 env
  被 null（后续调用该变量 = 空 env）。

**缓解**（阶段 5 落地）
checker 在定义点发 **warning**（不报错）：用户模块（路径不在
`<LS_HOME>/lib/` 下）定义名单内方法名 + 签名含 Block（非泛型按 resolved
类型判；泛型模板层按 TypeNode 语法扫描 + 类型别名穿透，实例化层不重复报）。
提示改名即可完全避开。回归 `test_block_protocol_lint`（3 触发 + 2 静默 +
std 豁免）。**运行时行为不改**——警告即本条的缓解措施。

**长期方向**（仅记录，不在本轮施工）
标记接口 / 属性替代名单：容器存储方法显式声明（如 `@stores_args` 或
`interface ContainerSink`），codegen 按标记而非名字分派；名单退化为 std
容器的迁移期兼容。

---

## L-018 · 控制流逃逸出口的冲刷职责分散（floor 三套语义）

**现状**（2026-07-05，审查 OWN-3 / 整改收工转长期）
每种「提前离开」的控制流各自负责补冲刷语句级临时，且 floor 选择不同：
`return`（含 try err 路径）→ scope_exit 全清 base=0；`break`/`continue` →
`cg_flush_temps_from(loop_temp_drop_floor)`（floor≠0，否则双释外层 match
subject）；`if` 分支 → 计数快照恢复；try → 双快照 + merge 恢复（阶段 8）。
同一逃逸类历史连出 3 个 bug（29b4fa3 裸 return / 164f90f break-continue /
44265c2 try-in-arm）。**任何未来新增逃逸构造（defer、labelled-break、
guard-let 等语法糖）都需要手工接冲刷**——机制上没有「新出口自动继承」的保证。

**缓解**：LS_OWN_AUDIT 编译期账本断言（阶段 6，fn_end/A4 + match A1/A2 从
两侧夹逼漏冲刷）+ guide 坑⑦ 对照表 + match_own_stress 逃逸形态语料。

**长期方向**（仅记录）：逃逸出口统一走单一 cleanup 通道（出口注册表 /
defer 式 lowering），floor 语义收敛为「构造入口快照」一种。

## L-019 · has_drop enum 绑定语义与其它 has_drop 类型不一致（clone-on-bind）✅ 已解决（2026-07-05）

**现状**（审查 OWN-6；评估报告 docs/stage12_eval_reports.md §12a）
`type_is_movable` 不含 has_drop enum → `Enum b = a` 后源仍活（深拷），而
Str/Vec/Map/has_drop struct 同形态是 move。checker/codegen 双本账 + 4 处
inline 特判（force-unwrap 手工补丁、move-only 取出特例、闭包捕获却按
by-move、codegen clone-on-IDENT 分支）。

**已解决（2026-07-05，feat/enum-move-semantics）**：用户决策直接翻转（不做
warning 过渡）。`type_is_movable` 纳入 has_drop enum → checker 通用 move
追踪（绑定/赋值/struct 字面量字段）自动生效，codegen 既有 moved_out→
invalidate 路径接手（var_decl/assign enum 分支早已备好，borrowed 源回退
clone 不变）；force-unwrap 手工补丁收敛进 `checker_try_mark_moved`。
实际 fallout 仅 1 处实代码（普查的另 4 处「复用」是注释行的正则误报）；
5 处规格语料改 `@dup` 保留深拷独立性覆盖。回归 test_enum_move_semantics
（move/@dup/重赋值 JIT+AOT+memcheck + use-after-move 拒绝）。

**评估结论（12a，2026-07-05）**：语料普查 lib/std **0 依赖**、tests 仅 5 处
且全是钉住 clone 语义的规格语料；`@dup` 迁移原语现成。**建议 GO
（warning 过渡 → 翻转）**，属 breaking 语言语义变更，**待用户拍板立项**。

## L-020 · 标量产值 match 无穷尽性硬约束 ✅ 已解决（2026-07-05，升级为 error）

**现状**（审查 M-7；12b 已缓解，2026-07-05）
`Str s = match i { 1 => "a" }` 在 i≠1 时得到零值（空 Str）而非报错——
codegen result_alloca 置零是 drop 安全网而非语义默认值。
**缓解**：checker warning 已落地（7609b69，test_match_scalar_exhaust_warn；
bool 全覆盖/void 臂/enum 主体豁免，全库误报 0）。
**已解决（2026-07-05，feat/match-scalar-exhaust-error，14a4bed）**：用户拍板
升级为 **error**——产值语境标量 match 无 `_` 臂直接拒绝编译（豁免不变：
wildcard / bool 双字面量全覆盖 / void 臂 / enum 主体）。12b 普查全库 0 命中
＝仓内零破坏。回归 test_match_scalar_exhaust（2 拒绝 + 4 豁免 + 钉输出）。

## L-021 · enum drop 置零幂等化的检出盲区（cap 哨兵评估 no-go）

**现状**（审查 M-8；分析报告 docs/stage12_eval_reports.md §12c）
`emit_enum_drop` drop 后置零使同槽重复 drop 静默 no-op——这是 L-012 兜底与
B-MAP-OPT-001 修法的**协议依赖**（两条合法双触发路径），但也使「真·重复
drop 同槽」类 bug 在 memcheck 下不可见（0 dfree ≠ 无双 drop）。

**评估结论（12c，2026-07-05）**：cap 哨兵 **no-go**——enum 域被幂等化契约
挡死（哨兵必在合法路径误报）；Str/Vec/Map 域技术可做但增量价值被 memcheck
dfree（同指针二次 free 本就报 + backtrace）覆盖。治理维持：LS_OWN_AUDIT
账本断言 + Block env poison + 值校验语料纪律；若 enum 双 drop 再出真 bug，
优先做 CG_DEBUG 下 emit_enum_drop 入口 tag 合法性诊断（复用 A3 形状）。

**状态更新（2026-07-05，feat/enum-drop-sentinel 分支，未合 main，见
docs/plan_enum_drop_sentinel.md）**：12c 评估里"优先做的诊断"已落地——
enum 析构后置零换成死亡哨兵 tag=variant_count（1 字节），CG_DEBUG/memcheck
档现在能经 match default 的损坏 tag 诊断 + payload 0xDD 毒化**检出
use-after-drop**（注入实验：哨兵开→精确诊断 + 干净退出，哨兵关→
STATUS_HEAP_CORRUPTION 硬崩溃，见 plan §7）。合法 re-drop（L-012 / 本条
B-MAP-OPT-001 两条协议路径）在所有档位仍按契约静默——这部分 M-8 盲区
**没有变化**，是刻意保留，不是遗漏。

## L-022 · 跨模块 inherent `methods` 块（✅ 已解决 2026-07-14）

**曾经的现状**（S5 str.lls 拆分探针发现，2026-07-13）
一个 struct 的**固有方法**（inherent `methods Type { ... }`，非 trait-impl）
若定义在与 struct 声明**不同的模块**里，符号命名两侧不对称、链接必然失败，
且即使修好 mangling，只 import 属主模块（或 prelude `import std.core.str`）的
消费者也看不见移走的方法。这挡住了"把核心 struct 的方法按领域拆到多个模块"
的重构；S5（str.lls）当时降级为文件内分区。

**修复**（三阶段，均已合 main）
1. **Phase 1 — codegen mangling 对称化**（`fix(codegen)` 92d1093）：
   `codegen_impl_decl` 固有方法发符号时改用 struct 的 `llvm_name` 前缀
   （struct 属主模块），不再用 methods 块所在的 `current_emit_module`。
   解锁"消费方**显式 import** 方法所在模块"的场景。
2. **Phase 2 — checker 可见性传播**（`feat(checker)` 2714758）：新增
   `propagate_inherited_methods`（镜像 `propagate_imported_traits`），沿
   import 链递归把传递依赖的固有方法注册进消费方 impl_registry，
   带 visited 去重 + 存在性预检幂等。facade 模块一句 import 即透传子模块方法。
3. **Phase 3 — imported-type impl-key 恢复**（`fix(checker)` d726a2a）：当
   `methods Type` 块的类型是被 import 进本模块（而非本地声明）时，模块导出表
   会 miss，直接/传播两条注册路径原会退化为**裸类型名** key，与调用点用的
   `llvm_name` key 不符 → 方法丢失。修复＝export-table miss 时经全局
   `find_struct_type` / `find_enum_type` 恢复属主模块 `llvm_name` 作 key。

**兑现**（`refactor(stdlib)` 1f8d786）：str.lls 从单模块升级为无环三层 facade
——`std.core.str_core`（Str/StrSlice 定义 + 钩子 + 基础方法）＋
`std.core.str_search`（search/transform/replace/collections）＋
`std.core.str_num`（数值解析），`std.core.str` 变纯 facade re-export 三者，
`import std.core.str` 零消费方改动。ctest 359/359，str 重度样本 memcheck 0/0/0。

**已知有界后果**（非 bug）：`@derive(ReflectRaw)` 只枚举**类型属主模块内**声明的
方法（derive 在 import 处理前展开，看不到 import 来的方法），故 `Str.reflect()`
现只报 str_core 的方法，search/parse 方法不在其列。这是模块局部方法扫描的
设计性结果，已在 reflect_containers 测试注释说明。

## L-023 · `array(T,N)` 的 has_drop 元素无所有权语义（值垃圾 + 双释）✅ 已解决（2026-07-25）

**已解决**（2026-07-25，同日，施工书
[plan_array_owned_elements.md](plan_array_owned_elements.md)，ctest 371/371）：
固定数组的元素现在与 struct 字段同一套所有权协议。六处落地——
① 数组字面量元素 store 走 `cg_store_owned`（命名 owned→move+moved_flag，
borrowed→深拷，rvalue→取走）；② `a[i] = x` 先析构旧值再 `cg_store_owned`
（并给拥有型数组局部补零初始化，使这个 drop 不会碰到栈垃圾）；
③ `array b = a` 绑定走 `emit_array_clone_val`（值类型语义＝拷贝，
与已经正确的按值返回侧对齐）；④ struct 字面量的内联数组字面量字段补上
**逐元素兜底 store**（真因：`cg_expr_array_lit` 对非常量元素按约定返回 NULL
让调用方兜底，而这条路径静默放弃，字段留栈垃圾）+ 字段读出侧 clone；
⑤ `cg_push_temp_drop` 与两个冲刷循环认 `TYPE_ARRAY`（冲刷统一收敛到
`emit_drop_value`）；⑥ `type_owns_heap_for_enum` 补 `TYPE_ARRAY` 递归，
`emit_drop_value`/`emit_clone_value` 补数组分支，struct 的 `__drop`/`__clone`
字段循环认拥有型数组字段。`Vec(Str) v = [x]` 的孪生漏账（`__from_list` 路径）
同批修复，经暂存槽复用同一 `cg_store_owned` 决策，保证 `[x]` 在两种容器上
语义不分叉。回归语料 `tests/samples/array_owned_elem_test.lls`
（driver `test_array_owned_elem`）：12 正例 + 六站点负例形态，逐形态**值校验**，
JIT+AOT+memcheck 0/0/0。

**遗留（本条范围外，独立小缺陷）**：`@print` 整体打印「元素是 has_drop struct
的容器」时不走元素的 Show，只打印其首字段——`array(Str,2)` 打出指针、
`Vec(Str)` 打出 `Vec(..){data=..,len=..}`。与所有权无关，纯显示缺口，
语料已注明不钉这两行文本。

**发现**（2026-07-25，修「array 字段读取缺陷」时用 memcheck 探针掘出；
与该修复无关的**预存**问题，且**不限于 struct 字段位置**）

固定数组的元素若是 has_drop 类型（`Str` / `Vec(T)` / has_drop struct / has_drop
enum），所有权链是断的。最小复现——**纯局部**数组，完全不涉及 struct 字段：

```lls
Str x = "he"; Str y = "llo"; x.push_str(&y)
Str p = "wo"; Str q = "rld"; p.push_str(&q)
array(Str,2) L = [x, p]      // 元素装入，源 x/p 未标 moved
@print(L[0].len())           // 值对（5），但 memcheck: 2 × DOUBLE FREE
```

作为 struct 字段时另有一层：

```lls
struct SBuf { array(Str,2) d }
array(Str,2) c = s.d         // 整体读出不 clone 元素 → 与 s 共享堆
@print(c[0].len())           // 值是垃圾（-891880560），memcheck: 1 × INVALID FREE
```

**症状清单**
- 数组字面量把 owned 元素**位拷贝**进数组槽，源局部不标 moved → 双释。
- 结构体的 array 字段整体读出（`array(Str,2) c = s.d`）**不深拷元素**
  （`cg_expr_field` 的读后 clone 只覆盖 `TYPE_STRUCT` / `TYPE_ENUM` /
  `TYPE_BLOCK` 字段，`TYPE_ARRAY` 不在其列）→ 别名 + 垃圾值。
- rvalue 数组 spill 不登记 drop（`cg_push_temp_drop` 只认 struct/enum/Block，
  `TYPE_ARRAY` 静默过滤）→ 泄漏。

**为何不在本次修复范围内**
本次修的是 **place 解析**（"array(T,N) 的地址怎么取"），五个站点统一走
`cg_array_place_ptr`。那修的是可达性，与元素所有权是两件事：上面的纯局部
复现证明 has_drop 元素在**没有任何 struct 字段参与**时就已经坏了，故 place
修复既不是病因也不是解药。回归语料 `struct_array_field_test.lls` 因此**刻意
只用 POD 元素**（`array(int,N)`），让 memcheck 0/0/0 这个门禁保持有意义。

**方向**（当时的判断，事后对照）：这里预判的三处（字段读出接
`emit_array_clone_val`、字面量元素走 `cg_store_owned`、`cg_push_temp_drop` 认
`TYPE_ARRAY`）方向全部正确且都已落地，但**不止三处**——实际六处，另加一个
前置条件，见本条开头。两处判断需要修正：
- 「字段读出没 clone」只是站点 ④ 的**第二半**，真因是 struct 字面量里的内联
  数组字面量**一个 store 都没发**（`cg_expr_array_lit` 对非常量元素返回 NULL
  让调用方兜底，这条路径静默放弃）。先按「读出 clone」单独下刀会把段错
  **提前**（clone 一份栈垃圾），TDD 的红灯当场拦下了这个误诊。
- 「含拥有型数组字段的 struct 根本不是 has_drop」（`type_owns_heap_for_enum`
  的 `default: return false` 吃掉 `TYPE_ARRAY`）此前未被记录，是第六站点。

**policy A 式「直接 checker 拒绝」评估结论：no-go**（普查 2026-07-25）——
`lib/std` 对 has_drop 元素定长数组**零使用**（两个 grep 命中是假阳性：
`nn.lls:38` 是注释、`json.lls:187` 是函数名 `is_array(JsonValue`），但
`tests/samples` 的 5 处**全是今天就正确工作的形态**（静态元素或 rvalue 元素）。
拒绝会毙掉 `array(Str,3) names = ["alice","bob","cy"]` 这类干净可用的固定
字符串表——政策 A 的先例不适用，那里禁掉的形态本来就是坏的，这里要禁的
本来是好的。


---

## L-024 · 泛型 trait-impl 签名校验的两处残留边界（2026-07-30，随主修一并成文）

主体已闭合（Phase A 折叠时查 shape、Phase B 单态化时查类型），残留两点，
都是刻意的，记录以免日后被当成新 bug：

1. **类型比较仍依赖至少一次实例化。** Phase A（arity / static / self borrow
   kind / 未声明 / 重复 / 完备性）不需要实例化，定义方 `lls check` 即报。但
   参数/返回**类型**比较需要具体 `Self`，只能在单态化时做。所以一个库若定义了
   `methods Wrap(T): Conv` 且类型写错、同时**整个程序从不实例化 `Wrap`**，
   那条类型错误不会出现。彻底修需要在抽象 `T` 上做结构比较
   （`Self` ≡ `Wrap(T)`、抽象 `T` 与自身相等），属独立特性，未做。
   ⭐注意这**不是**「定义方模块自己不实例化就漏检」——实测 `std.core.vec`
   自己从不实例化具体 `Vec(int)`，但消费方实例化时照样报，且经
   `generic_template_source_file` 把诊断落回 `vec.lls` 的正确行。

2. **`FromList` / `FromPairs` 的 arity 与元素类型不被校验。** 这两个是 marker
   protocol interface：`add_builtin_lifecycle_trait` 注册时 `param_count` 恒为
   0，是**占位符不是真 arity**（`from_list` 实收 1 个元素、`from_pairs` 收 2 个），
   元素/键/值类型来自实现类型自己的泛型参数。`is_marker_protocol_trait`
   （`checker_lower.c`）因此让它们跳过 arity 与类型比较；static / self borrow
   kind / 存在性仍然照查。所以 `def from_list(&!self, int a, int b)` 这种多参
   写法不会在 impl 处被拒，只会在字面量初始化的调用点失败。彻底修需要 interface
   自带类型参数（`interface FromList(E)`），触面远超收益，未做。

**顺带修掉的三个独立 bug（都是主修过程中暴露的，不在原计划内）：**
- **非泛型类型根本无法实现 `FromList`/`FromPairs`**：marker 豁免之前被
  `requires 0, got 1` 拒死，而泛型类型能用**只因泛型路径当时完全不查**——
  一个有文档的 opt-in（实现 FromList 即可 `Bag b = [a,b,c]`）事实上不可用。
- **`FromPairs` 注册的 `self_borrow_kind` 是 `3`**（非法哨兵，合法值只有
  `0/1/2`，见 `checker.h`），与同行注释和紧邻的 `FromList` 都矛盾。它一直
  不可观测，因为唯一消费者 `Map(K,V)` 是泛型的、而泛型路径从不比较签名。
- **Phase B 诊断的文件归属错**：`checker_error` 用当前 checker 的
  `source_path`，而模板方法 AST 的行号属于**定义方**模块 → 消费方实例化时
  把定义方的行号盖在消费方的文件上。注入实验实证：把
  `lib/std/core/vec.lls:626` 改坏，报出来是 `str_core.lls:626`，而那个文件
  只有 483 行。修＝调用期间把 `source_path` 换成定义方文件
  （`generic_template_source_file`）。⭐先试过的「只在定义方模块查」方案是
  **错的**：`std.core.vec` 自己从不实例化具体 `Vec(int)`，该方案会让 stdlib
  模板完全不被检查——同一个注入实验实证了它变哑。

<!-- 后续新增限制条目请沿用 L-NNN · 标题 格式 -->
