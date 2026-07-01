// Real keyword spellings from src/scanner.c's keyword table (the same set
// syntaxes/llvmscript.tmLanguage.json enumerates for highlighting, kept as
// an independent list here rather than shared across the grammar-JSON /
// server-JS boundary — ~35 words, duplication is cheaper than plumbing a
// shared module for something this small and this rarely changed).
const KEYWORDS = [
  'def', 'methods', 'interface', 'public', 'private', 'struct', 'enum', 'module',
  'import', 'load', 'extern', 'comptime', 'type', 'where',
  'if', 'else', 'while', 'for', 'in', 'match', 'break', 'continue', 'return', 'try',
  'as', 'new', 'from', 'static', 'self', 'true', 'false', 'nil',
  'int', 'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64',
  'f32', 'f64', 'f16', 'bf16', 'bool', 'char', 'void', 'lib', 'object', 'array',
  'Simd', 'Block',
];

module.exports = { KEYWORDS };
