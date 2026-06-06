# 计划：纯 LS 实现 `RawVec(T)` —— 自管裸内存的容器试点

> 状态：起草 2026-06-05。
> 目标：用**纯 LS** 实现一个**自己 `malloc`/`realloc`/`free` 裸缓冲区**的动态数组
> `RawVec(T)`，作为"能否用纯 LS 替换内建 `vec`"这一战略问题的**真实试点**。
> 与 `std.ring` 的本质区别：ring 把堆缓冲区、增长、元素 drop 全部委托给内建 `vec`
> （见 `std/ring.ls:19` 的 `vec(Option(T)) buf`），所以它**没有**触碰裸内存 / 增长 /
> 索引重载 / move-out 这几层——而这几层恰恰是替换 `vec` 必须攻克的。`RawVec` 把它们
> 全部用纯 LS + 最小 intrinsic 自己实现。
>
> 关联：[plan_std_containers.md](plan_std_containers.md) §0.1（未测泛型路径）、
> [vec_first_class_plan.md](vec_first_class_plan.md)（内建 vec 的值语义两层抽象）、
> [ownership.md](ownership.md)（借用/move/RAII 模型）。

---

## 0. 用户敲定的两个核心关切（贯穿全文）

| # | 关切 | 在本计划中的处置 |
|---|------|------------------|
| **C1 性能** | 纯 LS 容器经泛型方法调用，必须靠**内联**抹平开销。**接受 JIT 默认开 O2**。 | 见 [§6 性能](#6-性能c1jit-o2-默认化--内联验证)。JIT 已有 `default<O2>` 管线（`src/jit.c:611-632`），只是默认关闭；本计划将其默认化并验证 `push`/`[]` 被内联。 |
| **C2 内存** | drop / borrow / move 语义 + **嵌套值的嵌套 drop** 必须完全正确。 | 这是**全文最重的验收维度**。每个 PoC / Step 都以 `--memcheck` SUMMARY `0/0/0` 为通过预言。见 [§2.3 内存语义契约](#23-rawvec-的内存语义契约c2-的核心) 与 [§7 内存验收矩阵](#7-内存验收矩阵c2)。 |

> 一句话定调：**性能靠"JIT O2 + 让 RawVec 方法可内联"解决，是可控工程；内存靠"RawVec 作为
> 一个标准 has_drop struct，复用编译器既有 drop/move/borrow 机器，自己只负责 `__drop` 里
> free 裸 buffer + 逐元素递归 drop"解决，是本计划的真正难点。**

---

## 1. 现状盘点：基础设施已有的"种子"（带 file:line）

摸排结论：**裸内存层的脚手架大半已存在**，缺口比预期小。

| 能力 | 现状 | 位置 |
|------|------|------|
| 指针类型 `*T` | ✅ `TYPE_POINTER` + `type_pointer/reference/mut_reference` | `src/types.h:15,34,101-103` |
| `malloc(i64) -> *u8` | ✅ 已注册为内建 | `src/checker.c:8867-8872` |
| `free(*u8) -> void` | ✅ 已注册为内建 | `src/checker.c:8874-8879` |
| `sizeof(type) -> int` | ⚠️ **已注册但是占位**：参数是 `type_int()` 占位，**无 codegen 求值** | `src/checker.c:8881-8887` |
| `realloc` | ⚠️ **仅内部 wrapper**（codegen 自用），**未暴露给 LS 表层** | `src/codegen.c:1032-1039` |
| memcheck 包装裸内存 | ✅ **malloc/realloc/free 全部经 `ls_mc_alloc/realloc/free` + 逐点 site 标记**，自动纳入泄漏/双释放检测 | `src/codegen.c:975-1072`、`src/jit.c:95-97,202-205` |
| `*ptr` 解引用（读/写单元素） | ✅ codegen 支持 + auto-deref 指针到 struct | `src/codegen.c:10740-10744,4042-4075` |
| place 引擎（左值取址） | ✅ `codegen_lvalue_ptr`：ident / 字段（递归+解引）/ array 索引 / `*ptr` | `src/codegen.c:4025` |
| 运算符重载（trait 降级） | ✅ Add/Sub/Mul/Div/Rem/Eq/Ord 七个 | `src/checker.c:7746-7766` |
| 用户 `__drop`（has_drop 钩子） | ✅ struct 含 has_drop 字段自动 drop；用户可定义 drop 逻辑 | 既有 RAII |
| 泛型 struct/impl/方法/`&!self` | ✅ G1 完成（含跨模块实例化） | `tests/test_generics_*`、`plan_std_containers.md §0.0` |
| 统一值操作 `emit_drop_value`/`emit_clone_value` | ✅ 递归 drop/clone 权威入口（string/vec/map/struct/enum/array/Block） | `src/codegen.c`（vec-first-class 分支引入） |
| JIT O2 管线 | ✅ `default<O2>`（含 inliner），**默认关闭** | `src/jit.c:611-632` |

> **结论**：本计划不是"从零造基础设施"，而是"补 3 个真缺口 + 接通既有机器"。

---

## 2. 缺口分析 + RawVec 的内存语义契约

### 2.1 真缺口（3 个硬的 + 2 个小的）

| # | 缺口 | 为何必须 | 难度 |
|---|------|----------|------|
| **G1** | `sizeof(T)` 真实求值（编译期常量，按**类型实参**） | RawVec 要 `malloc(n * sizeof(T))` 算 buffer 字节数。当前 `sizeof` 是占位、参数是值不是类型 | 中（需 parser 让 `sizeof` 接受**类型**实参 + codegen 用 `LLVMABISizeOfType`） |
| **G2** | `*T` 的 typed 下标 `p[i]` 读写（GEP by element） | RawVec 的 `get/set/push` 要对 `*T` 缓冲区按元素寻址。当前 `AST_INDEX` 检查器**拒绝指针下标**（`src/checker.c:5380`），只支持单元素 `*p` | 中（checker 放开 `TYPE_POINTER` 分支 + codegen `lvalue_ptr` 对指针 GEP `data[i]`） |
| **G3** | 元素 **move-out / move-in** 原语（无 clone 的所有权转移） | `pop` 要把元素 move 出（不深拷）；`push` 要 move 入。否则 has_drop 元素被 clone → 性能差 + 语义错（`ring.ls:16` 已暴露此缺口） | 中高（见 §2.3，是 C2 的核心） |
| g4 | `realloc(*u8, i64) -> *u8` 暴露到 LS 表层 | grow 用。内部 wrapper 已在，只需像 malloc 一样在 checker 注册一行 | 低 |
| g5 | `memcpy`/typed 块拷贝 intrinsic（可选） | grow 时整段搬运旧元素。也可用 `for` + 逐元素 move 实现，先不做 intrinsic | 低（可延后） |

### 2.2 不需要造的（既有机器直接复用）

- **RAII / scope-drop**：RawVec 是普通 has_drop struct（含 `*T` 字段不会自动 drop，但我们给它写
  `impl Drop`），退出作用域时编译器自动调其 `__drop`。
- **move 语义**：has_drop struct 的 move（`moved_flag` 失效源）已工作（move-elision Q4）。
  `RawVec b = a` 自动 move，`a` 失效。
- **borrow**：`&RawVec` / `&!RawVec` 走既有 struct 借用 ABI（pointer，借用源不 drop）。
- **嵌套 drop**：`emit_drop_value` 已是递归权威——只要 RawVec 的 `__drop` 对每个**活元素**
  调用"元素类型的 drop"，嵌套（`RawVec(string)` / `RawVec(RawVec(int))` / `RawVec(MyStruct)`）
  自动正确。**这正是 C2 的关键接入点（见 §2.3）。**

### 2.3 RawVec 的内存语义契约（C2 的核心）

RawVec 内部布局：

```
struct RawVec(T) {
    *T  data       // 裸缓冲区（malloc 得来；空时 data == null）
    int len        // 活元素个数 [0, len) 已初始化、拥有所有权
    int cap         // 已分配容量（元素数）；字节数 = cap * sizeof(T)
}
```

**不变式（必须由实现保证，memcheck 做预言）**：

1. **唯一所有权**：`data[0..len)` 的每个元素被 RawVec **唯一拥有**。槽 `[len..cap)` 是**未初始化
   原始内存**（绝不能对其调 drop）。
2. **drop 契约（嵌套 drop 的关键）**：RawVec 的 `__drop` 必须：
   - 对 `i in 0..len` **逐个 drop 活元素**（若 `T` 是 has_drop：string free / 嵌套 RawVec
     递归 free / struct 字段递归…）——这一步把"嵌套值的嵌套 drop"压在元素 drop 上；
   - 然后 `free(data)` 释放裸 buffer **恰好一次**（`cap>0` 才 free，空 RawVec 不 free）。
   - **顺序**：先 drop 元素，后 free buffer（反了会 use-after-free）。
3. **move-out 契约（G3）**：`pop()` 把 `data[len-1]` 的所有权**转移**给调用方：
   - bit-copy 该槽的值出去（不 clone）；
   - `len -= 1`，该槽**逻辑上变未初始化**——RawVec 不再拥有它，后续 drop **跳过**它。
   - 因此绝不能发生：pop 出去的值在调用方 drop 一次 + RawVec 又 drop 一次（双释放）。
4. **move-in 契约（G3）**：`push(T x)` 把 `x` 的所有权 move 进槽 `data[len]`：
   - bit-copy `x` 进槽；`len += 1`；
   - 源 `x`（参数）**失效**，不再被调用方/被 RawVec 重复 drop。
5. **borrow 契约**：`peek(&self) -> &T`（只读借用元素）或 `get(&self) -> T`（**clone** 出独立
   owned 值）。`&self` 借用期间不得 grow（realloc 使元素地址失效——与既有 L-002 借用逃逸约束
   一致，文档化"持元素借用期间禁止改容器长度"）。
6. **grow 契约**：`realloc` 搬运 `[0..len)` 旧元素是 **bit-level 整体搬运**（地址变、值不变），
   **不触发任何 drop/clone**（元素被原样移动，所有权不变）。grow 后旧元素地址全部失效。

> **C2 落点**：编译器层面 RawVec 只需被当成"含 `*T` + 两个 int 的 has_drop struct"。所有
> 嵌套/move/borrow 的正确性，最终都收敛到**两个动作**：(a) `__drop` 里对活元素调
> `emit_drop_value`（递归权威，自动处理任意嵌套）；(b) push/pop 的 bit-copy + 失效源（move
> 语义，避免双释放）。如果这两点对了，C2 全绿。

---

## 3. PoC：最小验收（Stage 0）

> 目的：**用最小代价点亮三个真缺口 G1/G2/G3 的"可行性信号"**，并固定验收预言。
> 即使 RawVec 完整实现还没写，先让一个手搓的最小用例跑通，就能确认基础设施够用。

> ✅ **Gate M0 已通过 2026-06-05**：`test_rawvec_poc`（手写 `RawVecI`，push 20 触发
> 4 次 realloc 迁移 0→4→8→16→32，`__drop` free 一次，外加第二个 buffer）JIT+AOT+memcheck
> **0/0/0**。基础设施（g4/G1/G2）贯通，POD 裸内存自管容器内存安全成立。`new_rawveci()`
> return 的 struct move 不产生双释放。

### 3.1 PoC 范围（POD 元素，最小内存语义）

先做 `RawVec(int)`（POD，无 has_drop）—— 把 **G1+G2+g4** 走通，**回避 G3**（POD move-out
就是普通 bit-copy，无双释放风险）。这是成本最低的"基础设施贯通"证明。

```ls
// poc_rawvec_int.ls — 最小裸内存动态数组（POD 元素）
struct RawVec {
    *int data
    int  len
    int  cap
}

fn new_rawvec() -> RawVec {
    // NOTE: a nil pointer cannot be written directly in a struct literal field
    // (checker rejects `data: nil`); bind it to a local *T first, then use it.
    // Casts use `expr as type` (NOT C-style `(type)expr`). Verified 2026-06-05.
    *int p = nil
    return RawVec { data: p, len: 0, cap: 0 }
}

impl RawVec {
    fn push(&!self, int x) {
        if self.len >= self.cap {
            int ncap = 4
            if self.cap > 0 { ncap = self.cap * 2 }
            // realloc 旧 buffer 到新容量（G1 sizeof + g4 realloc）
            self.data = realloc(self.data as *u8, ncap * sizeof(int)) as *int
            self.cap = ncap
        }
        self.data[self.len] = x        // G2: *int 的 typed 下标写
        self.len = self.len + 1
    }
    fn get(&self, int i) -> int {
        return self.data[i]            // G2: typed 下标读
    }
    fn len(&self) -> int { return self.len }

    // C2: POD 无元素 drop，但裸 buffer 必须 free 恰好一次
    fn __drop(&!self) {
        if self.cap > 0 { free(self.data as *u8) }
    }
}

fn main() {
    RawVec v = new_rawvec()
    for (int i = 0; i < 10; i = i + 1) { v.push(i * i) }
    int sum = 0
    for (int i = 0; i < v.len(); i = i + 1) { sum = sum + v.get(i) }
    print(f"sum={sum}")   // 期望 285
    // v 退出作用域 → __drop → free(data) 一次
}
```

**PoC 通过预言**：

1. `ls run poc_rawvec_int.ls` → `sum=285`（功能正确）。
2. `ls run --memcheck poc_rawvec_int.ls` → SUMMARY `0 leak / 0 double-free / 0 invalid free`。
   - 验证：grow 时的 realloc 链（malloc→realloc→realloc…）被 memcheck 正确跟踪为**同一对象**，
     最终 `free` 一次清账。这是 g4 + memcheck 集成的真实考验。
3. AOT：`ls compile` 出的 .exe 行为一致。

> PoC 成功 = **G1/G2/g4 基础设施贯通 + 裸内存被 memcheck 正确清账**。这是"是否值得继续"的
> 第一个决策门。若 PoC 卡在某个 intrinsic，立即定位该 intrinsic 的实现成本再决定。

### 3.2 PoC 升级探针（has_drop 元素 → 触发 G3 + C2）

PoC POD 版通过后，加一个**最小 has_drop 探针**，把 C2 最难的"嵌套 drop + move-out"提前暴露：

```ls
// poc_rawvec_string.ls — has_drop 元素，验证 §2.3 契约 2/3/4
// （此版需 G3 move 语义就位；若 G3 未完成，get 先用 clone 版本占位）
struct RawVecS { *string data  int len  int cap }
// ... push(string)/get(&self,i)->string(clone)/__drop 逐元素 free ...
fn main() {
    RawVecS v = new_rawvec_s()
    v.push("alpha"); v.push("beta"); v.push("gamma")
    print(v.get(1))          // "beta"（clone 出，独立 owned）
    // v 退出 → __drop → 逐元素 drop string（3 次 free）+ free(data)
}
```

**通过预言**：memcheck `0/0/0`，且 string 元素 free **恰好 3 次**（不多不少）。
这一步直接验证 §2.3 契约 2（逐元素嵌套 drop）。

---

## 4. 基础设施分步实现

> 每步独立可测、保持全量 ctest 绿。顺序按依赖：g4 → G1 → G2 → G3 →（可选 Index trait）。

### Step A：暴露 `realloc`（g4，最易，先做） — ✅ 完成 2026-06-05（commit fca6000）

- **改动**：`src/checker.c` `register_builtins`（紧邻 `malloc` 注册，~L8873）增加：
  ```c
  /* realloc(*u8, i64) -> *u8 */
  Type **params = malloc_safe(2 * sizeof(Type *));
  params[0] = type_pointer(type_u8());
  params[1] = type_i64();
  Type *ft = type_function(params, 2, type_pointer(type_u8()), false);
  scope_define(c->current_scope, "realloc", ft);
  ```
- **codegen**：调用点已有内部 `realloc` wrapper（`src/codegen.c:1032`，经 `ls_mc_realloc`）。
  确认用户级 `realloc(...)` 调用解析到该 wrapper（与 malloc 同路径）。
- **测试**：`malloc → realloc → free` 三步 .ls，memcheck `0/0/0`，且 realloc 被记为同对象迁移。

### Step B：`sizeof(T)` 真实求值（G1） — ✅ 完成 2026-06-05

> 实现：新增 `AST_SIZEOF` 节点（parser `infix_call` 在泛型启发式**之前**拦截
> `sizeof(` → 解析 TYPE）；checker `resolve_type_node`（泛型形参 `T` 经实例化时
> 注册的 type-alias 自动代换）→ `sized_type`，结果 `i64`；codegen `LLVMSizeOf` 发
> 编译期常量。`test_rawvec_sizeof`（17 项，JIT+AOT）。
> 已知边界：泛型**自由函数** `fn f(T)()`（仅类型参、无值参）实例化仍报 `f(?)`——
> 既有泛型限制，与 sizeof 无关；RawVec 用泛型 **struct 方法**路径（已验证可用）。

当前 `sizeof` 形参是 `type_int()` 占位，**无法接收类型**。需让它接受**类型实参**。

- **parser**：`sizeof(TYPE)` 特殊形式——`sizeof` 后括号内解析为**类型**而非表达式（类似 `vec(T)` /
  cast `(*int)` 的类型解析路径）。产出一个带 `Type*` 的 AST 节点（新 `AST_SIZEOF` 或复用
  call + type-arg 槽）。
- **checker**：`sizeof(T)` → 返回 `type_i64()`（字节数）。解析 `T`（支持泛型形参 `T`，在单态化
  后变具体类型）。
- **codegen**：发射 `LLVMABISizeOfType(ctx->data_layout, llvm_type_of(T))` 作为 i64 常量。
  - **泛型关键**：`sizeof(T)` 在单态化后 `T` 已是具体类型 → 每个实例化拿到正确常量
    （`sizeof(int)=4`、`sizeof(string)=16`、`sizeof(RawVec)=...`）。这是 RawVec 泛型化的前提。
- **测试**：`sizeof(int)==4`、`sizeof(i64)==8`、`sizeof(bool)==1`、`sizeof` 在泛型 `fn f(T)()`
  体内随实例化变化。

### Step C：`*T` typed 下标 `p[i]`（G2） — ✅ 完成 2026-06-05

> 实现：checker `AST_INDEX` 加 `TYPE_POINTER` 分支（整型索引 → pointee 结果）；
> codegen 读路径 + `codegen_lvalue_ptr` 均加指针分支（load 指针值 → typed GEP
> `elem_llvm, ptr, [i]` → load/返回地址）；AST_ASSIGN 加 `p[i] = val` **raw-store**
> 分支：typed-GEP + `cg_store_owned`，**不 drop 旧槽**（与 vec/array 有意区分——
> 指针是 unsafe 层，槽可能是未初始化内存）。GEP 步长与 `sizeof(T)` 同源自 DataLayout，
> 故 struct padding 自动正确（`Pt{i8,i64}` stride=16 实测吻合）。
> `test_rawvec_ptr_index`（POD/padded-struct/字段访问 q[i].v/*u8 字节，JIT+AOT+memcheck）。

- **checker**（`src/checker.c:5330` `AST_INDEX`）：在 array/vec/map 分支后**新增 `TYPE_POINTER`
  分支**：`p[i]` 要求 `i` 整型，结果类型 = `obj->as.pointer_to`（pointee）。
- **codegen 读**（`AST_INDEX` 值路径 ~`src/codegen.c:13288`）：`p` 是 `*T` → load 指针 → GEP
  `data[i]`（typed，elem = llvm(T)）→ load 元素。
- **codegen 写 / 左值**（`codegen_lvalue_ptr` ~`src/codegen.c:4025` 与 `:10446`）：新增"object 为
  `*T`"分支 → load 指针 → GEP `data[i]` → 返回元素地址（供 `p[i] = x` 赋值 / `&!self` 写回）。
  - 复用既有 place 引擎，使 `self.data[i] = x` 自然走"字段读 data + 指针 GEP + store"。
- **借用交互**：`p[i]` 返回的地址在 realloc 后失效——文档化契约（§2.3 不变式 5/6），与 L-002 一致。
- **测试**：对 `malloc` 出的 `*int` 做 `p[i]=…` / 读回；越界不做运行时检查（与 array/vec 一致，
  `unsafe` 语义，文档标注）。

### Step D：元素 move-out / move-in（G3，C2 核心） — ✅ 完成 2026-06-05（Gate M1 通过）

> **实现**：新增最小原语 **`__drop_at(place)`**（checker 识别 + codegen 经
> `codegen_lvalue_ptr` 取址后 `emit_drop_value` 递归析构，POD 为 no-op）。配合既有
> `__move`，确立全套**内存安全惯用法**（`test_rawvec_m1`，JIT+AOT+memcheck 0/0/0）：
>
> | 操作 | 惯用法 | 说明 |
> |------|--------|------|
> | push 临时 | `v.push(f"...")` | 临时被消费，调用方不 drop |
> | push 命名局部 | `v.push(__move(local))` | 显式 move 标记局部，避免浅拷双释放 |
> | string 读/get | `string t = self.data[i]; return t` | var_decl 可靠深拷 string |
> | pop/move-out | `T o = self.data[i]; __drop_at(self.data[i]); len-=1; return o` | clone + drop slot |
> | set | `__drop_at(self.data[i]); self.data[i] = x` | drop 旧 + raw store |
> | `__drop` | `for i { __drop_at(self.data[i]) }; free(data)` | 递归析构,嵌套自动 |
>
> **嵌套 drop 已验证**：`RawVec(Person{string})`（递归 struct drop）+
> `RawVec(RawVec(string))`（两级递归，`__drop_at`→`RawVecS.__drop`→inner 释放）均 0/0/0。
>
> ✅ **聚合元素读取差异已消除 2026-06-05**：`p[i]` 读路径改为 `emit_clone_value` 深拷 +
> string temp 登记，**完全镜像 `vec[i]`/`array[i]` 读语义**。struct 元素整读 + 字段读穿透
> （`self.data[i].name` / `.age`）现全部 memcheck 0/0/0；同时消除了读取在 var_decl 与 return
> 之间的 clone 不一致（现统一在读路径 clone）。`test_rawvec_m1` 含这些读取验收。
>
> ✅ **嵌套容器读取差异已消除 2026-06-05（用户 `__clone` 钩子）**：`emit_struct_clone_val`
> 顶部新增——若 struct 定义了 `fn __clone(&self) -> Self`，clone 时调用它（spill 取 &self →
> 调 `<name>.__clone`）而非字段逐拷。对称于用户 `__drop`。这样含裸 `*T` buffer 的用户容器
> 也能被深拷：`RawVec(RawVec(T))` 读内层经 `RawVecS.__clone` 深拷，与 `vec(vec(T))` 一致。
> `test_rawvec_m1` 含 RawVecV 嵌套读取（row_len / row_get，JIT+AOT+memcheck 0/0/0）。
>
> **结论：RawVec 与内建 vec 的语义/内存差异已全部消除**——元素读 clone、字段读穿透、嵌套
> 容器读（用户 `__clone`）、整容器 move、逐元素递归 drop（`__drop_at`）、move-in/out 全部对齐。

这是最需谨慎的一步——直接决定 has_drop 元素是否双释放/泄漏。

**问题本质**：把 `data[i]`（一个槽）的所有权与一个 LS 值之间转移，**不经 clone、不经 drop**。

**方案（复用既有 move 机器，不发明新概念）**：

- **push（move-in）**：`fn push(&!self, T x)` 中 `self.data[self.len] = x`：
  - 对 has_drop `T`：这是"把参数 `x` move 进槽"。codegen 在 `*T[i] = x` 赋值点，当 RHS 是
    **owned 且将失效的源**（参数 by-value move 进来）时，走 **bit-store + 失效源**（标 `x`
    moved），**不 drop 旧槽**（槽是未初始化原始内存，契约 §2.3-1）。
  - 关键差异：普通 `lvalue = rhs` 赋值会 **drop 旧值**。对 RawVec 的"未初始化槽写入"必须
    **抑制旧值 drop**。→ 需要一个"原始写入"语义：要么 (a) 提供内建 `__raw_write(ptr, i, x)`
    抑制旧槽 drop，要么 (b) 约定"对 `*T` 索引赋值不 drop 旧槽"（指针下标天然是 unsafe/raw，
    不像 vec/array 元素那样 drop 旧值）。**推荐 (b)**：`*T` 索引赋值 = raw store，永不 drop 旧槽
    （指针本就是 unsafe 层）。这与 vec/array（drop 旧）有意区分，文档化。
- **pop（move-out）**：`fn pop(&!self) -> T`：
  - bit-load `data[len-1]` 出来作为返回值（owned），`len -= 1`。
  - 该槽**逻辑失效**——因为 RawVec 的 `__drop` 只遍历 `[0..len)`，`len` 减小后该槽自动被排除，
    **天然不重复 drop**。这正是用 `len` 作为"哪些槽存活"的边界的好处：move-out 只需改 `len`。
  - codegen：返回值是 bit-copy（POD memcpy / has_drop 直接搬指针字段），**不 clone**。
- **get（borrow / clone）**：`fn get(&self, int i) -> T` 返回 `emit_clone_value(data[i])`（深拷，
  调用方拿独立 owned）；或 `fn peek(&self, int i) -> &T` 只读借用（零拷，受 §2.3-5 约束）。

> **C2 的全部重量压在这一步**：只要 (1) `*T` 索引赋值 = raw store 不 drop 旧槽、(2) `__drop`
> 用 `len` 为界逐元素 `emit_drop_value` + free buffer、(3) pop 改 `len` 而非清槽——三者一致，
> 双释放/泄漏就被结构性排除。**嵌套 drop 自动正确**，因为元素 drop 走 `emit_drop_value` 递归
> 权威（§2.2）。

- **测试（C2 决定性）**：`RawVec(string)` push 5 / pop 2（move-out，2 个 string 转移给调用方
  drop）/ 剩 3 个随 `__drop` free → memcheck `0/0/0`，free 总数恰为 5。

### Step E（可选）：`Index` / `IndexMut` 运算符 trait

让 `v[i]` 语法糖作用于 RawVec（否则只能 `v.get(i)`）。

- **checker**：`is_builtin_operator_trait`（`src/checker.c:7746`）加 `"Index"` / `"IndexMut"`；
  `AST_INDEX` 检查器：object 是 struct 且实现 `Index` → 降级为 `$op_index` 方法调用（读）/
  `$op_index_set`（写）。
- **codegen**：与现有 `$op_*` 降级同路径。
- **优先级**：**可延后**。PoC/RawVec v1 用 `.get(i)`/`.set(i,x)` 方法即可；Index trait 是
  纯人体工学糖，不影响 C1/C2 结论。

---

## 5. RawVec 自身实现（泛型完整版）

> Step A–D 就位后，把 PoC 升级为泛型 `RawVec(T)`，放 `std/rawvec.ls`。

### 5.1 完整 API（对齐 plan_std_containers §2 动词约定）

```ls
struct RawVec(T) { *T data  int len  int cap }

fn new_rawvec(T)() -> RawVec(T)                 // 空容器
fn with_capacity(T)(int n) -> RawVec(T)          // 预分配

impl(T) RawVec(T) {
    fn len(&self) -> int
    fn capacity(&self) -> int
    fn is_empty(&self) -> bool

    fn push(&!self, T x)                          // move-in（§2.3-4）
    fn pop(&!self) -> Option(T)                   // move-out（§2.3-3），空→None
    fn get(&self, int i) -> T                     // clone 出（§2.3-5）
    fn set(&!self, int i, T x)                    // raw store，drop 旧元素一次
    fn peek(&self, int i) -> &T                   // 只读借用（受 grow 约束）
    fn clear(&!self)                              // 逐元素 drop，len=0，保留 cap
    fn reserve(&!self, int n)                     // grow 到 ≥ n（§2.3-6）

    fn __drop(&!self)                             // 逐元素 drop + free(data)（§2.3-2）
}
```

### 5.2 关键方法语义要点

- **`grow` / `reserve`**：`realloc(data, new_cap * sizeof(T))`（G1+g4）；旧 `[0..len)` 由 realloc
  bit-搬运（不 drop/clone，§2.3-6）；更新 `cap`。**注意**：`null` data 的 realloc 等价 malloc。
- **`set(i, x)`** 与 **`push`** 的区别：`set` 覆盖**已存在**槽 `[0..len)` → **必须先 drop 旧元素**
  再 raw-store 新值（否则旧 string 泄漏）；`push` 写**未初始化**槽 `[len]` → **不 drop**。这是
  §2.3 契约里"已初始化 vs 未初始化槽"区分的实际落点，测试要专门覆盖。
- **`pop` 返回 `Option(T)`**：空时 `None`；非空 bit-move 出 `data[len-1]` 包成 `Some`，`len-=1`。
- **`__drop`**：
  ```ls
  fn __drop(&!self) {
      for (int i = 0; i < self.len; i = i + 1) {
          // 对 data[i] 执行元素类型 drop —— 编译器对 has_drop T 自动递归（emit_drop_value）
          // 实现手段：把槽值 move 到一个局部，让其在迭代末尾自然 drop；或内建 __drop_at
      }
      if self.cap > 0 { free((*u8) self.data) }
  }
  ```
  - **嵌套 drop 的接入点**：循环体内"drop `data[i]`"必须触达 `emit_drop_value` 递归权威。
    最干净的实现：`T tmp = <move data[i]>`（move-out 到局部）→ `tmp` 在循环体末尾按 has_drop
    自动 drop（编译器既有 scope-drop）。这样 `RawVec(RawVec(string))` 的三层嵌套 drop 全自动。
    **避免**手写 free——那会绕过递归权威、漏掉嵌套。

### 5.3 与编译器内存模型的对接（C2 总账）

| 语义 | RawVec 怎么获得 | 谁保证 |
|------|-----------------|--------|
| scope-drop | 是 has_drop struct，退出作用域自动调 `__drop` | 编译器既有 RAII |
| move（`RawVec b = a`） | has_drop struct move（`moved_flag` 失效 `a`） | move-elision Q4 既有 |
| borrow（`&RawVec`/`&!RawVec`） | struct 借用 ABI（pointer，不 drop 借用源） | 既有借用机器 |
| 嵌套 drop | `__drop` 逐元素 move 到局部 → 局部 scope-drop 走 `emit_drop_value` 递归 | §5.2 实现纪律 |
| 元素 move-in/out | push raw-store 不 drop 旧槽；pop 改 `len` | Step D |
| 裸 buffer 不泄漏 | `__drop` 末尾 `free(data)`（cap>0），memcheck 跟踪 | Step A + memcheck |

---

## 6. 性能（C1）：JIT O2 默认化 + 内联验证

### 6.1 JIT 默认开 O2

- **现状**：`src/jit.c:611` `if (engine->jit_optimize)` 跑 `default<O2>`（含 inliner），**默认关**
  （`--optimize` / `LS_JIT_OPT=1` 才开）。注释指出 O2 给大模块（std.json）加 ~1s 编译。
- **改动**：把 `jit_optimize` 默认置 `true`（或对 `ls run` 默认开、REPL 保持关以求低延迟）。
  - 备选：跑**聚焦内联的轻量管线**（`function(inline,sroa,instcombine)` 而非完整 `default<O2>`），
    在编译时间与内联收益间取平衡。先用完整 O2 测准收益，再决定是否裁剪。
- **代价/权衡**：单文件 `ls run` 编译时间 +~1s（一次性）。对长跑程序/基准可忽略；对 REPL 交互式
  可保留默认关。**用户已确认接受**。

### 6.2 内联验证方法（确认 push/[] 真被抹平）

光开 O2 不够，要**证明** RawVec 方法被内联、退化为与内建 vec 同级的机器码：

1. `ls compile -o rawvec_bench.exe --emit-llvm`（或 dump O2 后 IR）→ 检查热点 `push`/`get` 调用
   点是否还有 `call @RawVec.push`，还是已 inline 成裸 GEP+store。
2. 检查 `&!self` 借用参数（pointer ABI）是否阻止内联——若是，考虑对小方法加 `alwaysinline`
   属性或在 codegen 对 trivial 方法标内联提示。
3. 基线对照：同逻辑的内建 `vec` 版本 dump IR，比较 push 热路径指令数。

### 6.3 已知性能风险点

- **borrow 参数 ABI**：`&!self` 是 pointer，可能挡内联——§6.2 步骤 2 专门验。
- **泛型单态化**：每个 `RawVec(T)` 是独立函数，内联后代码体积膨胀（可接受，与 Rust 同）。
- **bounds**：RawVec 不做越界检查（unsafe 层），与内建 vec 的 `[]`（同样不检查）对齐，性能可比。

---

## 7. 内存验收矩阵（C2）

> 每格 = 一个 .ls 测试，三重验证（JIT + AOT + `--memcheck` SUMMARY `0/0/0`）。
> 这是 C2 的回归预言，**任何一格红 → 停在该格定位 drop/move 缺陷再继续**。

| 维度 | POD `RawVec(int)` | has_drop `RawVec(string)` | 嵌套 `RawVec(RawVec(int))` | 结构 `RawVec(MyStruct{string})` |
|------|:-:|:-:|:-:|:-:|
| scope-drop（满容器退出） | ✓ | ✓ | ✓ | ✓ |
| `push` × N 触发多次 grow（realloc 链） | ✓ | ✓ | ✓ | ✓ |
| `pop`（move-out，调用方 drop） | ✓ | ✓（free 计数精确） | ✓ | ✓ |
| `set`（覆盖，旧元素 drop 一次） | ✓ | ✓ | ✓ | ✓ |
| `get`（clone 出，独立 owned） | ✓ | ✓ | ✓ | ✓ |
| `clear`（逐元素 drop，保留 cap） | ✓ | ✓ | ✓ | ✓ |
| move（`RawVec b = a`，源失效） | ✓ | ✓ | ✓ | ✓ |
| borrow（`&!self` 改 + `&self` 读，无双释放） | ✓ | ✓ | ✓ | ✓ |
| 空容器 drop（cap==0，不 free） | ✓ | ✓ | ✓ | ✓ |

**特别覆盖（最易出 bug）**：
- 嵌套列 `RawVec(RawVec(int))`：外层 `__drop` 逐元素 drop 内层 RawVec → 内层 `__drop` free 自己
  buffer → 外层 free 自己 buffer。**三层 free 顺序与计数**是嵌套 drop 的决定性测试。
- `pop` 后再 `__drop`：验证 pop 出去的元素**不被** RawVec 二次 drop（靠 `len` 边界）。
- grow 中途：realloc 把 has_drop 元素 bit-搬运，**不得**对旧地址元素调 drop（§2.3-6）。

---

## 8. 性能对比测试（实现完成后）

> 目标：量化"纯 LS RawVec（O2 内联后）vs 内建 vec"的差距，给"是否值得替换 vec"提供数据。

### 8.1 基准设计（放 `benchmarks/rawvecbench/`）

对**相同负载**跑两版（内建 `vec(T)` / 纯 LS `RawVec(T)`），各 T = int / string：

| 基准 | 负载 | 测什么 |
|------|------|--------|
| `push_pod` | push 1e7 个 int（触发多次 grow） | grow + 索引写热路径（C1 内联收益） |
| `push_str` | push 1e6 个 string | has_drop move-in + grow 搬运 |
| `random_access` | 预填后随机 `get` 1e7 次 | `[]`/get 读路径（内联关键） |
| `pop_drain` | 填满后全 pop | move-out 路径 |
| `scope_churn` | 循环内反复建/弃满容器 | __drop + free 吞吐 |

- 对照已有跨语言基准框架（CLAUDE.md 提到的 8 跨语言基准 / `benchmarks/vecbench/`）的计时方式，
  复用 `@bench` / `std.time`。
- **两组数据**：O2 关（看裸调用开销） vs O2 开（看内联后差距）——直接量化 §6 的内联收益。

### 8.2 评估口径

- **可接受阈值**（建议）：O2 开启后 RawVec 相对内建 vec 慢 **< 1.5×** → 视为"纯 LS 可替换 vec"
  在性能上成立（参考 enum 借用把 treebench 压到 1.3× 的同量级目标）。
- 若热路径仍 >2×：用 §6.2 dump IR 定位未内联点（多半是 `&!self` borrow ABI），再决定是否加
  `alwaysinline` 或调整方法 ABI。
- **内存口径**：所有基准同时跑一遍 `--memcheck`（小规模），确保高频 grow/pop 下仍 `0/0/0`。

### 8.3 产出

`benchmarks/rawvecbench/results.md`：两版 × 两 T × 5 基准 × (O2 on/off) 的吞吐表 + 一句结论
（"在 X 负载上 RawVec 达内建 vec 的 Y%，内存全绿；替换 vec 在性能/内存上[是否]成立"）。

---

## 9. 里程碑与决策门

| 门 | 内容 | 决策 |
|----|------|------|
| **M0** | PoC POD（§3.1）跑通 + memcheck `0/0/0` | G1/G2/g4 基础设施可行？否→评估单点 intrinsic 成本 |
| **M1** | PoC string（§3.2）+ Step D | **C2 核心（move-out + 嵌套 drop）成立？** 这是最硬的门 |
| **M2** | 泛型 `RawVec(T)` + §7 矩阵全绿 | 纯 LS 自管内存容器内存安全成立 |
| **M3** | §8 性能对比（O2 内联后） | **替换 vec 在性能上成立？** 给战略问题最终数据 |

> M1 + M3 是两个真正的决策点：M1 回答"纯 LS 能不能安全管裸内存"，M3 回答"快不快"。两者都绿，
> "用纯 LS 替换内建 vec"才从设想变成可执行选项；任一不达标，则结论是"纯 LS 容器留在内建原语
> 之上（ring/stack 范式）"，内建 vec 保留。

---

## 10. 一句话

> 基础设施的种子大半已在（malloc/free/sizeof 占位/realloc 内部/memcheck 包装/指针类型/place
> 引擎/泛型/递归值操作）。本计划补三个真缺口（`sizeof(T)` 求值、`*T` 下标、元素 move），把
> RawVec 实现成**一个普通 has_drop struct**——drop/move/borrow/嵌套 drop 全部复用编译器既有
> 机器，自己只负责 `__drop` 里"逐元素递归 drop + free buffer"和 push/pop 的"raw-store + 改 len"。
> 性能靠 JIT 默认 O2 + 验证内联解决（C1）；内存靠把一切收敛到 `emit_drop_value` 递归权威 +
> `len` 边界排除已 move 槽解决（C2）。以 PoC 为可行性门、以内存矩阵为预言、以性能对比为终判。
