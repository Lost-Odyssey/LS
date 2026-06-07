# 位模式匹配（Bit-level Pattern Matching）

> 状态：设计提案 | 优先级：中 | 预估工作量：~330 LOC / 2-3 天

---

## 1. 动机

### 1.1 问题

通信协议、网络报文、嵌入式寄存器、二进制文件格式——这些场景的核心操作是：**从一个固定宽度的整数中按位提取多个字段**。

```c
// C 语言的传统做法：
uint32_t raw = read_ecpri_packet();
uint8_t  trans_id = (raw >> 30) & 0x03;
uint8_t  dir       = (raw >> 29) & 0x01;
uint8_t  type     = (raw >> 24) & 0x1F;
// 程序员手动维护位偏移 → 极易数错格子 → 编译期零检查
```

痛点：
- **肉眼对齐**：每个字段的 `>> shift` 和 `& mask` 靠手工算，写错不报
- **总宽不校验**：字段加起来 31 位还是 33 位？不报
- **可读性差**：代码和协议规范（`[2:trans_id][1:dir][5:type]`）对不上

### 1.2 现有方案对比

| 语言 | 位域语法 | 编译期宽度校验 | 模式匹配集成 |
|------|---------|--------------|-------------|
| C | `struct field : 2` | ❌ 不跨字段校验 | ❌ |
| Rust | 无标准方案，靠 `bitfield!` 宏 | ❌ 宏内有限 | ❌ |
| Zig | 无 | ❌ | ❌ |
| Erlang | `<<x:2, y:1>> = bin` | ✅ | ✅ |
| **LS（提案）** | `bits[2:x][1:y]` | **✅ 总宽 vs 类型位宽** | **✅ match 内** |

### 1.3 目标

```
match raw_word {
    bits[2:trans_id][1:dir][5:type] => {
        // trans_id, dir, type 自动绑定
    }
}
```

---

## 2. 语法设计

### 2.1 基础位域提取

```ls
match raw_word {            // raw_word: u32 (32 bits)
    bits[2:trans_id]        // 位 [31:30] → trans_id (int)
         [1:dir]            // 位 [29:29] → dir (int)
         [5:type] => {      // 位 [28:24] → type (int)
        if type == 0x14 && dir == 0 {
            process(trans_id, type)
        }
    }
    _ => { drop_invalid() }
}
```

总宽校验：`2 + 1 + 5 = 8 ≠ 32` → **编译期报错**「expected 32 bits, got 8」。

### 2.2 匹配特定位值

```ls
match raw_word {
    bits[2:0b10][1:0][5:0x14] => {     // trans_id=2, dir=0, type=0x14
        process_specific()
    }
    bits[2:trans_id][1:dir][5:type] => { // 通用匹配，绑定到变量
        process_generic(trans_id, dir, type)
    }
    _ => { }
}
```

### 2.3 OR-pattern 组合

```ls
match raw_word {
    bits[4:0xA] | bits[4:0x5] => {    // 高 4 位匹配 0xA 或 0x5
        handle_special()
    }
    _ => { }
}
```

### 2.4 端序标注

**核心问题**：协议位布局始终是从 MSB 向下描述的（视觉从左到右 = bit 7→0）。但当你在 LE 主机（x86）上通过 `*(u32*)buf` 加载网络序（大端）数据时，字节在寄存器中是反的。

```ls
// 网络字节序 (big-endian)：从 MSB 开始解析。
// 内部自动 bswap（LE 主机）后再提取位域。
// O-RAN/Ethernet/IP 协议的标准用法。
match_be raw_word {
    bits[2:trans_id][1:dir][5:type] => { }
    _ => { }
}

// 主机字节序 (little-endian)：不 bswap，直接从 MSB 提取。
// 用于数据已在 host 字节序的场景（如硬件寄存器）。
match_le raw_word {
    bits[2:trans_id][1:dir][5:type] => { }
    _ => { }
}
```

**关键区别在 shift 值不同**，以 u32 为例：

```
缓冲区字节 (网络序):  [0xAA, 0xBB, 0xCC, 0xDD]
协议字段: bits[2:A][1:B][5:C]

match_be (自动 bswap):
  加载: val = *(u32*)buf       → 0xDDCCBBAA (LE host)
  bswap: val = __builtin_bswap32(val) → 0xAABBCCDD
  提取: A = (val >> 30) & 0x03   ← byte 0 的 bit 7:6 在 val 的 bit 31:30
        B = (val >> 29) & 0x01
        C = (val >> 24) & 0x1F

match_le (不做 bswap):
  加载: val = *(u32*)buf       → 0xDDCCBBAA (LE host)
  不 bswap
  提取: A = (val >> 6) & 0x03    ← byte 0 的 bit 7:6 在 val 的 bit 7:6
        B = (val >> 5) & 0x01
        C = (val >> 0) & 0x1F
```

两种方式的 `A/B/C` 提取结果相同，但 **shift 值不同**——因为 byte 0 在 LE 加载后落在 val 的 bit 7:0，而不是 bit 31:24。

**推荐用法**：
- 网络协议解析 → 一律用 `match_be`，无需手动 bswap
- 硬件寄存器 / 内存映射 I/O → 用 `match_le`
- 不确定时用 `match_be`（网络序是最常见的协议位序）

### 2.5 混合模式（未来扩展）

第一版只允许单一 `bits[...]` 序列作为一个 arm 的整个 pattern（不允许 `bits[...] | some_enum_variant` 等混合）。OR-pattern 只允许 `bits[...] | bits[...]`。

---

## 3. 语义

### 3.1 位域绑定规则

```
bits[2:trans_id][1:dir][5:type]
      ↑           ↑        ↑
    宽度        宽度      宽度
    变量名      变量名    变量名
```

- 字段按**从左到右**的顺序排列
- 第一个字段对应整数的**最高位**（或最低位，取决于端序模式）
- 每个字段的类型为 `int`（有符号？无符号？见 3.2）
- 变量名在 arm 体内作为一个普通变量使用

### 3.2 类型规则

| 字段 | 类型 | 说明 |
|------|------|------|
| `bits[1:x]` | `bool` | 1 位字段自动映射为 bool |
| `bits[N:x]` (N>1) | `int` | 无符号 int，范围 0..2^N-1 |

或：全部映射为 `uN`（N 位无符号整数），`bits[1:x]` 等价于 `bool`。

建议：**第一版统一映射为 `int`（无符号）**，与 LS 的 `int` 兼容。`bits[1:x]` 可自动转 `bool` 隐式（已有 `int → bool` 的隐式转换）。

### 3.3 位偏移计算

**核心问题**：协议位布局从 MSB 向下描述。通过 `*(u32*)buf` 加载网络序数据时，LE 主机寄存器值是字节反序的。`match_be`/`match_le` 的区别在于是否对主体做 bswap，以及由此导致 shift 值不同。

以网络缓冲区 `[0xAA, 0xBB, 0xCC, 0xDD]`、协议字段 `bits[2:A][1:B][5:C]` 为例：

```
协议 byte 0 = 0xAA = 10101010
  bit 7 6 5 4 3 2 1 0
      1 0 1 0 1 0 1 0
      [ A ][B][  C   ]    A=2(10) B=1(1) C=10(01010)

match_be (网络序):
  val = bswap(*(u32*)buf)   → 0xAABBCCDD
  MSB 31───24───16───8───0
  val: AA    BB    CC    DD
       [A][B][C]...
  A: shift=32- 0-2=30, mask=0x03  → (val>>30)&0x03 = 10 = 2 ✅
  B: shift=32- 2-1=29, mask=0x01  → (val>>29)&0x01 = 1  ✅
  C: shift=32- 3-5=24, mask=0x1F  → (val>>24)&0x1F = 01010 = 10 ✅

match_le (主机序):
  val = *(u32*)buf            → 0xDDCCBBAA
   byte 0 (0xAA) 在 val 的 bit 7:0，bit 次序不变
  MSB 31───24───16───8───0
  val: DD    CC    BB    AA
                         [A][B][C]
  A: shift=0*8+7-2+1=6,  mask=0x03  → (val>>6)&0x03 = 2  ✅
  B: shift=0*8+5-1+1=5,  mask=0x01  → (val>>5)&0x01 = 1  ✅
  C: shift=0*8+4-5+1=0,  mask=0x1F  → (val>>0)&0x1F = 10 ✅
```

**通用规则**：

```
match_be: shift_i = total_bits - accumulated - width_i
          accumulated += width_i  起始 0

match_le: 按字节分块，每个字节内从 bit 7 向下
          shift = byte_idx * 8 + (7 - bit_in_byte) - width + 1
          跨字节字段需分部分提取再组合
```

**跨字节字段**（`bits[12:X]`）：

```
match_be:
  shift = 32 - 0 - 12 = 20
  X = (val >> 20) & 0xFFF

match_le:
  lo = val & 0xFF             // byte 0 全部
  hi = (val >> 8) & 0x0F      // byte 1 高 4 bit
  X = (hi << 8) | lo
```

> 第一版优先实现 `match_be`。`match_le` 的跨字节提取更复杂，延后。用户如有 LE 数据，可手动 bswap 后用 `match_be`。

### 3.4 宽度校验

```ls
match raw_word {     // raw_word: u16 → 16 bits
    bits[8:high][8:low] => { }
    // 8+8 = 16 ✅
}

match raw_word {     // raw_word: u32 → 32 bits
    bits[16:high][8:mid][4:low] => { }
    // 16+8+4 = 28 ≠ 32 ❌ → error!
}
```

校验时机：**Checker 阶段**，在非枚举 match 路径中新增。

---

## 4. 技术实现

### 4.1 AST 扩展

`src/ast.h`：

```c
// 新增 AstNodeType:
AST_MATCH_BIT_PATTERN,   // bits[width:name] 或 bits[width:literal_value]

// AstNode union 新增字段:
struct {
    int            width;       // 位宽 (1-64)
    char          *name;        // 变量名 (NULL = 仅匹配不绑定)
    long long      match_val;   // 匹配特定值（仅在 match_value_set=true 时有效）
    bool           match_value_set;  // true = bits[4:0xA] 形式
} bit_pattern;

struct {
    AstNode **items;       // 每个 item 是 AST_MATCH_BIT_PATTERN
    int       count;
    int       total_width; // 编译期累加总位宽
} bit_pattern_seq;        // bits[...][...][...] 序列
```

### 4.2 Parser 扩展

`src/parser.c`：

```c
// 新增 prefix 规则: "bits" → prefix_bit_pattern_seq
// 注册在 parse_rules 表中（TOKEN_IDENTIFIER），检查 identifier == "bits"

static AstNode *prefix_bit_pattern_seq(Parser *p) {
    // bits 已消费
    // 预期: [width:name] [width:name] ...
    // 每次遇到 '[' 开始一个新字段
    // 直到遇到 '=>' 或 '|'

    consume(p, TOKEN_LBRACKET, "expected '[' after bits");
    AstNode **items = NULL;
    int count = 0, cap = 0;

    while (true) {
        // bits[width:name]
        // bits[width:0xVAL]
        // bits[width:_]

        // 解析 width
        int width = (int)consume_int_lit(p);

        consume(p, TOKEN_COLON, "expected ':' after width");

        // 解析 name / literal / _
        AstNode *item = new_node(AST_MATCH_BIT_PATTERN, line, col);
        item->as.bit_pattern.width = width;

        if (check(p, TOKEN_INT_LIT)) {
            item->as.bit_pattern.match_value_set = true;
            item->as.bit_pattern.match_val = consume_int_lit(p);
            item->as.bit_pattern.name = NULL;
        } else if (check(p, TOKEN_UNDERSCORE)) {
            advance(p); // consume '_'
            item->as.bit_pattern.name = NULL;
            item->as.bit_pattern.match_value_set = false;
        } else if (check(p, TOKEN_IDENTIFIER)) {
            advance(p);
            item->as.bit_pattern.name = str_dup_n(p->previous.start, p->previous.length);
            item->as.bit_pattern.match_value_set = false;
        } else {
            error(p, "expected name, literal, or '_' after ':'");
        }

        // append to items
        consume(p, TOKEN_RBRACKET, "expected ']' after bit field");

        // peek next: another '['? 继续; else break
        if (!check(p, TOKEN_LBRACKET)) break;
        advance(p); // consume '['

        ...
    }

    // 创建 bit_pattern_seq 节点
    AstNode *seq = new_node(AST_MATCH_BIT_PATTERN_SEQ, line, col);
    seq->as.bit_pattern_seq.items = items;
    seq->as.bit_pattern_seq.count = count;
    return seq;
}
```

Parser 还需要：
- `match_be` / `match_le` 作为新的 match 变体（或作为修饰符）
- 最简单：`match_be` 和 `match_le` 作为新 token（或作为标识符解析），parse 方式和 `match` 相同，但在 AST 中打上端序标记

### 4.3 Checker 扩展

`src/checker.c`，非枚举 match 路径（`5850:` `case AST_MATCH` 附近）：

```c
// 新增: 检测 subject 是否为整数类型
// 新增: 处理 AST_MATCH_BIT_PATTERN_SEQ

static void check_bit_pattern_seq(Checker *c, AstNode *seq, Type *subject_type) {
    // 1. 验证 subject_type 是整数类型
    int type_bit_width = type_bit_size(subject_type); // helper: u8=8, u16=16, u32=32, ...
    if (type_bit_width <= 0) {
        error("bit-pattern subject must be an integer type (u8-u64)");
        return;
    }

    // 2. 累加总位宽
    int total = 0;
    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++) {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        total += item->as.bit_pattern.width;
    }

    // 3. 校验总位宽
    if (total != type_bit_width) {
        error("bit-match total width %d does not match subject type (%d bits)",
              total, type_bit_width);
        return;
    }
    seq->as.bit_pattern_seq.total_width = total;

    // 4. 计算每个位域的 lsb_shift（从 LSB 起的偏移量），供 codegen 使用
    int accumulated = 0;  // 从 MSB 累计的宽度
    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++) {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        int width = item->as.bit_pattern.width;

        if (seq->is_big_endian) {
            // match_be: 第 i 个字段从 MSB 起偏移 accumulated
            // lsb_shift = total_width - accumulated - width
            item->as.bit_pattern.lsb_shift = type_bit_width - accumulated - width;
        } else {
            // match_le: 按字节分块，每个字节内从 bit 7 向下
            // 具体实现见 4.4（这里简化为第一版不支持 LE）
            item->as.bit_pattern.lsb_shift = 0; // 待实现
        }
        accumulated += width;

        // 绑定变量
        if (item->as.bit_pattern.name != NULL) {
            Type *field_type = (width == 1) ? type_bool() : type_int();
            Symbol *sym = scope_define(c->current_scope,
                                        item->as.bit_pattern.name,
                                        field_type);
            if (sym) {
                // store for codegen
                item->as.bit_pattern.lsb_shift = lsb_shift;
            }
        }
    }
}
```

注意：`lsb_shift` 需要在 `ast.h` 的 `bit_pattern` union 中加一个 `int lsb_shift` 字段。

### 4.4 Codegen 扩展

`src/codegen.c`，match 的 CondBr 路径（`12833:` `case AST_MATCH:` 非 enum 非 int-switch 路径）。

Checker 在 AST 节点中为每个位域计算出 `lsb_shift`（从 LSB 起的偏移量）和 `mask`。Codegen 只需要发射：

```llvm
; match_be 场景: 先 bswap 主体，再提取
; match_le 场景: 直接从主体提取

; 伪 IR:
%val = bswap(%subject)     ; 仅在 match_be 且 LE 主机时
                           ; match_le 时不用 bswap

; bits[2:A]  shift=30 mask=0x03  (match_be, u32)
; bits[2:A]  shift=6  mask=0x03  (match_le, u32)
%field = lshr %val, %shift
%field = and %field, %mask

; bits[1:B]  → 如果 width==1 转为 bool
%is_set = icmp ne %field, 0
```

Codegen 伪代码：

```c
static void handle_bit_pattern_arm(CodegenContext *ctx,
    AstNode *seq, LLVMValueRef subject_val,
    LLVMBasicBlockRef then_bb, LLVMBasicBlockRef next_bb)
{
    LLVMBuilderRef builder = ctx->builder;
    LLVMTypeRef i64_t = LLVMInt64TypeInContext(ctx->context);

    /* 1. 端序处理 */
    LLVMValueRef val = subject_val;
    if (ctx->match_endian == MATCH_BE) {
        /* 在 LE 主机上 bswap */
        int bit_width = type_bit_size(subject_type);
        LLVMValueRef bswap_fn = get_bswap_fn(ctx, bit_width);
        val = LLVMBuildCall2(builder, ..., bswap_fn, &val, 1, "be.swap");
    }

    /* 2. 提取位域 */
    LLVMValueRef match_cond = NULL;  /* 对 match_value 场景累积条件 */

    for (int i = 0; i < seq->as.bit_pattern_seq.count; i++) {
        AstNode *item = seq->as.bit_pattern_seq.items[i];
        int width = item->as.bit_pattern.width;
        int shift = item->as.bit_pattern.lsb_shift;  /* checker 已算好 */
        LLVMValueRef mask = LLVMConstInt(i64_t, (1ULL << width) - 1, 0);

        /* val = (val >> lsb_shift) & mask */
        LLVMValueRef shifted = LLVMBuildLShr(builder, val,
            LLVMConstInt(i64_t, shift, 0), "bit.shr");
        LLVMValueRef field_val = LLVMBuildAnd(builder, shifted, mask, "bit.val");

        /* width==1 → bool */
        if (width == 1) {
            field_val = LLVMBuildICmp(builder, LLVMIntNE, field_val,
                LLVMConstInt(i64_t, 0, 0), "bit.bool");
        }

        /* 绑定到变量 */
        if (item->as.bit_pattern.name != NULL) {
            CgSymbol *sym = cg_scope_resolve(ctx->current_scope,
                                              item->as.bit_pattern.name);
            if (sym) LLVMBuildStore(builder, field_val, sym->value);
        }

        /* match_value: bits[4:0xA] 比较 */
        if (item->as.bit_pattern.match_value_set) {
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntEQ, field_val,
                LLVMConstInt(i64_t, item->as.bit_pattern.match_val, 0), "bit.cmp");
            match_cond = match_cond
                ? LLVMBuildAnd(builder, match_cond, cmp, "bit.and")
                : cmp;
        }
    }

    /* 3. 生成分支 */
    if (match_cond) {
        LLVMBuildCondBr(builder, match_cond, then_bb, next_bb);
    } else {
        /* bind-only arm: 无条件匹配 */
        LLVMBuildBr(builder, then_bb);
    }
}
```

**Checker 计算 lsb_shift 的规则**（在 `check_bit_pattern_seq` 中完成）：

```c
// match_be:
int accumulated = 0;
for each field:
    field->as.bit_pattern.lsb_shift = total_width - accumulated - field.width;
    accumulated += field.width;

// match_le:
int byte_idx = 0;
int bit_in_byte = 7;
for each field:
    field->as.bit_pattern.lsb_shift = byte_idx * 8 + bit_in_byte - field.width + 1;
    // 处理跨字节:
    if (bit_in_byte - field.width + 1 < 0)
        → 需要分两部分提取，作为未来扩展
    bit_in_byte -= field.width;
    if (bit_in_byte < 0) { byte_idx++; bit_in_byte = 7; }
```

### 4.5 `match_be` / `match_le` 处理

最简单的实现方式：在 parser 中，`match_be x` 解析为带端序标记的 `AST_MATCH` 节点。Codegen 在计算 subject 之前，插入 `__builtin_bswap32`/`__builtin_bswap64`。

```llvm
; match_be raw_word (u32)
%swapped = call @llvm.bswap.i32(i32 %raw_word)
; 后续 shift/and 操作在 swapped 值上进行

; match_le raw_word (u32)
; 直接使用 raw_word，不做 swap
```

---

## 5. 各阶段改动汇总

| 文件 | 改动 | LOC |
|------|------|-----|
| `src/ast.h` | 新增 `AST_MATCH_BIT_PATTERN`、`AST_MATCH_BIT_PATTERN_SEQ`；union 字段；端序标记 | ~20 |
| `src/ast.c` | `ast_free` / `ast_dump` / `node_kind_name` 对应新节点 | ~15 |
| `src/parser.c` | `prefix_bit_pattern_seq` 函数；`match_be`/`match_le` 关键字或修饰符；precedence 表 | ~80 |
| `src/token.h` | 新增 `TOKEN_MATCH_BE` / `TOKEN_MATCH_LE`（可选，也可用标识符） | ~5 |
| `src/scanner.c` | 新增关键字 `match_be` / `match_le`（如果走 token 路线） | ~5 |
| `src/checker.c` | `check_bit_pattern_seq`、位宽校验、变量绑定 | ~60 |
| `src/checker.h` | `Checker` 结构体可能加一个临时端序标记 | ~5 |
| `src/codegen.c` | `handle_bit_pattern_arm`、bswap 插入、OR-pattern 扩展 | ~100 |
| `src/codegen.h` | `CodegenContext` 加端序标志（如需） | ~5 |
| `tests/samples/` | 测试文件 | ~150 |
| `tests/CMakeLists.txt` | 注册测试 | ~10 |
| **合计** | | **~455** |

---

## 6. 与现有功能的交互

| 功能 | 交互 | 处理 |
|------|------|------|
| **enum match** | 位模式只对整数有效 | 在位模式 match 中，subject 必须是整数类型；enum match 不变 |
| **OR-pattern** | bits[...] \| bits[...] | 通过现有的 `AST_MATCH_OR_PATTERN` + CondBr 路径自然支持 |
| **wildcard `_`** | bits[4:_] 跳过绑定 | `name = NULL`，不创建变量 |
| **整数 switch 优化** | 位模式带 match_value 时可用 switch？ | 不优化——位域组合后的值不连续，走 CondBr |
| **`try`** | match 内可用 try | 不影响 |
| **闭包** | match 内可用闭包 | 不影响 |
| **move-elision** | 位域绑定到 int/bool（POD） | 不影响 |
| **模块** | match be/le 在不同模块 | 不影响 |
| **JIT** | 位模式 match 在 JIT 中 | 正常运行（纯 LLVM IR 生成） |
| **memcheck** | 位模式不涉及内存分配 | 不影响 |

---

## 7. 测试方案

### 7.1 端到端测试 (tests/samples/)

```ls
// bit_match_basic.ls — 基础位域提取
fn main() {
    let raw: u32 = 0b10011000_00000000_00000000_00000000
    match raw {
        bits[2:trans_id][1:dir][5:type] => {
            assert(trans_id == 2)   // 0b10
            assert(dir == 0)        // 0b0
            assert(type == 19)      // 0b10011
        }
        _ => assert(false)
    }
}
```

| 测试文件 | 验证点 |
|---------|--------|
| `bit_match_basic.ls` | 3 字段提取 + 变量绑定 |
| `bit_match_width_check.ls` | 总宽 = 32 校验（u32 测试） |
| `bit_match_width_error.ls` | 总宽 ≠ 32 → 编译报错（预期错误测试） |
| `bit_match_value.ls` | bits[4:0xA] 匹配特定位值 |
| `bit_match_or.ls` | bits[4:0xA] \| bits[4:0x5] OR-pattern |
| `bit_match_wildcard.ls` | bits[4:_] 跳过绑定 |
| `bit_match_be.ls` | big-endian 解析 |
| `bit_match_le.ls` | little-endian 解析 |
| `bit_match_u16.ls` | u16 subject |
| `bit_match_u8.ls` | u8 subject |
| `bit_match_bool.ls` | bits[1:x] → bool |
| `bit_match_nested.ls` | 嵌套 if/match 内使用 |
| `bit_match_fuzz.ls` | 测试所有字段组合正确性 |
| `bit_match_memcheck.ls` | JIT + memcheck 0 leak |

### 7.2 C 单元测试

```
test_bit_pattern_parser:   验证 bits[...] 语法解析 AST 正确
test_bit_pattern_checker:  验证宽度校验、类型检查
test_bit_pattern_codegen:  验证 LLVM IR 中 shift/mask 正确
```

---

## 8. 实现顺序建议

```
Step 1:  ast.h   — 新节点类型 + union 字段                         (20 min)
Step 2:  ast.c   — ast_free / ast_dump 对应新节点                     (15 min)
Step 3:  parser.c — prefix_bit_pattern_seq + match_be/le             (1.5 h)
Step 4:  checker.c — check_bit_pattern_seq + 宽度校验 + 变量绑定       (1 h)
Step 5:  codegen.c — handle_bit_pattern_arm + bswap + CondBr 集成    (2 h)
Step 6:  基础测试修复                                               (1 h)
Step 7:  OR-pattern + match_value + wildcard 补全                     (1 h)
Step 8:  完整测试矩阵 + memcheck                                     (1.5 h)
                              总计: ~8 h = 1 个工作日
```

---

## 9. 未来扩展

| 版本 | 扩展 | 说明 |
|------|------|------|
| V1 | 固定位宽整数 match + 端序标注 | 本设计 |
| V2 | 位域与 enum 混合 match | `bits[2:id] \| ProtocolA =>` |
| V3 | 变长位域 | `bits[*:payload]` 需要运行时长度字段 |
| V4 | CRC 校验内联 | `bits[16:crc]` 自动验证校验和 |
| V5 | 位域 struct | `bitstruct Packet { [2:id] [6:type] }` 可复用的位域类型定义 |
