# Block Clone / Container 借用 规划文档
# (可选阶段 Phase G — Block 深拷贝与容器借用)

## 背景

当前（Phase F.4A）的策略是：**拒绝**将 Block 从容器（vec / map / struct 字段）中复制出来，
因为这会造成 env 的别名共享，进而在 scope 退出时发生 double-free。

```ls
// 当前全部被 checker 拒绝：
H g = p.step1            // ❌ F.3：struct 字段读出
H g = ns[i]              // ❌ F.4A：vec 元素读出
H g = ops.get("mul")     // ❌ F.4A：map.get 读出

// 当前唯一合法的调用方式：
p.step1(args)            // ✅ 直接调用（临时 SSA 值，无 scope cleanup）
ns[i](args)              // ✅
ops.get("mul")(args)     // ✅
```

这个限制在大多数使用场景下是可以接受的，但有些场景需要把 Block 提取出来复用：

```ls
// 希望支持的场景：
H handler = ops.get("on_click")   // 提取后多次调用
handler(x1)
handler(x2)
handler(x3)
```

---

## 三种解决方案对比

### 方案 A（已实现）：Checker 拒绝 + 直接调用

优点：零 double-free，实现简单，和现有 F.3 struct 字段处理风格一致。  
缺点：不能把 Block 存入局部变量后多次调用，只能每次都写 `ops.get(k)(args)`。

---

### 方案 B：深拷贝（env clone）

语义：`H g = ns[i]` 分配一个新的 env，把所有 capture 字段深拷贝进去，`g` 完全独立持有。

#### 需要合成 `__env_clone_N` 函数

类似现有的 `__env_drop_N(env_ptr)`，为每个有 capture 的闭包合成：

```c
void* __env_clone_0(void* src_env) {
    Env0* src = (Env0*)src_env;
    Env0* dst = malloc(sizeof(Env0));
    dst->drop_fn = src->drop_fn;
    // 按 capture 类型逐字段处理：
    //   POD (int/f64/bool/char/*T): memcpy
    //   string (by-move):           __ls_string_clone(dst->s, src->s)
    //   vec (by-ref):               不支持（见下文限制）
    //   map (by-ref):               不支持（见下文限制）
    //   struct (by-move):           需要 struct clone（尚未实现）
    return (void*)dst;
}
```

#### LsBlock 结构扩展

当前 LsBlock = `{fn_ptr, env_ptr}` (16 bytes)。  
需要增加 `clone_fn` 指针：

```c
// 扩展后（24 bytes）：
typedef struct { void* fn_ptr; void* env_ptr; void* clone_fn; } LsBlock;
```

或者把 clone_fn 也存入 env 内部（env[1]，类似 drop_fn 存在 env[0]）：

```c
// env 布局（已有 drop_fn slot）：
// [0] drop_fn ptr
// [1] clone_fn ptr   ← 新增
// [2..N] capture 字段
```

后者不需要修改 LsBlock 大小，更简洁。

#### capture 类型限制

| capture 类型 | 是否支持 clone | 说明 |
|---|---|---|
| POD（int/f64/bool/char/*T/object） | ✅ | memcpy |
| string（by-move） | ✅ | `__ls_string_clone` |
| array(POD, N)（by-copy） | ✅ | memcpy |
| vec(T)（by-ref） | ❌ | env 存的是外层 alloca 指针，clone 无意义 |
| map(K,V)（by-ref） | ❌ | 同上 |
| struct(has_drop)（by-move） | ⚠️ | 需要 struct clone，尚未实现 |
| enum（未实现） | ❌ | 需要 payload clone |

**结论**：只有当闭包的所有 capture 都是 POD / string / array(POD) 时，才能 clone。  
checker 需要检查 capture 列表，对不可 clone 的闭包报编译错。

#### 工作量估计

- checker：新增 `capture_list_is_cloneable` 检查，`H g = ns[i]` 路径改为「可 clone 时允许，否则拒绝并提示」
- codegen：`codegen_closure_literal` 合成 `__env_clone_N`；`emit_auto_drop_fn`（struct 字段）/ `emit_vec_elem_drop_at`（vec 元素）在 pop 时调 clone
- ABI：env[1] 新增 clone_fn slot，env[0] drop_fn 偏移不变（向后兼容）
- 测试：新增 Phase G 测试样本，含 POD-only / string capture 的 clone 场景

**预计工作量**：2~3 天

---

### 方案 C：引用计数（Arc-like 共享所有权）

语义：多个 Block 变量指向同一 env，env 内置引用计数，最后一个 drop 时才释放。

```c
// env 头部扩展：
// [0] drop_fn ptr
// [1] ref_count (atomic_int)
// [2..N] capture 字段
```

优点：语义最自然，可以随意复制 Block。  
缺点：
- 引用计数本身有运行时开销（原子操作）
- 与 LS 的"手动 malloc/free，无 GC"设计原则相悖
- 实现复杂度最高

**当前不推荐，留待未来引入 Arc/Rc 类型时一并考虑。**

---

## 类似情况汇总（同样需要 clone 才能安全复制）

除了 Block 以外，LS 中以下情况同样存在类似的容器 aliasing 问题：

| 场景 | 当前状态 | 说明 |
|---|---|---|
| `string s2 = vec_of_string[i]` | ✅ 安全（已有 string clone） | vec 存 string 时 push 已深拷贝 |
| `string s2 = map.get(k)` | ✅ 安全 | map node 内 string 独立存储 |
| `MyStruct b = vec_of_struct[i]` | ⚠️ 若 has_drop 则 double-free | struct clone 尚未实现；checker 目前不拒绝 |
| `Block g = ns[i]` | ✅ F.4A 已拒绝 | 本文档主题 |
| `Block g = p.step1` | ✅ F.3 已拒绝 | struct 字段 |
| `Block g = ops.get(k)` | ✅ F.4A 已拒绝 | map 值 |

> **注**：`MyStruct b = vec_of_struct[i]`（has_drop struct）的 double-free 问题是一个独立的 bug，
> 应在 struct clone 阶段（Phase H 或 struct Phase C）一并修复。

---

## 推荐阶段划分

| 阶段 | 内容 | 前置条件 |
|---|---|---|
| Phase F.4A（已完成）| Checker 拒绝 Block 容器读出 | — |
| **Phase G**（可选）| Block env clone（方案 B），支持 POD+string capture | 无硬性前置，但 struct clone 实现后可扩展 struct capture |
| Phase H（独立）| struct clone（深拷贝带 has_drop 的 struct） | 无 |
| Phase G+H 合并后 | Block clone 完整支持（含 struct capture） | Phase H 完成 |
| 远期 | Block clone 支持 vec/map capture（需要 clone 语义的借用改造） | 生命期系统 |

---

## 激活 Phase G 的信号

当用户/代码中出现以下模式时，说明需要实现 Phase G：

```ls
// 需要把 Block 提取到局部变量后多次调用
H handler = dispatch_table.get(event_type)
if handler.env_ptr != NULL {   // 伪码
    handler(payload1)
    handler(payload2)
}

// 需要把 vec(Block) 的元素分发给多个调用者
H first = pipeline[0]
H second = pipeline[1]
// ... 分别在不同生命期内使用
```

---

## 文件位置

本文档：`docs/block_clone_plan.md`  
相关实现：`src/checker.c`（`checker_reject_block_vec_index` / `checker_reject_block_map_get`）  
相关测试：`tests/samples/closure_f4_test.ls`、`tests/test_phase_f4_closure.cmake`
