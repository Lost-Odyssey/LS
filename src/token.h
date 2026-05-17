/* token.h — Token type enum and Token struct */
#ifndef LS_TOKEN_H
#define LS_TOKEN_H

typedef enum {
    /* Literals */
    TOKEN_INT_LIT,          /* 42, 0xFF, 0b1010 */
    TOKEN_FLOAT_LIT,        /* 3.14, 1.0e-5 */
    TOKEN_STRING_LIT,       /* "hello" */
    TOKEN_CHAR_LIT,         /* 'a' */
    TOKEN_TRUE,             /* true */
    TOKEN_FALSE,            /* false */
    TOKEN_NIL,              /* nil */

    /* Keywords */
    TOKEN_FN,               /* fn */
    TOKEN_RETURN,           /* return */
    TOKEN_IF,               /* if */
    TOKEN_ELSE,             /* else */
    TOKEN_WHILE,            /* while */
    TOKEN_FOR,              /* for */
    TOKEN_IN,               /* in */
    TOKEN_MATCH,            /* match */
    TOKEN_STRUCT,           /* struct */
    TOKEN_ENUM,             /* enum */
    TOKEN_IMPL,             /* impl */
    TOKEN_MODULE,           /* module */
    TOKEN_IMPORT,           /* import */
    TOKEN_LOAD,             /* load (FFI) */
    TOKEN_SELF,             /* self */
    TOKEN_STATIC,           /* static */
    TOKEN_DO,               /* do (reserved) */
    TOKEN_END,              /* end (reserved) */
    TOKEN_BREAK,            /* break */
    TOKEN_CONTINUE,         /* continue */
    TOKEN_EXTERN,           /* extern */
    TOKEN_AS,               /* as (type cast) */
    TOKEN_FROM,             /* from (FFI) */
    TOKEN_PUB,              /* pub (reserved) */
    TOKEN_NEW,              /* new (heap allocation) */
    TOKEN_TRY,              /* try (early return for Result/Option) */
    TOKEN_TYPE_ALIAS,       /* type (type alias keyword: `type Name = T`) */
    TOKEN_BLOCK,            /* Block (closure type keyword: `Block(args) -> ret`) */

    /* Annotations */
    TOKEN_AT_TIME,          /* @time (timing annotation) */
    TOKEN_AT_BENCH,         /* @bench (benchmark annotation) */

    /* Type keywords */
    TOKEN_TYPE_INT,         /* int */
    TOKEN_TYPE_I8,          /* i8 */
    TOKEN_TYPE_I16,         /* i16 */
    TOKEN_TYPE_I32,         /* i32 */
    TOKEN_TYPE_I64,         /* i64 */
    TOKEN_TYPE_U8,          /* u8 */
    TOKEN_TYPE_U16,         /* u16 */
    TOKEN_TYPE_U32,         /* u32 */
    TOKEN_TYPE_U64,         /* u64 */
    TOKEN_TYPE_F32,         /* f32 */
    TOKEN_TYPE_F64,         /* f64 */
    TOKEN_TYPE_BOOL,        /* bool */
    TOKEN_TYPE_CHAR,        /* char */
    TOKEN_TYPE_STRING,      /* string */
    TOKEN_TYPE_VOID,        /* void */
    TOKEN_TYPE_LIB,         /* lib */
    TOKEN_TYPE_OBJECT,      /* object (type-erased pointer, like void*) */
    TOKEN_ARRAY,            /* array (fixed-size array type) */
    TOKEN_VEC,              /* vec   (dynamic vector type) */
    TOKEN_MAP,              /* map   (hash map type) */

    /* Identifier */
    TOKEN_IDENTIFIER,       /* foo, bar_baz */

    /* Operators */
    TOKEN_PLUS,             /* + */
    TOKEN_MINUS,            /* - */
    TOKEN_STAR,             /* * */
    TOKEN_SLASH,            /* / */
    TOKEN_PERCENT,          /* % */
    TOKEN_AMP,              /* & */
    TOKEN_PIPE,             /* | */
    TOKEN_CARET,            /* ^ */
    TOKEN_TILDE,            /* ~ */
    TOKEN_LSHIFT,           /* << */
    TOKEN_RSHIFT,           /* >> */
    TOKEN_AND,              /* && */
    TOKEN_OR,               /* || */
    TOKEN_BANG,             /* ! */
    TOKEN_EQ,               /* == */
    TOKEN_NEQ,              /* != */
    TOKEN_LT,               /* < */
    TOKEN_GT,               /* > */
    TOKEN_LEQ,              /* <= */
    TOKEN_GEQ,              /* >= */

    /* Assignment */
    TOKEN_ASSIGN,           /* = */
    TOKEN_PLUS_ASSIGN,      /* += */
    TOKEN_MINUS_ASSIGN,     /* -= */
    TOKEN_STAR_ASSIGN,      /* *= */
    TOKEN_SLASH_ASSIGN,     /* /= */

    /* Delimiters */
    TOKEN_LPAREN,           /* ( */
    TOKEN_RPAREN,           /* ) */
    TOKEN_LBRACE,           /* { */
    TOKEN_RBRACE,           /* } */
    TOKEN_LBRACKET,         /* [ */
    TOKEN_RBRACKET,         /* ] */
    TOKEN_SEMICOLON,        /* ; */
    TOKEN_COLON,            /* : */
    TOKEN_COMMA,            /* , */
    TOKEN_DOT,              /* . */
    TOKEN_ARROW,            /* -> */
    TOKEN_FAT_ARROW,        /* => */
    TOKEN_UNDERSCORE,       /* _ (wildcard in match) */
    TOKEN_DOTDOT,           /* .. (range, reserved) */
    TOKEN_ELLIPSIS,         /* ... (varargs) */

    /* Format string (f"...{expr}...") */
    TOKEN_FSTRING_START,    /* f" — begins format string */
    TOKEN_FSTRING_TEXT,     /* literal text segment between { } */
    TOKEN_FSTRING_END,      /* closing " of format string */

    /* Special */
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    const char *start;     /* Points into source string */
    int length;
    int line;
    int column;
} Token;

/* Return a human-readable name for a token type */
const char *token_type_name(TokenType type);

#endif /* LS_TOKEN_H */
