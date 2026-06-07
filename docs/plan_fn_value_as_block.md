# 设计：命名函数作为 `Block(...)` 值（函数→闭包强制转换）

> 状态：起草 2026-06-07（分支 `feat/rawvec`）。
> 来源：[plan_vec_replacement.md](plan_vec_replacement.md) §6.1 VR-LIM-010。
> 规模：语言特性（checker 类型兼容 + codegen 包装），中等。

---

## 1. 问题
`d.sort_by(cmp_desc)`（`cmp_desc` 是命名函数 `fn cmp_desc(int a, int b)->int`）报错：
expected 与 got **都是** `Block(int,int)->int`，但 checker 不接受**函数名**作为 `Block`
实参。绕行：`d.sort_by(|a,b| cmp_desc(a,b))`（包一层 closure）。

## 2. 根因
- **类型层**：命名函数的类型是 `TYPE_FUNCTION`；`Block` 参数期望 `TYPE_BLOCK`。checker 的
  Block 实参校验（`checker.c:3289-3301` 一带、及各 `arg->kind != TYPE_BLOCK` 检查
  `checker.c:3370/3399/3428`）虽对 `TYPE_FUNCTION || TYPE_BLOCK` 有部分放行，但**结构相容
  判定 + 作为 Block 传递**未打通：签名相同的 `TYPE_FUNCTION` 未被接受/转换为 `TYPE_BLOCK`。
- **ABI 层**：`Block` 是 fat-pointer `{ptr fn, ptr env}`（闭包字面量在 `codegen_closure_literal`
  构造）。命名函数是裸函数指针，**没有 env**。把函数名当 Block 传，需在调用点把它包成
  `{fn = &named_fn, env = null}`——被调方按 `blk.fn(blk.env, args...)` 调用时，env=null 对
  无捕获函数无害（函数忽略首参 env，或 ABI 约定首参为 env 占位）。

> 注意 ABI 细节：闭包体签名是 `ret __closure(ptr env, params...)`（env 在首位，
> `codegen.c:16125` 一带）。命名函数签名是 `ret f(params...)`（**无 env 首参**）。直接把
> `&f` 塞进 `blk.fn` 会因首参错位崩溃。故需**合成一个 thunk** `ret __fnthunk_f(ptr env,
> params...) { return f(params...) }`（忽略 env），用 `&__fnthunk_f` 作 `blk.fn`。

## 3. 设计
当**期望类型是 `Block(P...)->R`、实参是签名结构相容的命名函数（`TYPE_FUNCTION`）**时：
1. **checker**：判定 `TYPE_FUNCTION` 与目标 `Block` 的参数/返回**结构相容**（逐参 `type_equals`
   + 返回相容），相容则接受，并在实参 AST 上标记"需 fn→Block 包装 + 目标 Block 类型"。
2. **codegen**：对被标记的实参，合成（或复用缓存的）`__fnthunk_<f>(ptr env, params...)`
   thunk（体内 `return f(params...)`，忽略 env），构造 `Block` 值
   `{ fn = &__fnthunk_<f>, env = null }` 传入。env=null，被调方不读它（thunk 忽略）。
   - thunk 每个 (函数, 目标Block签名) 组合合成一次，按名缓存避免重复。
   - 无需 env drop（env=null，`cg_emit_block_env_drop` 对 NULL 安全，既有保证）。

## 4. 实现步骤
1. 复现：`tests/samples/fn_as_block_test.ls`：`fn cmp_desc(int,int)->int`，
   `d.sort_by(cmp_desc)`；以及 `v.each(printer)`（`fn printer(int)`）等。
2. checker：在 Block 实参校验处（`checker.c:3289+` / 各 `!= TYPE_BLOCK` 点），加"实参为
   `TYPE_FUNCTION` 且与目标 Block 结构相容 → 接受 + 标记"。
3. codegen 调用实参 lowering：见到该标记 → 合成/取 `__fnthunk_<f>` → 建 `{thunk, null}`
   Block 值。
4. 同时支持：直接把命名函数赋给 `Block` 变量（`Block(int,int)->int c = cmp_desc`）与作为
   返回值——同一包装逻辑（在 var-decl / return 的 Block 目标处复用）。
5. JIT+AOT+memcheck 全绿。

## 5. 工作量 / 风险
中（checker 相容判定 + codegen thunk 合成 + 三处使用点：实参/赋值/返回）。风险：
- thunk ABI（env 首参占位）必须与闭包调用约定一致——对照 `codegen.c` 闭包调用点
  （`blk.fn(blk.env, args...)`）确认参数顺序。
- 重载/同名函数（LS 目前无重载）下按名取函数即可；跨模块命名函数需用 mangled 名解析
  （复用既有命名函数符号解析）。
- 不可变 thunk 缓存键 = 函数符号名 + 目标 Block 签名（避免不同签名误共用）。

## 6. 收益
- 直接 `d.sort_by(cmp_desc)` / `v.each(log_line)` / `v.map(int)(to_int)`，免去样板 `|a,b|
  f(a,b)`。
- 让"函数即值"在 LS 里更完整（与已有 Block 闭包统一为同一可调用 ABI）。

## 7. 后续（非本轮）
- 方法引用作 Block（`v.each(obj.method)`）——需 bound-method（env=对象），比裸函数 thunk 复杂。
- 泛型命名函数实例作 Block（`identity(int)` 作 `Block(int)->int`）。
