# LS 所有权与 Move 语义 — 详细设计

> 摘要版本见 CLAUDE.md §7。本文件包含完整借用规则表、运行时保护细节和实现计划。

---

## 1. LsString 内存三态

| cap 值 | 状态 | 含义 | 释放规则 |
|--------|------|------|----------|
| `== 0` | **Static** | data 指向 `.rodata` 全局常量 | 永不释放，永不 move |
| `> 0` | **Owned** | malloc 分配，当前变量持有所有权 | 退出作用域时 free |
| `== -1` | **Moved** | 所有权已转移 | 跳过 free |

---

## 2. 核心规则：MAYBE_MOVED = MOVED = 死亡状态

变量一旦存在**任何执行路径**导致 move，即视为**死亡**，不可再使用或赋值。

```
状态转移（单调不可逆）：LIVE → MAYBE_MOVED → MOVED
```

```ls
string s = "hello".upper()
if cond { v.push(s) }   // s 处于 MAYBE_MOVED
print(s)                 // ❌ 编译错误（即使 else 分支未 move）
```

**理由**：状态单调递增，无需 CFG 不动点迭代；2-pass 循环分析足以处理所有控制流。

---

## 3. Move 触发条件（仅对 cap > 0 的 Owned string 生效）

| 操作 | 说明 |
|------|------|
| `vec.push(s)` | 所有权转入 vec，s 标记为 Moved |
| `map.set(k/v = s)` | 深拷贝到 map 节点，s 标记为 Moved |
| `string t = s` | 直接赋值转移所有权 |
| `fn_call(s)` | 函数参数传递（Phase A/B 实现） |

**Static string（cap==0）永不触发 move。**

---

## 4. 借用规则完整表

借用是"不移交所有权"的函数传参方式，形参结束时不释放源内存。借用变量**禁止**出现在 §3 的 move 触发位点。

| 借用形式 | ABI | 允许 | 禁止 |
|---|---|---|---|
| `&string`（只读） | by-value，cap 字段置 0 | 读 / `.upper` 等返回新 owned 的方法 | `=` / `+=` / `.append` / `vec.push` / `__move` |
| `&!string`（可写） | pointer（LsString\*） | 读 / `=` 重赋值 / `+=` / `.append` | `vec.push` / `__move` / `string t = s` copy-out |
| `&vec(T)`（只读） | pointer（LsVec\*） | 读（`v[i]` / `.length` / `.get` / `.is_empty` / `.first` / `.last` / `.contains` / `.index_of` / `.slice` / `.copy`） | `push/pop/clear/set/reserve/remove/truncate/swap/reverse/extend/insert/resize/sort/sort_by/shrink_to_fit` / `v[i]=x` / `v=new_vec` / copy-out |
| `&!vec(T)`（可写） | pointer（LsVec\*） | 读 + 所有 mutating 方法 + `v[i]=x` + `v=new_vec` | `__move` / `vec(T) t = v` copy-out |
| `&map(K,V)`（只读） | pointer（LsMap\*） | 读（`.length/.get/.contains_key/.keys/.values`） | `set/remove/clear` / copy-out |
| `&!map(K,V)`（可写） | pointer（LsMap\*） | 读 + 所有 mutating 方法（`set/remove/clear`） | `__move` / `map(K,V) t = m` copy-out |
| `&struct`（只读） | pointer（Struct\*） | 读字段（drop 字段返回 owned clone）；`&self` 方法调用 | `s.field = x` / `&!self` 方法调用 / copy-out |
| `&!struct`（可写） | pointer（Struct\*） | 读字段 + 字段写 `s.field=x`；`&self`/`&!self` 方法调用 | `__move` / `Struct t = s` copy-out |

### 调用点规则

- 只读借用支持 **auto-borrow**（`f(my_owned)` 自动传递为 `&T`）
- 可写借用**必须显式** `f(&!my_owned)`；可向 `&T` 形参降级
- 同一 call 内同名变量不能同时出现 `&!x` 与其它借用（aliasing 禁止）
- 当前支持类型：`string / vec(T) / map(K,V) / struct`（含 has_drop 字段亦可）
- **不支持**：借用作为返回类型 / 变量声明 / struct 字段（需引入生命期系统）

### ABI 策略

- `&string` 是 16 字节 POD 值传递特化（cap=0 标记阻止 callee free）
- 其他 `&T` / `&!T` 默认 pointer，callee 的 `sym->value` 直接作为 `LsT*` 使用，和 owned 形参路径共享同一份 codegen

---

## 5. Move 语义适用类型

| 类型 | 需要跟踪 | 说明 |
|------|----------|------|
| `string` | ✅ | cap>0 时有堆内存所有权 |
| `struct`（含 string 字段或自定义 `__drop`） | ✅ | has_drop 标记 |
| `vec(T)`, `map(K,V)` | ✅ | 含堆内存 |
| `Block(...)` | ✅ | Phase F.2：env_ptr 非 NULL 时持有堆 env；赋值后 source.env_ptr 置 NULL |
| `int/f64/bool` | ❌ | 值语义，拷贝即可 |
| `*T`, `object` | ❌ | 裸指针，不做 RAII |

---

## 6. 运行时保护（已实现）

- `mark_string_moved()` 仅对 `cap > 0` 生效，设 cap = -1
- `map.set` 若 key 已 moved（`cap < 0`），打印 warning 并 nop
- cap==0 时 `MAP_EMIT_COPY_KEY` 跳过深拷贝，直接存指针

---

## 7. 实现计划（优先级顺序）

String Phase A（顺序语句线性分析）→ String Phase B（if/else + 循环控制流）→ Struct Phase A/B → 借用语义

详细计划见 [move_semantics_plan.md](move_semantics_plan.md)
