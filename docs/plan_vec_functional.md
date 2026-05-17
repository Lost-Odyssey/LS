# Vec 集合函数式操作 — 实现说明书

## 0. 概述

为 `vec(T)` 添加接受 `Block` 闭包的高阶方法，填补 LS 与 Ruby/Rust/Go/C++ 在集合操作方面的核心差距。

**前置条件**：闭包 Phase F.7 ✅ 已完成（Block 类型、block_call codegen、env 所有权、RAII 全部就绪）。

**设计原则**：
- 所有新方法作为编译器内建 vec 方法实现（与现有 push/pop/sort_by 一致）
- Block 参数走现有 `codegen_block_call` 路径（间接 call `{fn_ptr, env_ptr}`）
- 闭包类型推导走现有 `expected_type` 传播机制
- 内存安全：Block 形参标 `is_borrowed`，不释放 caller 持有的 env

---

## 1. 目标 API

```ls
vec(int) nums = [3, 1, 4, 1, 5, 9, 2, 6]

// --- 谓词操作（返回 bool / int） ---
bool has_big   = nums.any(|x| x > 7)        // true
bool all_pos   = nums.all(|x| x > 0)        // true
int  big_count = nums.count(|x| x > 3)      // 4

// --- 查找 ---
Option(int) first_big = nums.find(|x| x > 7)   // Some(9)
int         idx       = nums.find_index(|x| x > 7)  // 5 (-1 if not found)

// --- 过滤（返回新 vec） ---
vec(int) evens = nums.filter(|x| x % 2 == 0)   // [4, 2, 6]

// --- 映射（返回新 vec，元素类型由 Block 返回类型决定） ---
vec(string) strs = nums.map(|x| f"{x}")         // ["3","1","4",...]
vec(int) doubled = nums.map(|x| x * 2)          // [6, 2, 8, ...]

// --- 归约 ---
int sum = nums.reduce(0, |acc, x| acc + x)      // 31

// --- 排序（升级现有 sort_by 支持 Block） ---
nums.sort_by(|a, b| a - b)                      // in-place sort with closure

// --- 遍历（副作用，无返回值） ---
nums.each(|x| print(x))
```

---

## 2. 方法签名与返回类型

| 方法 | 签名 | 返回类型 | Mutating | 说明 |
|------|------|----------|:--------:|------|
| `filter` | `v.filter(Block(T)->bool)` | `vec(T)` | ❌ | 新 vec，元素深拷贝 |
| `map` | `v.map(Block(T)->U)` | `vec(U)` | ❌ | 新 vec，U 从 Block 返回类型推导 |
| `reduce` | `v.reduce(A, Block(A,T)->A)` | `A` | ❌ | A 为 init 的类型 |
| `any` | `v.any(Block(T)->bool)` | `bool` | ❌ | 短路求值 |
| `all` | `v.all(Block(T)->bool)` | `bool` | ❌ | 短路求值 |
| `count` | `v.count(Block(T)->bool)` | `int` | ❌ | |
| `find` | `v.find(Block(T)->bool)` | `Option(T)` | ❌ | 第一个满足条件的元素 |
| `find_index` | `v.find_index(Block(T)->bool)` | `int` | ❌ | -1 表示未找到 |
| `each` | `v.each(Block(T)->void)` | `void` | ❌ | |
| `sort_by` | `v.sort_by(Block(T,T)->int)` | `void` | ✅ | 升级现有实现 |

---

## 3. Checker 实现（`src/checker.c`）

### 3.1 修改位置

在 `check_vector_method()` 函数（当前 line ~1577）中，在 `shrink_to_fit` 之后、fallback 错误消息之前，添加新方法分支。

### 3.2 各方法 Checker 逻辑

#### 3.2.1 `filter`

```c
/* v.filter(Block(T)->bool) -> vec(T) */
if (strcmp(method, "filter") == 0)
{
    if (argc != 1) { error("filter() takes 1 argument"); return NULL; }
    
    // 构造期望的 Block 类型：Block(elem) -> bool
    Type *expected_block = type_block(&elem, 1, type_bool());
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("filter() expects Block(T)->bool, got '%s'", type_name(arg));
        return NULL;
    }
    // 验证参数数量和返回类型
    if (arg->as.function.param_count != 1) {
        error("filter() predicate must take 1 argument"); return NULL;
    }
    Type *ret = arg->as.function.return_type;
    if (!ret || ret->kind != TYPE_BOOL) {
        error("filter() predicate must return bool"); return NULL;
    }
    return vec_type; // 返回同类型 vec(T)
}
```

#### 3.2.2 `map`

```c
/* v.map(Block(T)->U) -> vec(U) */
if (strcmp(method, "map") == 0)
{
    if (argc != 1) { error("map() takes 1 argument"); return NULL; }
    
    // 构造部分期望类型（只约束参数，返回类型由推导确定）
    // 注：expected_type 传播让闭包字面量的参数类型从 elem 推导
    Type *expected_block = type_block(&elem, 1, NULL); // ret=NULL 表示待推导
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("map() expects Block(T)->U, got '%s'", type_name(arg));
        return NULL;
    }
    if (arg->as.function.param_count != 1) {
        error("map() closure must take 1 argument"); return NULL;
    }
    
    // 返回类型 = vec(Block 的 return_type)
    Type *ret_elem = arg->as.function.return_type;
    if (!ret_elem) {
        error("map() closure must have a return type"); return NULL;
    }
    return type_vec(ret_elem);
}
```

**关键点**：`map` 的返回类型是 `vec(U)` 其中 U 来自 Block 的返回类型推导。需要在 `check_expr` 完成对闭包 body 的检查后，读取 `arg->as.function.return_type`。

#### 3.2.3 `reduce`

```c
/* v.reduce(init: A, Block(A,T)->A) -> A */
if (strcmp(method, "reduce") == 0)
{
    if (argc != 2) { error("reduce() takes 2 arguments (init, block)"); return NULL; }
    
    Type *init_type = check_expr(c, call_node->as.call.args[0]);
    if (!init_type) return NULL;
    
    // Block 期望签名：(A, T) -> A
    Type *block_params[2] = { init_type, elem };
    Type *expected_block = type_block(block_params, 2, init_type);
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[1]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("reduce() second argument must be Block(A,T)->A"); return NULL;
    }
    if (arg->as.function.param_count != 2) {
        error("reduce() block must take 2 arguments (accumulator, element)"); return NULL;
    }
    return init_type; // 返回类型 = 初始值类型
}
```

#### 3.2.4 `any` / `all` / `count`

```c
/* v.any(Block(T)->bool) -> bool */
/* v.all(Block(T)->bool) -> bool */
/* v.count(Block(T)->bool) -> int */
if (strcmp(method, "any") == 0 || strcmp(method, "all") == 0 ||
    strcmp(method, "count") == 0)
{
    if (argc != 1) { error("%s() takes 1 argument", method); return NULL; }
    
    Type *expected_block = type_block(&elem, 1, type_bool());
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("%s() expects Block(T)->bool", method); return NULL;
    }
    if (arg->as.function.param_count != 1) {
        error("%s() predicate must take 1 argument", method); return NULL;
    }
    return strcmp(method, "count") == 0 ? type_int() : type_bool();
}
```

#### 3.2.5 `find`

```c
/* v.find(Block(T)->bool) -> Option(T) */
if (strcmp(method, "find") == 0)
{
    if (argc != 1) { error("find() takes 1 argument"); return NULL; }
    
    Type *expected_block = type_block(&elem, 1, type_bool());
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("find() expects Block(T)->bool"); return NULL;
    }
    // 实例化 Option(elem) 并返回
    return checker_instantiate_option(c, elem);
}
```

#### 3.2.6 `find_index`

```c
/* v.find_index(Block(T)->bool) -> int */
if (strcmp(method, "find_index") == 0)
{
    // 与 any/all 相同的检查逻辑
    if (argc != 1) { error("find_index() takes 1 argument"); return NULL; }
    // ... (same predicate validation as any/all)
    return type_int();
}
```

#### 3.2.7 `each`

```c
/* v.each(Block(T)->void) -> void */
if (strcmp(method, "each") == 0)
{
    if (argc != 1) { error("each() takes 1 argument"); return NULL; }
    
    Type *expected_block = type_block(&elem, 1, type_void());
    Type *saved = c->expected_type;
    c->expected_type = expected_block;
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    c->expected_type = saved;
    type_free(expected_block);
    
    if (arg == NULL) return NULL;
    if (arg->kind != TYPE_BLOCK && arg->kind != TYPE_FUNCTION) {
        error("each() expects Block(T)->void"); return NULL;
    }
    return type_void();
}
```

#### 3.2.8 升级现有 `sort_by`

当前 `sort_by` 只接受 `TYPE_FUNCTION`，需要扩展为也接受 `TYPE_BLOCK`：

```c
// 现有代码（line ~1994）:
if (arg->kind != TYPE_FUNCTION)
// 改为:
if (arg->kind != TYPE_FUNCTION && arg->kind != TYPE_BLOCK)
```

并添加 `expected_type` 传播，使 `|a,b| a - b` 能推导参数类型。

### 3.3 错误消息更新

`check_vector_method` 末尾的 fallback 错误消息需要加入新方法名：

```c
"vec has no method '%s' (available: push, pop, clear, reserve, "
"is_empty, get, first, last, truncate, remove, swap, reverse, "
"extend, insert, contains, index_of, resize, copy, "
"sort, sort_by, slice, shrink_to_fit, "
"filter, map, reduce, any, all, count, find, find_index, each)",
```

### 3.4 借用限制

- `filter`/`map`/`reduce`/`any`/`all`/`count`/`find`/`find_index`/`each` 都是**只读操作**
- 不在 `vec_method_is_mutating()` 列表中
- `&vec(T)` 只读借用可以调用这些方法

---

## 4. Codegen 实现（`src/codegen.c`）

### 4.1 修改位置

在 `codegen_vec_method()` 函数中（当前 line ~5817），在 `sort/sort_by` 和 `slice` 之间或之后添加新分支。

### 4.2 公共模式：Block 调用机制

所有新方法共用一个核心模式：遍历 vec 元素 + 对每个元素调用 Block。

```c
// 1. 加载 vec 数据
LLVMValueRef sv = LLVMBuildLoad2(ctx->builder, vec_t, vec_alloca, "sv");
LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, sv, 0, "data");
LLVMValueRef len  = LLVMBuildExtractValue(ctx->builder, sv, 1, "len");

// 2. 求值 Block 参数（得到 {fn_ptr, env_ptr} struct）
LLVMValueRef block_val = codegen_expr(ctx, call_node->as.call.args[0]);
LLVMValueRef fn_ptr  = LLVMBuildExtractValue(ctx->builder, block_val, 0, "bfn");
LLVMValueRef env_ptr = LLVMBuildExtractValue(ctx->builder, block_val, 1, "benv");

// 3. 循环：for i in 0..len
//    load elem[i], call block(env_ptr, elem[i]), process result

// 4. Block call 使用 codegen_block_call 的内联版本：
//    LLVMTypeRef call_ft = LLVMFunctionType(ret_llvm, {ptr_t, elem_llvm}, 2, 0);
//    LLVMValueRef args[] = {env_ptr, elem_val};
//    LLVMValueRef result = LLVMBuildCall2(builder, call_ft, fn_ptr, args, 2, "");
```

### 4.3 各方法 Codegen 细节

#### 4.3.1 `filter` — 返回新 vec

```
IR 伪代码：
  new_vec = {malloc(elem_size * len), 0, len}   // 预分配同等容量
  i = 0
  loop:
    if i >= len goto end
    elem = load data[i]
    result = block_call(env, elem)              // bool
    if result { new_vec.push(clone(elem)) }     // 深拷贝满足条件的元素
    i++
    goto loop
  end:
    return new_vec
```

**关键**：
- 元素是 POD 时直接 memcpy（int/f64/bool）
- 元素是 string 时调 `emit_string_clone_val`
- 元素是 has_drop struct 时调 `emit_struct_clone_val`
- 这与现有 `slice()` / `copy()` 的元素克隆逻辑完全一致，可复用

#### 4.3.2 `map` — 返回新 vec(U)

```
IR 伪代码：
  U_size = sizeof(U)  // U 从 Block 返回类型的 LLVM type 获取
  new_vec = {malloc(U_size * len), 0, len}
  i = 0
  loop:
    if i >= len goto end
    elem = load data[i]
    result = block_call(env, elem)      // 返回 U 类型值
    store result into new_vec.data[i]
    new_vec.len++
    i++
    goto loop
  end:
    return new_vec
```

**关键**：
- 新 vec 的 LLVM struct type 是 `{ptr, i32, i32}` 不变
- 但 data buffer 的元素大小是 `LLVMABISizeOfType(U_llvm)`
- Block 返回 string / has_drop struct 时，闭包 body 已经产生了新的 owned 值（return alloca）——map 直接接管所有权存入新 vec，不额外 clone

#### 4.3.3 `reduce`

```
IR 伪代码：
  acc = init_val    // codegen_expr 求值第一个参数
  i = 0
  loop:
    if i >= len goto end
    elem = load data[i]
    acc = block_call(env, acc, elem)   // 2-arg Block
    i++
    goto loop
  end:
    return acc
```

**关键**：Block 有 2 个参数（acc + elem），fn_type 要构造 `(ptr, A_llvm, T_llvm) -> A_llvm`。

#### 4.3.4 `any` / `all`（短路求值）

```
IR 伪代码 (any):
  i = 0
  loop:
    if i >= len goto return_false
    elem = load data[i]
    result = block_call(env, elem)   // bool
    if result goto return_true       // 短路！
    i++
    goto loop
  return_true:  return 1
  return_false: return 0
```

`all` 相反：`if !result goto return_false`。

#### 4.3.5 `count`

```
IR 伪代码：
  n = 0
  i = 0
  loop:
    if i >= len goto end
    elem = load data[i]
    result = block_call(env, elem)   // bool
    if result { n++ }
    i++
    goto loop
  end:
    return n
```

#### 4.3.6 `find` — 返回 Option(T)

```
IR 伪代码：
  i = 0
  loop:
    if i >= len goto return_none
    elem = load data[i]
    result = block_call(env, elem)   // bool
    if result goto found
    i++
    goto loop
  found:
    return Some(clone(elem))    // 深拷贝元素构造 Option
  return_none:
    return None
```

**Option 构造**：使用现有的 Option 模板实例化机制（`checker_instantiate_option`），codegen 用 `disc=1 + payload` 构造 Some，`disc=0` 构造 None。参考 `try` 操作符的 Option 构造 IR。

#### 4.3.7 `find_index`

```
IR 伪代码：
  i = 0
  loop:
    if i >= len goto return_neg1
    elem = load data[i]
    result = block_call(env, elem)   // bool
    if result goto found
    i++
    goto loop
  found:      return i
  return_neg1: return -1
```

#### 4.3.8 `each`

```
IR 伪代码：
  i = 0
  loop:
    if i >= len goto end
    elem = load data[i]
    block_call(env, elem)   // void 返回，忽略
    i++
    goto loop
  end:
    return void
```

#### 4.3.9 升级 `sort_by` 支持 Block

当前 `sort_by` 直接取函数指针传给 qsort。Block 是 `{fn_ptr, env_ptr}` 胖指针，不能直接传。

**方案**：生成一个 trampoline 比较器函数，捕获 `env_ptr`：

```c
// 生成 __ls_vcmp_block_N(void* a, void* b) -> int:
//   A = *(T*)a
//   B = *(T*)b
//   return block_call(captured_env, A, B)
```

问题：qsort 的比较器签名是 `int (*)(const void*, const void*)`，无法传递额外的 env 指针。

**解决方案**：不用 qsort，改为在 IR 中内联生成插入排序或快速排序循环，Block 在循环中直接调用。对于 `sort_by` with Block：

```
IR 伪代码（插入排序，适合中小数组；大数组可改快排）：
  i = 1
  outer_loop:
    if i >= len goto end
    key = load data[i]
    j = i - 1
    inner_loop:
      if j < 0 goto insert
      elem_j = load data[j]
      cmp = block_call(env, elem_j, key)   // > 0 means elem_j > key
      if cmp <= 0 goto insert
      store elem_j at data[j+1]
      j--
      goto inner_loop
    insert:
      store key at data[j+1]
    i++
    goto outer_loop
  end:
```

**备选方案**（推荐）：使用全局 thread-local 存 env_ptr + fn_ptr，生成的 trampoline 从 global 读取。这样仍可用 qsort：

```c
// 模块级全局（per sort call）：
@__ls_sort_env = internal thread_local global ptr null
@__ls_sort_fn  = internal thread_local global ptr null

// Trampoline __ls_vcmp_block_N(void* a, void* b):
//   env = load @__ls_sort_env
//   fn  = load @__ls_sort_fn
//   A = *(T*)a, B = *(T*)b
//   return call fn(env, A, B)

// 调用前：
//   store block.env_ptr -> @__ls_sort_env
//   store block.fn_ptr  -> @__ls_sort_fn
//   call qsort(data, len, sizeof(T), @__ls_vcmp_block_N)
```

**推荐方案**：全局变量方案，复用 qsort，实现简单且 O(n log n) 保证。单线程下安全（LS 当前无并发）。

---

## 5. 内存安全考量

### 5.1 Block 参数的生命期

- Block 形参通过 `expected_type` 推导后，闭包 body 内的参数 alloca 标 `is_borrowed=true`
- 方法调用期间 Block 的 env 不被释放（caller 持有）
- 方法返回后，Block 变量（如果是命名变量）正常参与 scope cleanup

### 5.2 元素传递给 Block 的方式

| 元素类型 | 传递方式 | Block 内 |
|---------|---------|---------|
| POD (int/f64/bool) | by-value | 直接使用 |
| string | by-value (cap=0 borrow) | 只读借用，不 free |
| struct (has_drop) | by-pointer (借用) | 只读访问字段 |

**为什么元素用借用传递**：filter/map/any/all 是只读遍历，深拷贝每个元素传给闭包太昂贵。元素以借用方式传入 Block，Block 内部只能读取（与 `&T` 一致）。

**例外**：`map` 如果 Block 需要返回元素的 owned 值（如 `|x| x.copy()`），用户需显式 clone。但对 POD 和 string literal 这是自动的。

### 5.3 filter/find 返回的元素

返回新 vec / Option 中的元素是**深拷贝**（与 `slice`/`copy` 一致）：
- POD：memcpy
- string：`emit_string_clone_val`
- struct：`emit_struct_clone_val`

### 5.4 map 返回的元素

Block 返回值是 owned 的（闭包 body 的 `return` 产生新值或移交所有权），直接存入新 vec。

### 5.5 reduce 的累加器

Block 每次调用返回新的 acc 值。如果 acc 是 string/has_drop struct，旧 acc 需要在下一轮循环开始前释放。

方案：循环体内，调用 Block 得到 new_acc 后，`emit_drop(old_acc)` 再 `old_acc = new_acc`。

---

## 6. 类型推导策略

### 6.1 闭包参数类型

通过 `c->expected_type` 传播。Checker 在调用 `check_expr(closure_arg)` 之前设置期望的 Block 类型，闭包 body checker 从 `expected_type` 读取参数类型。

**这与现有 `push(Block)` / `map.set(key, Block)` 的推导机制一致**（Phase F.4 已验证）。

### 6.2 map 的返回类型推导

`map` 的特殊性：期望的 Block 类型的返回值部分是 unknown（`NULL`）。在 check_expr 完成后，从闭包的 resolved_type 读取实际返回类型。

需要确认：当 `expected_type` 的 return_type 为 NULL 时，闭包 checker 是否仍能正常推导 body 的返回类型。

**预期行为**：闭包 checker 在 Phase B 中的逻辑是：如果 expected_type 有 return_type 则使用它，否则从 body 的 AST_RETURN 推导。`map` 走后一种路径。

### 6.3 reduce 的双参数推导

reduce 的 Block 有两个参数（acc_type, elem_type），且返回 acc_type。期望类型精确指定所有三者。

---

## 7. 实现步骤（建议顺序）

### Phase V.1：谓词操作（最简单，验证 Block 调用模式）

1. Checker：添加 `any`/`all`/`count`/`each` 分支
2. Codegen：实现循环 + Block call 基础模板
3. 测试：`tests/samples/vec_functional_v1.ls`

### Phase V.2：filter + find + find_index

1. Checker：添加 `filter`/`find`/`find_index` 分支
2. Codegen：filter 需要新 vec 构造 + 元素深拷贝；find 需要 Option 构造
3. 测试：含 POD + string + struct 元素的过滤

### Phase V.3：map

1. Checker：添加 `map` 分支（返回类型推导）
2. Codegen：新 vec 元素类型与原 vec 不同的 buffer 分配
3. 测试：`nums.map(|x| f"{x}")` 跨类型映射

### Phase V.4：reduce

1. Checker：添加 `reduce` 分支（双参数 Block）
2. Codegen：acc 生命期管理（has_drop 时需 drop old acc）
3. 测试：求和、字符串拼接归约

### Phase V.5：sort_by Block 升级

1. 升级 checker 接受 TYPE_BLOCK
2. Codegen：thread-local trampoline 方案
3. 测试：`nums.sort_by(|a, b| a - b)`

---

## 8. 测试计划

### 8.1 端到端测试文件

```
tests/samples/vec_functional_v1.ls   — any/all/count/each
tests/samples/vec_functional_v2.ls   — filter/find/find_index
tests/samples/vec_functional_v3.ls   — map (同类型 + 跨类型)
tests/samples/vec_functional_v4.ls   — reduce
tests/samples/vec_functional_v5.ls   — sort_by with closure
```

### 8.2 每个测试文件的验证维度

- AOT 编译 + 运行
- JIT 运行
- `--memcheck` 0 leaks / 0 double-free

### 8.3 关键测试用例

```ls
// 基础 int
vec(int) nums = [1, 2, 3, 4, 5]
vec(int) evens = nums.filter(|x| x % 2 == 0)
assert(evens.length == 2)
assert(evens[0] == 2)
assert(evens[1] == 4)

// string 元素 filter（验证深拷贝）
vec(string) words = ["hello", "world", "hi"]
vec(string) short = words.filter(|w| w.length <= 3)
assert(short.length == 1)
assert(short[0] == "hi")

// map 跨类型
vec(int) nums2 = [10, 20, 30]
vec(string) strs = nums2.map(|x| f"val={x}")
assert(strs[0] == "val=10")

// reduce
int sum = nums.reduce(0, |acc, x| acc + x)
assert(sum == 15)

// find + Option
Option(int) r = nums.find(|x| x > 3)
match r { Some(v) => { assert(v == 4) }  None => { assert(false) } }

// any/all
assert(nums.any(|x| x == 3))
assert(nums.all(|x| x > 0))
assert(!nums.all(|x| x > 3))

// sort_by with closure capture
int direction = 1
nums.sort_by(|a, b| (a - b) * direction)  // 验证闭包捕获
```

---

## 9. 已知限制与未来扩展

### 9.1 当前限制

- **无惰性求值**：`filter`/`map` 立即求值产生新 vec，不支持 Rust 的 `.iter().filter().map().collect()` 链式惰性模式
- **无泛型**：`map` 的返回类型由编译器在具体调用点推导，不能写出泛化的 `fn my_map<T,U>(...)` 用户函数
- **元素借用语义固定**：Block 内的元素参数始终是借用（不可 move 出），即使元素是 POD。这简化了实现但略增语法噪音（如 `filter` 内需要 `x.copy()` 才能返回 owned 字符串）

### 9.2 未来扩展（泛型到位后）

- `flat_map(Block(T)->vec(U)) -> vec(U)`
- `zip(vec(U)) -> vec(Pair(T,U))`（需要 Pair 泛型 struct）
- `enumerate() -> vec(Pair(int,T))`
- `chunk(n) -> vec(vec(T))`
- `take(n) / skip(n) / take_while(pred) / skip_while(pred)`
- Iterator 协议（惰性）

---

## 10. 工作量估算

| Phase | 内容 | 估算 |
|-------|------|------|
| V.1 | any/all/count/each | 1 天 |
| V.2 | filter/find/find_index | 1.5 天 |
| V.3 | map（跨类型） | 1 天 |
| V.4 | reduce | 0.5 天 |
| V.5 | sort_by Block 升级 | 0.5 天 |
| 测试 | 5 个测试文件 + memcheck | 1 天 |
| **合计** | | **5.5 天** |
