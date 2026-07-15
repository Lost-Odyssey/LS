; ModuleID = 'samples/enum_basic_test.lls'
source_filename = "samples/enum_basic_test.lls"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%std_core_reflect_core__RawType = type { ptr, ptr, i32, ptr, i32 }
%std_core_reflect_core__RawField = type { ptr, ptr }
%std_core_reflect_core__RawMethod = type { ptr, ptr, i1 }
%std_core_str_core__Str = type { ptr, i32, i32 }
%std_core_str_core__StrSlice = type { ptr, i32 }
%"Vec(std_core_str_core__StrSlice)" = type { ptr, i32, i32 }
%"Vec(int)" = type { ptr, i32, i32 }
%"Vec(std_core_str_core__Str)" = type { ptr, i32, i32 }
%"Result(int,std_core_str_core__Str)" = type { i8, [2 x i64] }
%"Result(i64,std_core_str_core__Str)" = type { i8, [2 x i64] }
%"Result(f64,std_core_str_core__Str)" = type { i8, [2 x i64] }
%"Result(bool,std_core_str_core__Str)" = type { i8, [2 x i64] }
%Color = type { i8, [0 x i8] }

@.ls.rawstr = private unnamed_addr constant [4 x i8] c"Str\00", align 1
@.ls.rawstr.1 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@.ls.rawstr.2 = private unnamed_addr constant [4 x i8] c"*u8\00", align 1
@.ls.rawstr.3 = private unnamed_addr constant [4 x i8] c"len\00", align 1
@.ls.rawstr.4 = private unnamed_addr constant [4 x i8] c"int\00", align 1
@.ls.rawstr.5 = private unnamed_addr constant [4 x i8] c"cap\00", align 1
@.ls.rawstr.6 = private unnamed_addr constant [4 x i8] c"int\00", align 1
@.ls.rawstr.7 = private unnamed_addr constant [8 x i8] c"reserve\00", align 1
@.ls.rawstr.8 = private unnamed_addr constant [25 x i8] c"def reserve(&!self, int)\00", align 1
@.ls.rawstr.9 = private unnamed_addr constant [4 x i8] c"len\00", align 1
@.ls.rawstr.10 = private unnamed_addr constant [22 x i8] c"def len(&self) -> int\00", align 1
@.ls.rawstr.11 = private unnamed_addr constant [4 x i8] c"cap\00", align 1
@.ls.rawstr.12 = private unnamed_addr constant [22 x i8] c"def cap(&self) -> int\00", align 1
@.ls.rawstr.13 = private unnamed_addr constant [7 x i8] c"empty?\00", align 1
@.ls.rawstr.14 = private unnamed_addr constant [26 x i8] c"def empty?(&self) -> bool\00", align 1
@.ls.rawstr.15 = private unnamed_addr constant [7 x i8] c"as_ptr\00", align 1
@.ls.rawstr.16 = private unnamed_addr constant [23 x i8] c"def as_ptr(&self) -> ?\00", align 1
@.ls.rawstr.17 = private unnamed_addr constant [6 x i8] c"c_str\00", align 1
@.ls.rawstr.18 = private unnamed_addr constant [25 x i8] c"def c_str(&!self) -> *u8\00", align 1
@.ls.rawstr.19 = private unnamed_addr constant [8 x i8] c"byte_at\00", align 1
@.ls.rawstr.20 = private unnamed_addr constant [31 x i8] c"def byte_at(&self, int) -> int\00", align 1
@.ls.rawstr.21 = private unnamed_addr constant [9 x i8] c"byte_at!\00", align 1
@.ls.rawstr.22 = private unnamed_addr constant [32 x i8] c"def byte_at!(&self, int) -> int\00", align 1
@.ls.rawstr.23 = private unnamed_addr constant [10 x i8] c"push_byte\00", align 1
@.ls.rawstr.24 = private unnamed_addr constant [27 x i8] c"def push_byte(&!self, int)\00", align 1
@.ls.rawstr.25 = private unnamed_addr constant [9 x i8] c"push_str\00", align 1
@.ls.rawstr.26 = private unnamed_addr constant [27 x i8] c"def push_str(&!self, &Str)\00", align 1
@.ls.rawstr.27 = private unnamed_addr constant [14 x i8] c"__from_static\00", align 1
@.ls.rawstr.28 = private unnamed_addr constant [35 x i8] c"def __from_static(*u8, int) -> Str\00", align 1
@.ls.rawstr.29 = private unnamed_addr constant [13 x i8] c"__from_parts\00", align 1
@.ls.rawstr.30 = private unnamed_addr constant [26 x i8] c"def __from_parts() -> Str\00", align 1
@.ls.rawstr.31 = private unnamed_addr constant [4 x i8] c"eq?\00", align 1
@.ls.rawstr.32 = private unnamed_addr constant [29 x i8] c"def eq?(&self, &Str) -> bool\00", align 1
@.ls.rawstr.33 = private unnamed_addr constant [8 x i8] c"compare\00", align 1
@.ls.rawstr.34 = private unnamed_addr constant [32 x i8] c"def compare(&self, &Str) -> int\00", align 1
@.ls.rawstr.35 = private unnamed_addr constant [7 x i8] c"substr\00", align 1
@.ls.rawstr.36 = private unnamed_addr constant [35 x i8] c"def substr(&self, int, int) -> Str\00", align 1
@.ls.rawstr.37 = private unnamed_addr constant [5 x i8] c"copy\00", align 1
@.ls.rawstr.38 = private unnamed_addr constant [23 x i8] c"def copy(&self) -> Str\00", align 1
@.ls.rawstr.39 = private unnamed_addr constant [9 x i8] c"as_slice\00", align 1
@.ls.rawstr.40 = private unnamed_addr constant [32 x i8] c"def as_slice(&self) -> StrSlice\00", align 1
@.ls.rawstr.41 = private unnamed_addr constant [9 x i8] c"subslice\00", align 1
@.ls.rawstr.42 = private unnamed_addr constant [42 x i8] c"def subslice(&self, int, int) -> StrSlice\00", align 1
@.ls.rawstr.43 = private unnamed_addr constant [11 x i8] c"split_view\00", align 1
@.ls.rawstr.44 = private unnamed_addr constant [45 x i8] c"def split_view(&self, &Str) -> Vec(StrSlice)\00", align 1
@.ls.rawstr.45 = private unnamed_addr constant [10 x i8] c"slice_str\00", align 1
@.ls.rawstr.46 = private unnamed_addr constant [38 x i8] c"def slice_str(&self, StrSlice) -> Str\00", align 1
@.ls.rawstr.47 = private unnamed_addr constant [6 x i8] c"clone\00", align 1
@.ls.rawstr.48 = private unnamed_addr constant [24 x i8] c"def clone(&self) -> Str\00", align 1
@.ls.rawstr.49 = private unnamed_addr constant [2 x i8] c"~\00", align 1
@.ls.rawstr.50 = private unnamed_addr constant [14 x i8] c"def ~(&!self)\00", align 1
@.ls.rawstr.51 = private unnamed_addr constant [3 x i8] c"==\00", align 1
@.ls.rawstr.52 = private unnamed_addr constant [28 x i8] c"def ==(&self, &Str) -> bool\00", align 1
@.ls.rawstr.53 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@.ls.rawstr.54 = private unnamed_addr constant [23 x i8] c"def hash(&self) -> u64\00", align 1
@.ls.rawstr.55 = private unnamed_addr constant [2 x i8] c"+\00", align 1
@.ls.rawstr.56 = private unnamed_addr constant [26 x i8] c"def +(&self, &Str) -> Str\00", align 1
@.ls.rawstr.57 = private unnamed_addr constant [2 x i8] c"<\00", align 1
@.ls.rawstr.58 = private unnamed_addr constant [27 x i8] c"def <(&self, &Str) -> bool\00", align 1
@.ls.strlit = private unnamed_addr constant [29 x i8] c"Str byte index out of bounds\00", align 1
@.ls.fmt = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.59 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.60 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@.ls.strlit.61 = private unnamed_addr constant [34 x i8] c"StrSlice byte index out of bounds\00", align 1
@.ls.fmt.62 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.63 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.64 = private unnamed_addr constant [13 x i8] c"empty string\00", align 1
@.ls.strlit.65 = private unnamed_addr constant [10 x i8] c"no digits\00", align 1
@.ls.strlit.66 = private unnamed_addr constant [14 x i8] c"no hex digits\00", align 1
@.ls.strlit.67 = private unnamed_addr constant [18 x i8] c"invalid hex digit\00", align 1
@.ls.strlit.68 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.69 = private unnamed_addr constant [13 x i8] c"empty string\00", align 1
@.ls.strlit.70 = private unnamed_addr constant [10 x i8] c"no digits\00", align 1
@.ls.strlit.71 = private unnamed_addr constant [14 x i8] c"no hex digits\00", align 1
@.ls.strlit.72 = private unnamed_addr constant [18 x i8] c"invalid hex digit\00", align 1
@.ls.strlit.73 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.74 = private unnamed_addr constant [13 x i8] c"empty string\00", align 1
@.ls.strlit.75 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.76 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.77 = private unnamed_addr constant [10 x i8] c"no digits\00", align 1
@.ls.strlit.78 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.79 = private unnamed_addr constant [10 x i8] c"no digits\00", align 1
@.ls.strlit.80 = private unnamed_addr constant [14 x i8] c"invalid digit\00", align 1
@.ls.strlit.81 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@.ls.strlit.82 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@.ls.strlit.83 = private unnamed_addr constant [13 x i8] c"invalid bool\00", align 1
@.ls.fmt.84 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@.ls.fmt.85 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@.ls.fmt.86 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

declare i32 @printf(ptr %0, ...)

declare i32 @__ls_printf(ptr %0, ...)

declare i32 @puts(ptr %0)

declare ptr @malloc(i64 %0)

declare void @free(ptr %0)

declare ptr @realloc(ptr %0, i64 %1)

declare ptr @calloc(i64 %0, i64 %1)

declare double @sqrt(double %0)

declare i32 @sprintf(ptr %0, ptr %1, ...)

declare i64 @strlen(ptr %0)

declare ptr @memcpy(ptr %0, ptr %1, i64 %2)

declare ptr @memmove(ptr %0, ptr %1, i64 %2)

declare void @qsort(ptr %0, i64 %1, i64 %2, ptr %3)

declare i32 @strcmp(ptr %0, ptr %1)

declare ptr @strstr(ptr %0, ptr %1)

declare i32 @strncmp(ptr %0, ptr %1, i64 %2)

define ptr @__ls_str_replace(ptr %0, i32 %1, ptr %2, i32 %3, ptr %4, i32 %5, ptr %6) {
entry:
  %dst = alloca ptr, align 8
  %src = alloca ptr, align 8
  %p = alloca ptr, align 8
  %count = alloca i32, align 4
  store i32 0, ptr %count, align 4
  store ptr %0, ptr %p, align 8
  br label %cnt.cond

cnt.cond:                                         ; preds = %cnt.body, %entry
  %p.val = load ptr, ptr %p, align 8
  %found = call ptr @strstr(ptr %p.val, ptr %2)
  %notnull = icmp ne ptr %found, null
  br i1 %notnull, label %cnt.body, label %cnt.end

cnt.body:                                         ; preds = %cnt.cond
  %cnt = load i32, ptr %count, align 4
  %cnt.inc = add i32 %cnt, 1
  store i32 %cnt.inc, ptr %count, align 4
  %adv = getelementptr i8, ptr %found, i32 %3
  store ptr %adv, ptr %p, align 8
  br label %cnt.cond

cnt.end:                                          ; preds = %cnt.cond
  %fcnt = load i32, ptr %count, align 4
  %diff = sub i32 %5, %3
  %delta = mul i32 %fcnt, %diff
  %rlen = add i32 %1, %delta
  store i32 %rlen, ptr %6, align 4
  %asz = add i32 %rlen, 1
  %asz64 = zext i32 %asz to i64
  %buf = call ptr @malloc(i64 %asz64)
  store ptr %0, ptr %src, align 8
  store ptr %buf, ptr %dst, align 8
  br label %cp.cond

cp.cond:                                          ; preds = %cp.body, %cnt.end
  %src.v = load ptr, ptr %src, align 8
  %found2 = call ptr @strstr(ptr %src.v, ptr %2)
  %nn2 = icmp ne ptr %found2, null
  br i1 %nn2, label %cp.body, label %cp.end

cp.body:                                          ; preds = %cp.cond
  %src2 = load ptr, ptr %src, align 8
  %dst2 = load ptr, ptr %dst, align 8
  %fint = ptrtoint ptr %found2 to i64
  %sint = ptrtoint ptr %src2 to i64
  %seg64 = sub i64 %fint, %sint
  %7 = call ptr @memcpy(ptr %dst2, ptr %src2, i64 %seg64)
  %seg32 = trunc i64 %seg64 to i32
  %dadv = getelementptr i8, ptr %dst2, i32 %seg32
  %nl64 = zext i32 %5 to i64
  %8 = call ptr @memcpy(ptr %dadv, ptr %4, i64 %nl64)
  %dadv2 = getelementptr i8, ptr %dadv, i32 %5
  store ptr %dadv2, ptr %dst, align 8
  %sadv = getelementptr i8, ptr %found2, i32 %3
  store ptr %sadv, ptr %src, align 8
  br label %cp.cond

cp.end:                                           ; preds = %cp.cond
  %fsrc = load ptr, ptr %src, align 8
  %fdst = load ptr, ptr %dst, align 8
  %send = getelementptr i8, ptr %0, i32 %1
  %seint = ptrtoint ptr %send to i64
  %fsint = ptrtoint ptr %fsrc to i64
  %remain = sub i64 %seint, %fsint
  %rem1 = add i64 %remain, 1
  %9 = call ptr @memcpy(ptr %fdst, ptr %fsrc, i64 %rem1)
  ret ptr %buf
}

declare ptr @LoadLibraryA(ptr %0)

declare ptr @GetProcAddress(ptr %0, ptr %1)

declare i32 @FreeLibrary(ptr %0)

declare ptr @fopen(ptr %0, ptr %1)

declare i32 @fclose(ptr %0)

declare i32 @fflush(ptr %0)

declare i64 @fread(ptr %0, i64 %1, i64 %2, ptr %3)

declare i64 @fwrite(ptr %0, i64 %1, i64 %2, ptr %3)

declare i32 @system(ptr %0)

declare ptr @strerror(i32 %0)

define internal void @std_sys_c__abort() {
entry:
  call void @__ls_proc_exit(i32 1)
  ret void
}

declare i32 @__ls_get_argc()

declare ptr @__ls_get_argv(i32 %0)

; Function Attrs: cold noreturn
declare void @__ls_proc_exit(i32 %0) #0

declare i32 @__ls_str_find(ptr %0, i32 %1, ptr %2, i32 %3, i32 %4)

declare void @__ls_bytecopy(ptr %0, i32 %1, ptr %2, i32 %3, i32 %4)

declare ptr @__ls_ptr_at(ptr %0, i64 %1)

declare ptr @__ls_stdout()

declare ptr @__ls_stderr()

declare void @__ls_sink_set(ptr %0, i32 %1)

declare ptr @__ls_sink_stream()

declare void @__ls_float_fixed_exec(double %0, i32 %1)

declare ptr @__ls_float_fixed_ptr()

declare i64 @__ls_fxhash_bytes(ptr %0, i32 %1)

declare i32 @__ls_cpu_count()

declare i32 @__ls_cache_kb(i32 %0)

declare i32 @__ls_cpu_has_avx512()

declare i64 @__ls_load_u8(ptr %0, i64 %1)

declare i64 @__ls_load_be_u16(ptr %0, i64 %1)

declare i64 @__ls_load_be_u32(ptr %0, i64 %1)

declare i64 @__ls_load_be_u64(ptr %0, i64 %1)

declare i64 @__ls_load_le_u16(ptr %0, i64 %1)

declare i64 @__ls_load_le_u32(ptr %0, i64 %1)

declare i64 @__ls_load_le_u64(ptr %0, i64 %1)

declare void @__ls_readline_exec()

declare i32 @__ls_readline_ok()

declare i64 @__ls_readline_len()

declare ptr @__ls_readline_take()

declare ptr @__ls_readline_ptr()

declare i32 @__ls_regex_compile(ptr %0, i32 %1)

declare void @__ls_regex_free(i32 %0)

declare ptr @__ls_regex_last_error()

declare i32 @__ls_regex_exec(i32 %0, ptr %1, i32 %2, i32 %3)

declare i32 @__ls_regex_cap_start(i32 %0)

declare i32 @__ls_regex_cap_len(i32 %0)

declare i32 @__ls_regex_group_count(i32 %0)

declare i32 @__ls_regex_named_count(i32 %0)

declare ptr @__ls_regex_named_name(i32 %0, i32 %1)

declare i32 @__ls_regex_named_index(i32 %0, i32 %1)

define internal i64 @std_core_hash__fx_seed() {
entry:
  ret i64 5871781006564002453
}

define internal i64 @std_core_hash__fx_mix(i64 %0, i64 %1) {
entry:
  %r = alloca i64, align 8
  %x = alloca i64, align 8
  %word = alloca i64, align 8
  %h = alloca i64, align 8
  store i64 %0, ptr %h, align 8
  store i64 %1, ptr %word, align 8
  %h1 = load i64, ptr %h, align 8
  %word2 = load i64, ptr %word, align 8
  %xor = xor i64 %h1, %word2
  store i64 %xor, ptr %x, align 8
  %x3 = load i64, ptr %x, align 8
  %shl = shl i64 %x3, 5
  %x4 = load i64, ptr %x, align 8
  %lshr = lshr i64 %x4, 59
  %or = or i64 %shl, %lshr
  store i64 %or, ptr %r, align 8
  %r5 = load i64, ptr %r, align 8
  %call = call i64 @std_core_hash__fx_seed()
  %mul = mul i64 %r5, %call
  ret i64 %mul
}

define %std_core_reflect_core__RawType @std_core_str_core__Str.reflect_raw() {
entry:
  %var.moved = alloca i1, align 1
  %__rt = alloca %std_core_reflect_core__RawType, align 8
  call void @llvm.lifetime.start.p0(i64 40, ptr %__rt)
  store i1 false, ptr %var.moved, align 1
  store %std_core_reflect_core__RawType zeroinitializer, ptr %__rt, align 8
  %call = call %std_core_reflect_core__RawType @std_core_reflect_core__RawType.make(ptr @.ls.rawstr, i32 3, i32 26)
  store %std_core_reflect_core__RawType %call, ptr %__rt, align 8
  call void @std_core_reflect_core__RawType.set_field(ptr %__rt, i32 0, ptr @.ls.rawstr.1, ptr @.ls.rawstr.2)
  call void @std_core_reflect_core__RawType.set_field(ptr %__rt, i32 1, ptr @.ls.rawstr.3, ptr @.ls.rawstr.4)
  call void @std_core_reflect_core__RawType.set_field(ptr %__rt, i32 2, ptr @.ls.rawstr.5, ptr @.ls.rawstr.6)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 0, ptr @.ls.rawstr.7, ptr @.ls.rawstr.8, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 1, ptr @.ls.rawstr.9, ptr @.ls.rawstr.10, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 2, ptr @.ls.rawstr.11, ptr @.ls.rawstr.12, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 3, ptr @.ls.rawstr.13, ptr @.ls.rawstr.14, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 4, ptr @.ls.rawstr.15, ptr @.ls.rawstr.16, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 5, ptr @.ls.rawstr.17, ptr @.ls.rawstr.18, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 6, ptr @.ls.rawstr.19, ptr @.ls.rawstr.20, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 7, ptr @.ls.rawstr.21, ptr @.ls.rawstr.22, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 8, ptr @.ls.rawstr.23, ptr @.ls.rawstr.24, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 9, ptr @.ls.rawstr.25, ptr @.ls.rawstr.26, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 10, ptr @.ls.rawstr.27, ptr @.ls.rawstr.28, i1 true)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 11, ptr @.ls.rawstr.29, ptr @.ls.rawstr.30, i1 true)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 12, ptr @.ls.rawstr.31, ptr @.ls.rawstr.32, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 13, ptr @.ls.rawstr.33, ptr @.ls.rawstr.34, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 14, ptr @.ls.rawstr.35, ptr @.ls.rawstr.36, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 15, ptr @.ls.rawstr.37, ptr @.ls.rawstr.38, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 16, ptr @.ls.rawstr.39, ptr @.ls.rawstr.40, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 17, ptr @.ls.rawstr.41, ptr @.ls.rawstr.42, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 18, ptr @.ls.rawstr.43, ptr @.ls.rawstr.44, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 19, ptr @.ls.rawstr.45, ptr @.ls.rawstr.46, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 20, ptr @.ls.rawstr.47, ptr @.ls.rawstr.48, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 21, ptr @.ls.rawstr.49, ptr @.ls.rawstr.50, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 22, ptr @.ls.rawstr.51, ptr @.ls.rawstr.52, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 23, ptr @.ls.rawstr.53, ptr @.ls.rawstr.54, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 24, ptr @.ls.rawstr.55, ptr @.ls.rawstr.56, i1 false)
  call void @std_core_reflect_core__RawType.set_method(ptr %__rt, i32 25, ptr @.ls.rawstr.57, ptr @.ls.rawstr.58, i1 false)
  %__rt1 = load %std_core_reflect_core__RawType, ptr %__rt, align 8
  ret %std_core_reflect_core__RawType %__rt1
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg %0, ptr nocapture %1) #1

define %std_core_reflect_core__RawType @std_core_reflect_core__RawType.make(ptr %0, i32 %1, i32 %2) {
entry:
  %sl.tmp = alloca %std_core_reflect_core__RawType, align 8
  %mm = alloca ptr, align 8
  %ff = alloca ptr, align 8
  %z = alloca ptr, align 8
  %nm = alloca i32, align 4
  %nf = alloca i32, align 4
  %name = alloca ptr, align 8
  store ptr %0, ptr %name, align 8
  store i32 %1, ptr %nf, align 4
  store i32 %2, ptr %nm, align 4
  store ptr null, ptr %z, align 8
  %z1 = load ptr, ptr %z, align 8
  store ptr %z1, ptr %ff, align 8
  %z2 = load ptr, ptr %z, align 8
  store ptr %z2, ptr %mm, align 8
  %nf3 = load i32, ptr %nf, align 4
  %sgt = icmp sgt i32 %nf3, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %nf4 = load i32, ptr %nf, align 4
  %widen.sext = sext i32 %nf4 to i64
  %mul = mul nsw i64 %widen.sext, ptrtoint (ptr getelementptr (%std_core_reflect_core__RawField, ptr null, i32 1) to i64)
  %call = call ptr @malloc(i64 %mul)
  store ptr %call, ptr %ff, align 8
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %nm5 = load i32, ptr %nm, align 4
  %sgt6 = icmp sgt i32 %nm5, 0
  br i1 %sgt6, label %if.then7, label %if.merge8

if.then7:                                         ; preds = %if.merge
  %nm9 = load i32, ptr %nm, align 4
  %widen.sext10 = sext i32 %nm9 to i64
  %mul11 = mul nsw i64 %widen.sext10, ptrtoint (ptr getelementptr (%std_core_reflect_core__RawMethod, ptr null, i32 1) to i64)
  %call12 = call ptr @malloc(i64 %mul11)
  store ptr %call12, ptr %mm, align 8
  br label %if.merge8

if.merge8:                                        ; preds = %if.then7, %if.merge
  store %std_core_reflect_core__RawType zeroinitializer, ptr %sl.tmp, align 8
  %name13 = load ptr, ptr %name, align 8
  %field_ptr = getelementptr inbounds %std_core_reflect_core__RawType, ptr %sl.tmp, i32 0, i32 0
  store ptr %name13, ptr %field_ptr, align 8
  %ff14 = load ptr, ptr %ff, align 8
  %field_ptr15 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %sl.tmp, i32 0, i32 1
  store ptr %ff14, ptr %field_ptr15, align 8
  %nf16 = load i32, ptr %nf, align 4
  %field_ptr17 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %sl.tmp, i32 0, i32 2
  store i32 %nf16, ptr %field_ptr17, align 4
  %mm18 = load ptr, ptr %mm, align 8
  %field_ptr19 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %sl.tmp, i32 0, i32 3
  store ptr %mm18, ptr %field_ptr19, align 8
  %nm20 = load i32, ptr %nm, align 4
  %field_ptr21 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %sl.tmp, i32 0, i32 4
  store i32 %nm20, ptr %field_ptr21, align 4
  %sl.val = load %std_core_reflect_core__RawType, ptr %sl.tmp, align 8
  ret %std_core_reflect_core__RawType %sl.val
}

define void @std_core_reflect_core__RawType.set_field(ptr noalias nocapture nonnull align 8 dereferenceable(40) %0, i32 %1, ptr %2, ptr %3) {
entry:
  %sl.tmp = alloca %std_core_reflect_core__RawField, align 8
  %type_name = alloca ptr, align 8
  %name = alloca ptr, align 8
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  store ptr %2, ptr %name, align 8
  store ptr %3, ptr %type_name, align 8
  store %std_core_reflect_core__RawField zeroinitializer, ptr %sl.tmp, align 8
  %name1 = load ptr, ptr %name, align 8
  %field_ptr = getelementptr inbounds %std_core_reflect_core__RawField, ptr %sl.tmp, i32 0, i32 0
  store ptr %name1, ptr %field_ptr, align 8
  %type_name2 = load ptr, ptr %type_name, align 8
  %field_ptr3 = getelementptr inbounds %std_core_reflect_core__RawField, ptr %sl.tmp, i32 0, i32 1
  store ptr %type_name2, ptr %field_ptr3, align 8
  %sl.val = load %std_core_reflect_core__RawField, ptr %sl.tmp, align 8
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 1
  %fields = load ptr, ptr %field, align 8
  %i4 = load i32, ptr %i, align 4
  %pis.idx = sext i32 %i4 to i64
  %pis.ep = getelementptr %std_core_reflect_core__RawField, ptr %fields, i64 %pis.idx
  store %std_core_reflect_core__RawField %sl.val, ptr %pis.ep, align 8
  ret void
}

define void @std_core_reflect_core__RawType.set_method(ptr noalias nocapture nonnull align 8 dereferenceable(40) %0, i32 %1, ptr %2, ptr %3, i1 %4) {
entry:
  %sl.tmp = alloca %std_core_reflect_core__RawMethod, align 8
  %is_static = alloca i1, align 1
  %sig = alloca ptr, align 8
  %name = alloca ptr, align 8
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  store ptr %2, ptr %name, align 8
  store ptr %3, ptr %sig, align 8
  store i1 %4, ptr %is_static, align 1
  store %std_core_reflect_core__RawMethod zeroinitializer, ptr %sl.tmp, align 8
  %name1 = load ptr, ptr %name, align 8
  %field_ptr = getelementptr inbounds %std_core_reflect_core__RawMethod, ptr %sl.tmp, i32 0, i32 0
  store ptr %name1, ptr %field_ptr, align 8
  %sig2 = load ptr, ptr %sig, align 8
  %field_ptr3 = getelementptr inbounds %std_core_reflect_core__RawMethod, ptr %sl.tmp, i32 0, i32 1
  store ptr %sig2, ptr %field_ptr3, align 8
  %is_static4 = load i1, ptr %is_static, align 1
  %field_ptr5 = getelementptr inbounds %std_core_reflect_core__RawMethod, ptr %sl.tmp, i32 0, i32 2
  store i1 %is_static4, ptr %field_ptr5, align 1
  %sl.val = load %std_core_reflect_core__RawMethod, ptr %sl.tmp, align 8
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 3
  %funcs = load ptr, ptr %field, align 8
  %i6 = load i32, ptr %i, align 4
  %pis.idx = sext i32 %i6 to i64
  %pis.ep = getelementptr %std_core_reflect_core__RawMethod, ptr %funcs, i64 %pis.idx
  store %std_core_reflect_core__RawMethod %sl.val, ptr %pis.ep, align 8
  ret void
}

define void @std_core_str_core__Str.reserve(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %nd = alloca ptr, align 8
  %n = alloca i32, align 4
  %need = alloca i32, align 4
  store i32 %1, ptr %need, align 4
  %need1 = load i32, ptr %need, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sle = icmp sle i32 %need1, %cap
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret void

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap3 = load i32, ptr %field2, align 4
  store i32 %cap3, ptr %n, align 4
  %n4 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %n4, 8
  br i1 %slt, label %if.then5, label %if.merge6

if.then5:                                         ; preds = %if.merge
  store i32 8, ptr %n, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.then5, %if.merge
  br label %while.cond

while.cond:                                       ; preds = %while.body, %if.merge6
  %n7 = load i32, ptr %n, align 4
  %need8 = load i32, ptr %need, align 4
  %slt9 = icmp slt i32 %n7, %need8
  br i1 %slt9, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %n10 = load i32, ptr %n, align 4
  %mul = mul nsw i32 %n10, 2
  store i32 %mul, ptr %n, align 4
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %field11 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap12 = load i32, ptr %field11, align 4
  %sle13 = icmp sle i32 %cap12, 0
  br i1 %sle13, label %if.then14, label %if.merge15

if.then14:                                        ; preds = %while.end
  %n16 = load i32, ptr %n, align 4
  %widen.sext = sext i32 %n16 to i64
  %call = call ptr @malloc(i64 %widen.sext)
  store ptr %call, ptr %nd, align 8
  %nd17 = load ptr, ptr %nd, align 8
  %field18 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field18, align 8
  %field19 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field19, align 4
  %bc.i64 = sext i32 %len to i64
  %bc.dst = getelementptr i8, ptr %nd17, i64 0
  %bc.src = getelementptr i8, ptr %data, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %nd20 = load ptr, ptr %nd, align 8
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  store ptr %nd20, ptr %field.ptr, align 8
  %n21 = load i32, ptr %n, align 4
  %field.ptr22 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  store i32 %n21, ptr %field.ptr22, align 4
  ret void

if.merge15:                                       ; preds = %while.end
  %field23 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data24 = load ptr, ptr %field23, align 8
  %n25 = load i32, ptr %n, align 4
  %widen.sext26 = sext i32 %n25 to i64
  %call27 = call ptr @realloc(ptr %data24, i64 %widen.sext26)
  %field.ptr28 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  store ptr %call27, ptr %field.ptr28, align 8
  %n29 = load i32, ptr %n, align 4
  %field.ptr30 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  store i32 %n29, ptr %field.ptr30, align 4
  ret void
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly %0, ptr noalias nocapture readonly %1, i64 %2, i1 immarg %3) #2

define i32 @std_core_str_core__Str.len(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  ret i32 %len
}

define i32 @std_core_str_core__Str.cap(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  ret i32 %cap
}

define i1 @"std_core_str_core__Str.empty?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %eq = icmp eq i32 %len, 0
  ret i1 %eq
}

define ptr @std_core_str_core__Str.as_ptr(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  ret ptr %data
}

define ptr @std_core_str_core__Str.c_str(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %eq = icmp eq i32 %cap, 0
  br i1 %eq, label %sc.rhs, label %sc.merge

sc.rhs:                                           ; preds = %entry
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %sgt = icmp sgt i32 %len, 0
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %entry
  %sc = phi i1 [ %eq, %entry ], [ %sgt, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  ret ptr %data

if.merge:                                         ; preds = %sc.merge
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %add = add nsw i32 %len4, 1
  call void @std_core_str_core__Str.reserve(ptr %0, i32 %add)
  %field5 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data6 = load ptr, ptr %field5, align 8
  %field7 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len8 = load i32, ptr %field7, align 4
  %pis.idx = sext i32 %len8 to i64
  %pis.ep = getelementptr i8, ptr %data6, i64 %pis.idx
  store i8 0, ptr %pis.ep, align 1
  %field9 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data10 = load ptr, ptr %field9, align 8
  ret ptr %data10
}

define i32 @std_core_str_core__Str.byte_at(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %slt = icmp slt i32 %i1, 0
  br i1 %slt, label %sc.merge, label %sc.rhs

sc.rhs:                                           ; preds = %entry
  %i2 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %sge = icmp sge i32 %i2, %len
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %entry
  %sc = phi i1 [ %slt, %entry ], [ %sge, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %2 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt, i32 28, ptr @.ls.strlit)
  %3 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.59)
  call void @std_sys_c__abort()
  br label %if.merge

if.merge:                                         ; preds = %if.then, %sc.merge
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field3, align 8
  %i4 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i4 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  ret i32 %widen.zext
}

define i32 @"std_core_str_core__Str.byte_at!"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  %i1 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i1 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  ret i32 %widen.zext
}

define void @std_core_str_core__Str.push_byte(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %b = alloca i32, align 4
  store i32 %1, ptr %b, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %add = add nsw i32 %len, 1
  call void @std_core_str_core__Str.reserve(ptr %0, i32 %add)
  %b1 = load i32, ptr %b, align 4
  %trunc = trunc i32 %b1 to i8
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %pis.idx = sext i32 %len4 to i64
  %pis.ep = getelementptr i8, ptr %data, i64 %pis.idx
  store i8 %trunc, ptr %pis.ep, align 1
  %field5 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %add7 = add nsw i32 %len6, 1
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  store i32 %add7, ptr %field.ptr, align 4
  ret void
}

define void @std_core_str_core__Str.push_str(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %n3 = load i32, ptr %n, align 4
  %add = add nsw i32 %len2, %n3
  call void @std_core_str_core__Str.reserve(ptr %0, i32 %add)
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %field5 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %field7 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data8 = load ptr, ptr %field7, align 8
  %n9 = load i32, ptr %n, align 4
  %bc.i64 = sext i32 %len6 to i64
  %bc.i6410 = sext i32 %n9 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 %bc.i64
  %bc.src = getelementptr i8, ptr %data8, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i6410, i1 false)
  %field11 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len12 = load i32, ptr %field11, align 4
  %n13 = load i32, ptr %n, align 4
  %add14 = add nsw i32 %len12, %n13
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  store i32 %add14, ptr %field.ptr, align 4
  ret void
}

define %std_core_str_core__Str @std_core_str_core__Str.__from_static(ptr %0, i32 %1) {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %len = alloca i32, align 4
  %ptr = alloca ptr, align 8
  store ptr %0, ptr %ptr, align 8
  store i32 %1, ptr %len, align 4
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %ptr1 = load ptr, ptr %ptr, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %ptr1, ptr %field_ptr, align 8
  %len2 = load i32, ptr %len, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 %len2, ptr %field_ptr3, align 4
  %field_ptr4 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr4, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  ret %std_core_str_core__Str %sl.val
}

define %std_core_str_core__Str @std_core_str_core__Str.__from_parts() {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  store ptr null, ptr %z, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  ret %std_core_str_core__Str %sl.val
}

define i1 @"std_core_str_core__Str.eq?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %i = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %ne = icmp ne i32 %len, %len2
  br i1 %ne, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %len5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data9 = load ptr, ptr %field8, align 8
  %i10 = load i32, ptr %i, align 4
  %pi.idx11 = sext i32 %i10 to i64
  %ptr.idx12 = getelementptr i8, ptr %data9, i64 %pi.idx11
  %ptr.elem13 = load i8, ptr %ptr.idx12, align 1
  %ne14 = icmp ne i8 %ptr.elem, %ptr.elem13
  br i1 %ne14, label %if.then15, label %if.merge16

for.update:                                       ; preds = %if.merge16
  %i17 = load i32, ptr %i, align 4
  %add = add nsw i32 %i17, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then15:                                        ; preds = %for.body
  ret i1 false

if.merge16:                                       ; preds = %for.body
  br label %for.update
}

define i32 @std_core_str_core__Str.compare(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %y = alloca i32, align 4
  %x = alloca i32, align 4
  %i = alloca i32, align 4
  %n = alloca i32, align 4
  %b = alloca i32, align 4
  %a = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %a, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %b, align 4
  %a3 = load i32, ptr %a, align 4
  store i32 %a3, ptr %n, align 4
  %b4 = load i32, ptr %b, align 4
  %n5 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %b4, %n5
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %b6 = load i32, ptr %b, align 4
  store i32 %b6, ptr %n, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i7 = load i32, ptr %i, align 4
  %n8 = load i32, ptr %n, align 4
  %slt9 = icmp slt i32 %i7, %n8
  br i1 %slt9, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field10 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field10, align 8
  %i11 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i11 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %x, align 4
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data13 = load ptr, ptr %field12, align 8
  %i14 = load i32, ptr %i, align 4
  %pi.idx15 = sext i32 %i14 to i64
  %ptr.idx16 = getelementptr i8, ptr %data13, i64 %pi.idx15
  %ptr.elem17 = load i8, ptr %ptr.idx16, align 1
  %widen.zext18 = zext i8 %ptr.elem17 to i32
  store i32 %widen.zext18, ptr %y, align 4
  %x19 = load i32, ptr %x, align 4
  %y20 = load i32, ptr %y, align 4
  %slt21 = icmp slt i32 %x19, %y20
  br i1 %slt21, label %if.then22, label %if.merge23

for.update:                                       ; preds = %if.merge27
  %i28 = load i32, ptr %i, align 4
  %add = add nsw i32 %i28, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %a29 = load i32, ptr %a, align 4
  %b30 = load i32, ptr %b, align 4
  %slt31 = icmp slt i32 %a29, %b30
  br i1 %slt31, label %if.then32, label %if.merge33

if.then22:                                        ; preds = %for.body
  ret i32 -1

if.merge23:                                       ; preds = %for.body
  %x24 = load i32, ptr %x, align 4
  %y25 = load i32, ptr %y, align 4
  %sgt = icmp sgt i32 %x24, %y25
  br i1 %sgt, label %if.then26, label %if.merge27

if.then26:                                        ; preds = %if.merge23
  ret i32 1

if.merge27:                                       ; preds = %if.merge23
  br label %for.update

if.then32:                                        ; preds = %for.end
  ret i32 -1

if.merge33:                                       ; preds = %for.end
  %a34 = load i32, ptr %a, align 4
  %b35 = load i32, ptr %b, align 4
  %sgt36 = icmp sgt i32 %a34, %b35
  br i1 %sgt36, label %if.then37, label %if.merge38

if.then37:                                        ; preds = %if.merge33
  ret i32 1

if.merge38:                                       ; preds = %if.merge33
  ret i32 0
}

define %std_core_str_core__Str @std_core_str_core__Str.substr(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1, i32 %2) {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %l = alloca i32, align 4
  %s = alloca i32, align 4
  %n = alloca i32, align 4
  %len = alloca i32, align 4
  %start = alloca i32, align 4
  store i32 %1, ptr %start, align 4
  store i32 %2, ptr %len, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len1 = load i32, ptr %field, align 4
  store i32 %len1, ptr %n, align 4
  %start2 = load i32, ptr %start, align 4
  store i32 %start2, ptr %s, align 4
  %s3 = load i32, ptr %s, align 4
  %slt = icmp slt i32 %s3, 0
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store i32 0, ptr %s, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %s4 = load i32, ptr %s, align 4
  %n5 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %s4, %n5
  br i1 %sgt, label %if.then6, label %if.merge7

if.then6:                                         ; preds = %if.merge
  %n8 = load i32, ptr %n, align 4
  store i32 %n8, ptr %s, align 4
  br label %if.merge7

if.merge7:                                        ; preds = %if.then6, %if.merge
  %len9 = load i32, ptr %len, align 4
  store i32 %len9, ptr %l, align 4
  %l10 = load i32, ptr %l, align 4
  %slt11 = icmp slt i32 %l10, 0
  br i1 %slt11, label %if.then12, label %if.merge13

if.then12:                                        ; preds = %if.merge7
  store i32 0, ptr %l, align 4
  br label %if.merge13

if.merge13:                                       ; preds = %if.then12, %if.merge7
  %s14 = load i32, ptr %s, align 4
  %l15 = load i32, ptr %l, align 4
  %add = add nsw i32 %s14, %l15
  %n16 = load i32, ptr %n, align 4
  %sgt17 = icmp sgt i32 %add, %n16
  br i1 %sgt17, label %if.then18, label %if.merge19

if.then18:                                        ; preds = %if.merge13
  %n20 = load i32, ptr %n, align 4
  %s21 = load i32, ptr %s, align 4
  %sub = sub nsw i32 %n20, %s21
  store i32 %sub, ptr %l, align 4
  br label %if.merge19

if.merge19:                                       ; preds = %if.then18, %if.merge13
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z22 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z22, ptr %field_ptr, align 8
  %field_ptr23 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr23, align 4
  %field_ptr24 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr24, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %l25 = load i32, ptr %l, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %l25)
  %field26 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field26, align 8
  %field27 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data28 = load ptr, ptr %field27, align 8
  %s29 = load i32, ptr %s, align 4
  %l30 = load i32, ptr %l, align 4
  %bc.i64 = sext i32 %s29 to i64
  %bc.i6431 = sext i32 %l30 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 0
  %bc.src = getelementptr i8, ptr %data28, i64 %bc.i64
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i6431, i1 false)
  %l32 = load i32, ptr %l, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %l32, ptr %field.ptr, align 4
  %out33 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out33
}

define %std_core_str_core__Str @std_core_str_core__Str.copy(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %call = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 0, i32 %len)
  ret %std_core_str_core__Str %call
}

define %std_core_str_core__StrSlice @std_core_str_core__Str.as_slice(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %sl.tmp = alloca %std_core_str_core__StrSlice, align 8
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 0
  store ptr %data, ptr %field_ptr, align 8
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %field_ptr2 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 1
  store i32 %len, ptr %field_ptr2, align 4
  %sl.val = load %std_core_str_core__StrSlice, ptr %sl.tmp, align 8
  ret %std_core_str_core__StrSlice %sl.val
}

define %std_core_str_core__StrSlice @std_core_str_core__Str.subslice(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1, i32 %2) {
entry:
  %sl.tmp = alloca %std_core_str_core__StrSlice, align 8
  %l = alloca i32, align 4
  %s = alloca i32, align 4
  %n = alloca i32, align 4
  %len = alloca i32, align 4
  %start = alloca i32, align 4
  store i32 %1, ptr %start, align 4
  store i32 %2, ptr %len, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len1 = load i32, ptr %field, align 4
  store i32 %len1, ptr %n, align 4
  %start2 = load i32, ptr %start, align 4
  store i32 %start2, ptr %s, align 4
  %s3 = load i32, ptr %s, align 4
  %slt = icmp slt i32 %s3, 0
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store i32 0, ptr %s, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %s4 = load i32, ptr %s, align 4
  %n5 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %s4, %n5
  br i1 %sgt, label %if.then6, label %if.merge7

if.then6:                                         ; preds = %if.merge
  %n8 = load i32, ptr %n, align 4
  store i32 %n8, ptr %s, align 4
  br label %if.merge7

if.merge7:                                        ; preds = %if.then6, %if.merge
  %len9 = load i32, ptr %len, align 4
  store i32 %len9, ptr %l, align 4
  %l10 = load i32, ptr %l, align 4
  %slt11 = icmp slt i32 %l10, 0
  br i1 %slt11, label %if.then12, label %if.merge13

if.then12:                                        ; preds = %if.merge7
  store i32 0, ptr %l, align 4
  br label %if.merge13

if.merge13:                                       ; preds = %if.then12, %if.merge7
  %s14 = load i32, ptr %s, align 4
  %l15 = load i32, ptr %l, align 4
  %add = add nsw i32 %s14, %l15
  %n16 = load i32, ptr %n, align 4
  %sgt17 = icmp sgt i32 %add, %n16
  br i1 %sgt17, label %if.then18, label %if.merge19

if.then18:                                        ; preds = %if.merge13
  %n20 = load i32, ptr %n, align 4
  %s21 = load i32, ptr %s, align 4
  %sub = sub nsw i32 %n20, %s21
  store i32 %sub, ptr %l, align 4
  br label %if.merge19

if.merge19:                                       ; preds = %if.then18, %if.merge13
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp, align 8
  %field22 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field22, align 8
  %s23 = load i32, ptr %s, align 4
  %sext = sext i32 %s23 to i64
  %call = call ptr @__ls_ptr_at(ptr %data, i64 %sext)
  %field_ptr = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 0
  store ptr %call, ptr %field_ptr, align 8
  %l24 = load i32, ptr %l, align 4
  %field_ptr25 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 1
  store i32 %l24, ptr %field_ptr25, align 4
  %sl.val = load %std_core_str_core__StrSlice, ptr %sl.tmp, align 8
  ret %std_core_str_core__StrSlice %sl.val
}

define %"Vec(std_core_str_core__StrSlice)" @std_core_str_core__Str.split_view(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %sl.tmp33 = alloca %std_core_str_core__StrSlice, align 8
  %sl.tmp20 = alloca %std_core_str_core__StrSlice, align 8
  %p = alloca i32, align 4
  %i = alloca i32, align 4
  %start = alloca i32, align 4
  %sl.tmp4 = alloca %std_core_str_core__StrSlice, align 8
  %n = alloca i32, align 4
  %sn = alloca i32, align 4
  %sl.tmp = alloca %"Vec(std_core_str_core__StrSlice)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(std_core_str_core__StrSlice)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__StrSlice)" zeroinitializer, ptr %out, align 8
  store %"Vec(std_core_str_core__StrSlice)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(std_core_str_core__StrSlice)", ptr %sl.tmp, align 8
  store %"Vec(std_core_str_core__StrSlice)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %sn, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %n, align 4
  %sn3 = load i32, ptr %sn, align 4
  %eq = icmp eq i32 %sn3, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp4, align 8
  %field5 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field5, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp4, i32 0, i32 0
  store ptr %data, ptr %field_ptr, align 8
  %n6 = load i32, ptr %n, align 4
  %field_ptr7 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp4, i32 0, i32 1
  store i32 %n6, ptr %field_ptr7, align 4
  %sl.val8 = load %std_core_str_core__StrSlice, ptr %sl.tmp4, align 8
  call void @"Vec(std_core_str_core__StrSlice).push"(ptr %out, %std_core_str_core__StrSlice %sl.val8)
  %out9 = load %"Vec(std_core_str_core__StrSlice)", ptr %out, align 8
  ret %"Vec(std_core_str_core__StrSlice)" %out9

if.merge:                                         ; preds = %entry
  store i32 0, ptr %start, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge19, %if.merge
  br i1 true, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field10 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data11 = load ptr, ptr %field10, align 8
  %n12 = load i32, ptr %n, align 4
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %sn15 = load i32, ptr %sn, align 4
  %i16 = load i32, ptr %i, align 4
  %call = call i32 @__ls_str_find(ptr %data11, i32 %n12, ptr %data14, i32 %sn15, i32 %i16)
  store i32 %call, ptr %p, align 4
  %p17 = load i32, ptr %p, align 4
  %slt = icmp slt i32 %p17, 0
  br i1 %slt, label %if.then18, label %if.merge19

while.end:                                        ; preds = %if.then18, %while.cond
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp33, align 8
  %field34 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data35 = load ptr, ptr %field34, align 8
  %start36 = load i32, ptr %start, align 4
  %sext37 = sext i32 %start36 to i64
  %call38 = call ptr @__ls_ptr_at(ptr %data35, i64 %sext37)
  %field_ptr39 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp33, i32 0, i32 0
  store ptr %call38, ptr %field_ptr39, align 8
  %n40 = load i32, ptr %n, align 4
  %start41 = load i32, ptr %start, align 4
  %sub42 = sub nsw i32 %n40, %start41
  %field_ptr43 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp33, i32 0, i32 1
  store i32 %sub42, ptr %field_ptr43, align 4
  %sl.val44 = load %std_core_str_core__StrSlice, ptr %sl.tmp33, align 8
  call void @"Vec(std_core_str_core__StrSlice).push"(ptr %out, %std_core_str_core__StrSlice %sl.val44)
  %out45 = load %"Vec(std_core_str_core__StrSlice)", ptr %out, align 8
  ret %"Vec(std_core_str_core__StrSlice)" %out45

if.then18:                                        ; preds = %while.body
  br label %while.end

if.merge19:                                       ; preds = %while.body
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp20, align 8
  %field21 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data22 = load ptr, ptr %field21, align 8
  %start23 = load i32, ptr %start, align 4
  %sext = sext i32 %start23 to i64
  %call24 = call ptr @__ls_ptr_at(ptr %data22, i64 %sext)
  %field_ptr25 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp20, i32 0, i32 0
  store ptr %call24, ptr %field_ptr25, align 8
  %p26 = load i32, ptr %p, align 4
  %start27 = load i32, ptr %start, align 4
  %sub = sub nsw i32 %p26, %start27
  %field_ptr28 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp20, i32 0, i32 1
  store i32 %sub, ptr %field_ptr28, align 4
  %sl.val29 = load %std_core_str_core__StrSlice, ptr %sl.tmp20, align 8
  call void @"Vec(std_core_str_core__StrSlice).push"(ptr %out, %std_core_str_core__StrSlice %sl.val29)
  %p30 = load i32, ptr %p, align 4
  %sn31 = load i32, ptr %sn, align 4
  %add = add nsw i32 %p30, %sn31
  store i32 %add, ptr %i, align 4
  %i32 = load i32, ptr %i, align 4
  store i32 %i32, ptr %start, align 4
  br label %while.cond
}

define void @"Vec(std_core_str_core__StrSlice).push"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, %std_core_str_core__StrSlice %1) {
entry:
  %x = alloca %std_core_str_core__StrSlice, align 8
  store %std_core_str_core__StrSlice %1, ptr %x, align 8
  %field = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %add = add nsw i32 %len, 1
  call void @"Vec(std_core_str_core__StrSlice).reserve"(ptr %0, i32 %add)
  %x1 = load %std_core_str_core__StrSlice, ptr %x, align 8
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %pis.idx = sext i32 %len4 to i64
  %pis.ep = getelementptr %std_core_str_core__StrSlice, ptr %data, i64 %pis.idx
  store %std_core_str_core__StrSlice %x1, ptr %pis.ep, align 8
  %field5 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %add7 = add nsw i32 %len6, 1
  %field.ptr = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  store i32 %add7, ptr %field.ptr, align 4
  ret void
}

define %std_core_str_core__Str @std_core_str_core__Str.slice_str(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, %std_core_str_core__StrSlice %1) {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %s = alloca %std_core_str_core__StrSlice, align 8
  store %std_core_str_core__StrSlice %1, ptr %s, align 8
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %s, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %len)
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %field5 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %s, i32 0, i32 0
  %ptr = load ptr, ptr %field5, align 8
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %s, i32 0, i32 1
  %len7 = load i32, ptr %field6, align 4
  %bc.i64 = sext i32 %len7 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 0
  %bc.src = getelementptr i8, ptr %ptr, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %field8 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %s, i32 0, i32 1
  %len9 = load i32, ptr %field8, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %len9, ptr %field.ptr, align 4
  %out10 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out10
}

define %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %sl.tmp5 = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %eq = icmp eq i32 %cap, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field1, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %data, ptr %field_ptr, align 8
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 %len, ptr %field_ptr3, align 4
  %field_ptr4 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr4, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  ret %std_core_str_core__Str %sl.val

if.merge:                                         ; preds = %entry
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp5, align 8
  %z6 = load ptr, ptr %z, align 8
  %field_ptr7 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp5, i32 0, i32 0
  store ptr %z6, ptr %field_ptr7, align 8
  %field_ptr8 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp5, i32 0, i32 1
  store i32 0, ptr %field_ptr8, align 4
  %field_ptr9 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp5, i32 0, i32 2
  store i32 0, ptr %field_ptr9, align 4
  %sl.val10 = load %std_core_str_core__Str, ptr %sl.tmp5, align 8
  store %std_core_str_core__Str %sl.val10, ptr %out, align 8
  %field11 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len12 = load i32, ptr %field11, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %len12)
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %field15 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data16 = load ptr, ptr %field15, align 8
  %field17 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len18 = load i32, ptr %field17, align 4
  %bc.i64 = sext i32 %len18 to i64
  %bc.dst = getelementptr i8, ptr %data14, i64 0
  %bc.src = getelementptr i8, ptr %data16, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %field19 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len20 = load i32, ptr %field19, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %len20, ptr %field.ptr, align 4
  %out21 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out21
}

define void @std_core_str_core__Str.__drop(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sgt = icmp sgt i32 %cap, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field1, align 8
  call void @free(ptr %data)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  ret void
}

define i1 @"std_core_str_core__Str.$op_eq"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %i = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %ne = icmp ne i32 %len, %len2
  br i1 %ne, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %len5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data9 = load ptr, ptr %field8, align 8
  %i10 = load i32, ptr %i, align 4
  %pi.idx11 = sext i32 %i10 to i64
  %ptr.idx12 = getelementptr i8, ptr %data9, i64 %pi.idx11
  %ptr.elem13 = load i8, ptr %ptr.idx12, align 1
  %ne14 = icmp ne i8 %ptr.elem, %ptr.elem13
  br i1 %ne14, label %if.then15, label %if.merge16

for.update:                                       ; preds = %if.merge16
  %i17 = load i32, ptr %i, align 4
  %add = add nsw i32 %i17, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then15:                                        ; preds = %for.body
  ret i1 false

if.merge16:                                       ; preds = %for.body
  br label %for.update
}

define i64 @std_core_str_core__Str.hash(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %call = call i64 @__ls_fxhash_bytes(ptr %data, i32 %len)
  ret i64 %call
}

define %std_core_str_core__Str @"std_core_str_core__Str.$op_add"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.60, i32 0, i32 0 }, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %add = add nsw i32 %len, %len2
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %add)
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field3, align 8
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data5 = load ptr, ptr %field4, align 8
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len7 = load i32, ptr %field6, align 4
  %bc.i64 = sext i32 %len7 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 0
  %bc.src = getelementptr i8, ptr %data5, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data9 = load ptr, ptr %field8, align 8
  %field10 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len11 = load i32, ptr %field10, align 4
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data13 = load ptr, ptr %field12, align 8
  %field14 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len15 = load i32, ptr %field14, align 4
  %bc.i6416 = sext i32 %len11 to i64
  %bc.i6417 = sext i32 %len15 to i64
  %bc.dst18 = getelementptr i8, ptr %data9, i64 %bc.i6416
  %bc.src19 = getelementptr i8, ptr %data13, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst18, ptr align 1 %bc.src19, i64 %bc.i6417, i1 false)
  %field20 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len21 = load i32, ptr %field20, align 4
  %field22 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len23 = load i32, ptr %field22, align 4
  %add24 = add nsw i32 %len21, %len23
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %add24, ptr %field.ptr, align 4
  %out25 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out25
}

define i1 @"std_core_str_core__Str.$op_lt"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %rhs = load %std_core_str_core__Str, ptr %1, align 8
  %call = call i32 @std_core_str_core__Str.compare(ptr %0, ptr %1)
  %slt = icmp slt i32 %call, 0
  ret i1 %slt
}

define i32 @std_core_str_core__StrSlice.len(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  ret i32 %len
}

define i1 @"std_core_str_core__StrSlice.empty?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %eq = icmp eq i32 %len, 0
  ret i1 %eq
}

define i32 @"std_core_str_core__StrSlice.at!"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field, align 8
  %i1 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i1 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  ret i32 %widen.zext
}

define i32 @std_core_str_core__StrSlice.at(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %slt = icmp slt i32 %i1, 0
  br i1 %slt, label %sc.merge, label %sc.rhs

sc.rhs:                                           ; preds = %entry
  %i2 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %sge = icmp sge i32 %i2, %len
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %entry
  %sc = phi i1 [ %slt, %entry ], [ %sge, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %2 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.62, i32 33, ptr @.ls.strlit.61)
  %3 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.63)
  call void @std_sys_c__abort()
  br label %if.merge

if.merge:                                         ; preds = %if.then, %sc.merge
  %field3 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field3, align 8
  %i4 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i4 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  ret i32 %widen.zext
}

define i1 @std_core_str_core__StrSlice.eq(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %i = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %ne = icmp ne i32 %len, %len2
  br i1 %ne, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %len5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 0
  %ptr9 = load ptr, ptr %field8, align 8
  %i10 = load i32, ptr %i, align 4
  %pi.idx11 = sext i32 %i10 to i64
  %ptr.idx12 = getelementptr i8, ptr %ptr9, i64 %pi.idx11
  %ptr.elem13 = load i8, ptr %ptr.idx12, align 1
  %ne14 = icmp ne i8 %ptr.elem, %ptr.elem13
  br i1 %ne14, label %if.then15, label %if.merge16

for.update:                                       ; preds = %if.merge16
  %i17 = load i32, ptr %i, align 4
  %add = add nsw i32 %i17, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then15:                                        ; preds = %for.body
  ret i1 false

if.merge16:                                       ; preds = %for.body
  br label %for.update
}

define i1 @std_core_str_core__StrSlice.eq_str(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %i = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %ne = icmp ne i32 %len, %len2
  br i1 %ne, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %len5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data = load ptr, ptr %field8, align 8
  %i9 = load i32, ptr %i, align 4
  %pi.idx10 = sext i32 %i9 to i64
  %ptr.idx11 = getelementptr i8, ptr %data, i64 %pi.idx10
  %ptr.elem12 = load i8, ptr %ptr.idx11, align 1
  %ne13 = icmp ne i8 %ptr.elem, %ptr.elem12
  br i1 %ne13, label %if.then14, label %if.merge15

for.update:                                       ; preds = %if.merge15
  %i16 = load i32, ptr %i, align 4
  %add = add nsw i32 %i16, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then14:                                        ; preds = %for.body
  ret i1 false

if.merge15:                                       ; preds = %for.body
  br label %for.update
}

define i32 @std_core_str_core__StrSlice.compare(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %y = alloca i32, align 4
  %x = alloca i32, align 4
  %i = alloca i32, align 4
  %n = alloca i32, align 4
  %b = alloca i32, align 4
  %a = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %a, align 4
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %b, align 4
  %a3 = load i32, ptr %a, align 4
  store i32 %a3, ptr %n, align 4
  %b4 = load i32, ptr %b, align 4
  %n5 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %b4, %n5
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %b6 = load i32, ptr %b, align 4
  store i32 %b6, ptr %n, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i7 = load i32, ptr %i, align 4
  %n8 = load i32, ptr %n, align 4
  %slt9 = icmp slt i32 %i7, %n8
  br i1 %slt9, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field10 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field10, align 8
  %i11 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i11 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %x, align 4
  %field12 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 0
  %ptr13 = load ptr, ptr %field12, align 8
  %i14 = load i32, ptr %i, align 4
  %pi.idx15 = sext i32 %i14 to i64
  %ptr.idx16 = getelementptr i8, ptr %ptr13, i64 %pi.idx15
  %ptr.elem17 = load i8, ptr %ptr.idx16, align 1
  %widen.zext18 = zext i8 %ptr.elem17 to i32
  store i32 %widen.zext18, ptr %y, align 4
  %x19 = load i32, ptr %x, align 4
  %y20 = load i32, ptr %y, align 4
  %slt21 = icmp slt i32 %x19, %y20
  br i1 %slt21, label %if.then22, label %if.merge23

for.update:                                       ; preds = %if.merge27
  %i28 = load i32, ptr %i, align 4
  %add = add nsw i32 %i28, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %a29 = load i32, ptr %a, align 4
  %b30 = load i32, ptr %b, align 4
  %slt31 = icmp slt i32 %a29, %b30
  br i1 %slt31, label %if.then32, label %if.merge33

if.then22:                                        ; preds = %for.body
  ret i32 -1

if.merge23:                                       ; preds = %for.body
  %x24 = load i32, ptr %x, align 4
  %y25 = load i32, ptr %y, align 4
  %sgt = icmp sgt i32 %x24, %y25
  br i1 %sgt, label %if.then26, label %if.merge27

if.then26:                                        ; preds = %if.merge23
  ret i32 1

if.merge27:                                       ; preds = %if.merge23
  br label %for.update

if.then32:                                        ; preds = %for.end
  ret i32 -1

if.merge33:                                       ; preds = %for.end
  %a34 = load i32, ptr %a, align 4
  %b35 = load i32, ptr %b, align 4
  %sgt36 = icmp sgt i32 %a34, %b35
  br i1 %sgt36, label %if.then37, label %if.merge38

if.then37:                                        ; preds = %if.merge33
  ret i32 1

if.merge38:                                       ; preds = %if.merge33
  ret i32 0
}

define i32 @std_core_str_core__StrSlice.find(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field, align 8
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %call = call i32 @__ls_str_find(ptr %ptr, i32 %len, ptr %data, i32 %len4, i32 0)
  ret i32 %call
}

define i1 @"std_core_str_core__StrSlice.contains?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %needle = load %std_core_str_core__Str, ptr %1, align 8
  %call = call i32 @std_core_str_core__StrSlice.find(ptr %0, ptr %1)
  %sge = icmp sge i32 %call, 0
  ret i1 %sge
}

define i1 @"std_core_str_core__StrSlice.starts_with?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %j = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %m1 = load i32, ptr %m, align 4
  %field2 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %sgt = icmp sgt i32 %m1, %len3
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %j, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %j4 = load i32, ptr %j, align 4
  %m5 = load i32, ptr %m, align 4
  %slt = icmp slt i32 %j4, %m5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field6, align 8
  %j7 = load i32, ptr %j, align 4
  %pi.idx = sext i32 %j7 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data = load ptr, ptr %field8, align 8
  %j9 = load i32, ptr %j, align 4
  %pi.idx10 = sext i32 %j9 to i64
  %ptr.idx11 = getelementptr i8, ptr %data, i64 %pi.idx10
  %ptr.elem12 = load i8, ptr %ptr.idx11, align 1
  %ne = icmp ne i8 %ptr.elem, %ptr.elem12
  br i1 %ne, label %if.then13, label %if.merge14

for.update:                                       ; preds = %if.merge14
  %j15 = load i32, ptr %j, align 4
  %add = add nsw i32 %j15, 1
  store i32 %add, ptr %j, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then13:                                        ; preds = %for.body
  ret i1 false

if.merge14:                                       ; preds = %for.body
  br label %for.update
}

define i1 @"std_core_str_core__StrSlice.ends_with?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %j = alloca i32, align 4
  %off = alloca i32, align 4
  %n = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %n, align 4
  %m3 = load i32, ptr %m, align 4
  %n4 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %m3, %n4
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  %n5 = load i32, ptr %n, align 4
  %m6 = load i32, ptr %m, align 4
  %sub = sub nsw i32 %n5, %m6
  store i32 %sub, ptr %off, align 4
  store i32 0, ptr %j, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %j7 = load i32, ptr %j, align 4
  %m8 = load i32, ptr %m, align 4
  %slt = icmp slt i32 %j7, %m8
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field9 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field9, align 8
  %off10 = load i32, ptr %off, align 4
  %j11 = load i32, ptr %j, align 4
  %add = add nsw i32 %off10, %j11
  %pi.idx = sext i32 %add to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data = load ptr, ptr %field12, align 8
  %j13 = load i32, ptr %j, align 4
  %pi.idx14 = sext i32 %j13 to i64
  %ptr.idx15 = getelementptr i8, ptr %data, i64 %pi.idx14
  %ptr.elem16 = load i8, ptr %ptr.idx15, align 1
  %ne = icmp ne i8 %ptr.elem, %ptr.elem16
  br i1 %ne, label %if.then17, label %if.merge18

for.update:                                       ; preds = %if.merge18
  %j19 = load i32, ptr %j, align 4
  %add20 = add nsw i32 %j19, 1
  store i32 %add20, ptr %j, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then17:                                        ; preds = %for.body
  ret i1 false

if.merge18:                                       ; preds = %for.body
  br label %for.update
}

define %std_core_str_core__StrSlice @std_core_str_core__StrSlice.sub(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1, i32 %2) {
entry:
  %sl.tmp = alloca %std_core_str_core__StrSlice, align 8
  %l = alloca i32, align 4
  %s = alloca i32, align 4
  %n = alloca i32, align 4
  %len = alloca i32, align 4
  %start = alloca i32, align 4
  store i32 %1, ptr %start, align 4
  store i32 %2, ptr %len, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len1 = load i32, ptr %field, align 4
  store i32 %len1, ptr %n, align 4
  %start2 = load i32, ptr %start, align 4
  store i32 %start2, ptr %s, align 4
  %s3 = load i32, ptr %s, align 4
  %slt = icmp slt i32 %s3, 0
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store i32 0, ptr %s, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %s4 = load i32, ptr %s, align 4
  %n5 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %s4, %n5
  br i1 %sgt, label %if.then6, label %if.merge7

if.then6:                                         ; preds = %if.merge
  %n8 = load i32, ptr %n, align 4
  store i32 %n8, ptr %s, align 4
  br label %if.merge7

if.merge7:                                        ; preds = %if.then6, %if.merge
  %len9 = load i32, ptr %len, align 4
  store i32 %len9, ptr %l, align 4
  %l10 = load i32, ptr %l, align 4
  %slt11 = icmp slt i32 %l10, 0
  br i1 %slt11, label %if.then12, label %if.merge13

if.then12:                                        ; preds = %if.merge7
  store i32 0, ptr %l, align 4
  br label %if.merge13

if.merge13:                                       ; preds = %if.then12, %if.merge7
  %s14 = load i32, ptr %s, align 4
  %l15 = load i32, ptr %l, align 4
  %add = add nsw i32 %s14, %l15
  %n16 = load i32, ptr %n, align 4
  %sgt17 = icmp sgt i32 %add, %n16
  br i1 %sgt17, label %if.then18, label %if.merge19

if.then18:                                        ; preds = %if.merge13
  %n20 = load i32, ptr %n, align 4
  %s21 = load i32, ptr %s, align 4
  %sub = sub nsw i32 %n20, %s21
  store i32 %sub, ptr %l, align 4
  br label %if.merge19

if.merge19:                                       ; preds = %if.then18, %if.merge13
  store %std_core_str_core__StrSlice zeroinitializer, ptr %sl.tmp, align 8
  %field22 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field22, align 8
  %s23 = load i32, ptr %s, align 4
  %sext = sext i32 %s23 to i64
  %call = call ptr @__ls_ptr_at(ptr %ptr, i64 %sext)
  %field_ptr = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 0
  store ptr %call, ptr %field_ptr, align 8
  %l24 = load i32, ptr %l, align 4
  %field_ptr25 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %sl.tmp, i32 0, i32 1
  store i32 %l24, ptr %field_ptr25, align 4
  %sl.val = load %std_core_str_core__StrSlice, ptr %sl.tmp, align 8
  ret %std_core_str_core__StrSlice %sl.val
}

define %std_core_str_core__Str @std_core_str_core__StrSlice.to_str(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %len)
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %field5 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field5, align 8
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len7 = load i32, ptr %field6, align 4
  %bc.i64 = sext i32 %len7 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 0
  %bc.src = getelementptr i8, ptr %ptr, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %field8 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len9 = load i32, ptr %field8, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %len9, ptr %field.ptr, align 4
  %out10 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out10
}

define i1 @"std_core_str_core__StrSlice.$op_eq"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %i = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  %ne = icmp ne i32 %len, %len2
  br i1 %ne, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %len5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %ptr, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %1, i32 0, i32 0
  %ptr9 = load ptr, ptr %field8, align 8
  %i10 = load i32, ptr %i, align 4
  %pi.idx11 = sext i32 %i10 to i64
  %ptr.idx12 = getelementptr i8, ptr %ptr9, i64 %pi.idx11
  %ptr.elem13 = load i8, ptr %ptr.idx12, align 1
  %ne14 = icmp ne i8 %ptr.elem, %ptr.elem13
  br i1 %ne14, label %if.then15, label %if.merge16

for.update:                                       ; preds = %if.merge16
  %i17 = load i32, ptr %i, align 4
  %add = add nsw i32 %i17, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then15:                                        ; preds = %for.body
  ret i1 false

if.merge16:                                       ; preds = %for.body
  br label %for.update
}

define i64 @std_core_str_core__StrSlice.hash(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 0
  %ptr = load ptr, ptr %field, align 8
  %field1 = getelementptr inbounds %std_core_str_core__StrSlice, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %call = call i64 @__ls_fxhash_bytes(ptr %ptr, i32 %len)
  ret i64 %call
}

define i1 @"std_core_str_core__StrSlice.$op_lt"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %rhs = load %std_core_str_core__StrSlice, ptr %1, align 8
  %call = call i32 @std_core_str_core__StrSlice.compare(ptr %0, ptr %1)
  %slt = icmp slt i32 %call, 0
  ret i1 %slt
}

define %std_core_reflect_core__RawField @std_core_reflect_core__RawType.field_at(ptr nocapture nonnull readonly align 8 dereferenceable(40) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 1
  %fields = load ptr, ptr %field, align 8
  %i1 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i1 to i64
  %ptr.idx = getelementptr %std_core_reflect_core__RawField, ptr %fields, i64 %pi.idx
  %ptr.elem = load %std_core_reflect_core__RawField, ptr %ptr.idx, align 8
  ret %std_core_reflect_core__RawField %ptr.elem
}

define %std_core_reflect_core__RawMethod @std_core_reflect_core__RawType.method_at(ptr nocapture nonnull readonly align 8 dereferenceable(40) %0, i32 %1) {
entry:
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 3
  %funcs = load ptr, ptr %field, align 8
  %i1 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i1 to i64
  %ptr.idx = getelementptr %std_core_reflect_core__RawMethod, ptr %funcs, i64 %pi.idx
  %ptr.elem = load %std_core_reflect_core__RawMethod, ptr %ptr.idx, align 8
  ret %std_core_reflect_core__RawMethod %ptr.elem
}

define %std_core_reflect_core__RawType @std_core_reflect_core__RawType.__clone(ptr nocapture nonnull readonly align 8 dereferenceable(40) %0) {
entry:
  %i12 = alloca i32, align 4
  %i = alloca i32, align 4
  %var.moved = alloca i1, align 1
  %cp = alloca %std_core_reflect_core__RawType, align 8
  call void @llvm.lifetime.start.p0(i64 40, ptr %cp)
  store i1 false, ptr %var.moved, align 1
  store %std_core_reflect_core__RawType zeroinitializer, ptr %cp, align 8
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 0
  %name = load ptr, ptr %field, align 8
  %field1 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 2
  %field_count = load i32, ptr %field1, align 4
  %field2 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 4
  %func_count = load i32, ptr %field2, align 4
  %call = call %std_core_reflect_core__RawType @std_core_reflect_core__RawType.make(ptr %name, i32 %field_count, i32 %func_count)
  store %std_core_reflect_core__RawType %call, ptr %cp, align 8
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i3 = load i32, ptr %i, align 4
  %field4 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 2
  %field_count5 = load i32, ptr %field4, align 4
  %slt = icmp slt i32 %i3, %field_count5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 1
  %fields = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr %std_core_reflect_core__RawField, ptr %fields, i64 %pi.idx
  %ptr.elem = load %std_core_reflect_core__RawField, ptr %ptr.idx, align 8
  %field8 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %cp, i32 0, i32 1
  %fields9 = load ptr, ptr %field8, align 8
  %i10 = load i32, ptr %i, align 4
  %pis.idx = sext i32 %i10 to i64
  %pis.ep = getelementptr %std_core_reflect_core__RawField, ptr %fields9, i64 %pis.idx
  store %std_core_reflect_core__RawField %ptr.elem, ptr %pis.ep, align 8
  br label %for.update

for.update:                                       ; preds = %for.body
  %i11 = load i32, ptr %i, align 4
  %add = add nsw i32 %i11, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  store i32 0, ptr %i12, align 4
  br label %for.cond13

for.cond13:                                       ; preds = %for.update15, %for.end
  %i17 = load i32, ptr %i12, align 4
  %field18 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 4
  %func_count19 = load i32, ptr %field18, align 4
  %slt20 = icmp slt i32 %i17, %func_count19
  br i1 %slt20, label %for.body14, label %for.end16

for.body14:                                       ; preds = %for.cond13
  %field21 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 3
  %funcs = load ptr, ptr %field21, align 8
  %i22 = load i32, ptr %i12, align 4
  %pi.idx23 = sext i32 %i22 to i64
  %ptr.idx24 = getelementptr %std_core_reflect_core__RawMethod, ptr %funcs, i64 %pi.idx23
  %ptr.elem25 = load %std_core_reflect_core__RawMethod, ptr %ptr.idx24, align 8
  %field26 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %cp, i32 0, i32 3
  %funcs27 = load ptr, ptr %field26, align 8
  %i28 = load i32, ptr %i12, align 4
  %pis.idx29 = sext i32 %i28 to i64
  %pis.ep30 = getelementptr %std_core_reflect_core__RawMethod, ptr %funcs27, i64 %pis.idx29
  store %std_core_reflect_core__RawMethod %ptr.elem25, ptr %pis.ep30, align 8
  br label %for.update15

for.update15:                                     ; preds = %for.body14
  %i31 = load i32, ptr %i12, align 4
  %add32 = add nsw i32 %i31, 1
  store i32 %add32, ptr %i12, align 4
  br label %for.cond13

for.end16:                                        ; preds = %for.cond13
  %cp33 = load %std_core_reflect_core__RawType, ptr %cp, align 8
  ret %std_core_reflect_core__RawType %cp33
}

define void @std_core_reflect_core__RawType.__drop(ptr noalias nocapture nonnull align 8 dereferenceable(40) %0) {
entry:
  %field = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 2
  %field_count = load i32, ptr %field, align 4
  %sgt = icmp sgt i32 %field_count, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %field1 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 1
  %fields = load ptr, ptr %field1, align 8
  call void @free(ptr %fields)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %field2 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 4
  %func_count = load i32, ptr %field2, align 4
  %sgt3 = icmp sgt i32 %func_count, 0
  br i1 %sgt3, label %if.then4, label %if.merge5

if.then4:                                         ; preds = %if.merge
  %field6 = getelementptr inbounds %std_core_reflect_core__RawType, ptr %0, i32 0, i32 3
  %funcs = load ptr, ptr %field6, align 8
  call void @free(ptr %funcs)
  br label %if.merge5

if.merge5:                                        ; preds = %if.then4, %if.merge
  ret void
}

define i64 @int.hash(ptr nocapture nonnull readonly align 4 dereferenceable(4) %0) {
entry:
  %self = load i32, ptr %0, align 4
  %sext = sext i32 %self to i64
  %call = call i64 @std_core_hash__fx_mix(i64 0, i64 %sext)
  ret i64 %call
}

define i64 @i64.hash(ptr nocapture nonnull readonly align 8 dereferenceable(8) %0) {
entry:
  %self = load i64, ptr %0, align 8
  %call = call i64 @std_core_hash__fx_mix(i64 0, i64 %self)
  ret i64 %call
}

define i64 @char.hash(ptr nocapture nonnull readonly align 4 dereferenceable(4) %0) {
entry:
  %self = load i32, ptr %0, align 4
  %zext = zext i32 %self to i64
  %call = call i64 @std_core_hash__fx_mix(i64 0, i64 %zext)
  ret i64 %call
}

define i64 @bool.hash(ptr nocapture nonnull readonly align 1 dereferenceable(1) %0) {
entry:
  %w = alloca i64, align 8
  store i64 0, ptr %w, align 8
  %self = load i1, ptr %0, align 1
  br i1 %self, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  store i64 1, ptr %w, align 8
  br label %if.merge

if.merge:                                         ; preds = %if.then, %entry
  %w1 = load i64, ptr %w, align 8
  %call = call i64 @std_core_hash__fx_mix(i64 0, i64 %w1)
  ret i64 %call
}

define i32 @std_core_str_core__Str.find(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field1, align 4
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data3 = load ptr, ptr %field2, align 8
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len5 = load i32, ptr %field4, align 4
  %call = call i32 @__ls_str_find(ptr %data, i32 %len, ptr %data3, i32 %len5, i32 0)
  ret i32 %call
}

define i1 @"std_core_str_core__Str.contains?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %needle = load %std_core_str_core__Str, ptr %1, align 8
  %call = call i32 @std_core_str_core__Str.find(ptr %0, ptr %1)
  %sge = icmp sge i32 %call, 0
  ret i1 %sge
}

define i1 @"std_core_str_core__Str.starts_with?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %j = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %m1 = load i32, ptr %m, align 4
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %sgt = icmp sgt i32 %m1, %len3
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  store i32 0, ptr %j, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %j4 = load i32, ptr %j, align 4
  %m5 = load i32, ptr %m, align 4
  %slt = icmp slt i32 %j4, %m5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field6, align 8
  %j7 = load i32, ptr %j, align 4
  %pi.idx = sext i32 %j7 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data9 = load ptr, ptr %field8, align 8
  %j10 = load i32, ptr %j, align 4
  %pi.idx11 = sext i32 %j10 to i64
  %ptr.idx12 = getelementptr i8, ptr %data9, i64 %pi.idx11
  %ptr.elem13 = load i8, ptr %ptr.idx12, align 1
  %ne = icmp ne i8 %ptr.elem, %ptr.elem13
  br i1 %ne, label %if.then14, label %if.merge15

for.update:                                       ; preds = %if.merge15
  %j16 = load i32, ptr %j, align 4
  %add = add nsw i32 %j16, 1
  store i32 %add, ptr %j, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then14:                                        ; preds = %for.body
  ret i1 false

if.merge15:                                       ; preds = %for.body
  br label %for.update
}

define i1 @"std_core_str_core__Str.ends_with?"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %j = alloca i32, align 4
  %off = alloca i32, align 4
  %n = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %n, align 4
  %m3 = load i32, ptr %m, align 4
  %n4 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %m3, %n4
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i1 false

if.merge:                                         ; preds = %entry
  %n5 = load i32, ptr %n, align 4
  %m6 = load i32, ptr %m, align 4
  %sub = sub nsw i32 %n5, %m6
  store i32 %sub, ptr %off, align 4
  store i32 0, ptr %j, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %j7 = load i32, ptr %j, align 4
  %m8 = load i32, ptr %m, align 4
  %slt = icmp slt i32 %j7, %m8
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field9 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field9, align 8
  %off10 = load i32, ptr %off, align 4
  %j11 = load i32, ptr %j, align 4
  %add = add nsw i32 %off10, %j11
  %pi.idx = sext i32 %add to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data13 = load ptr, ptr %field12, align 8
  %j14 = load i32, ptr %j, align 4
  %pi.idx15 = sext i32 %j14 to i64
  %ptr.idx16 = getelementptr i8, ptr %data13, i64 %pi.idx15
  %ptr.elem17 = load i8, ptr %ptr.idx16, align 1
  %ne = icmp ne i8 %ptr.elem, %ptr.elem17
  br i1 %ne, label %if.then18, label %if.merge19

for.update:                                       ; preds = %if.merge19
  %j20 = load i32, ptr %j, align 4
  %add21 = add nsw i32 %j20, 1
  store i32 %add21, ptr %j, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i1 true

if.then18:                                        ; preds = %for.body
  ret i1 false

if.merge19:                                       ; preds = %for.body
  br label %for.update
}

define i32 @std_core_str_core__Str.rfind(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %j = alloca i32, align 4
  %hit = alloca i1, align 1
  %i = alloca i32, align 4
  %m = alloca i32, align 4
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %m, align 4
  %m3 = load i32, ptr %m, align 4
  %eq = icmp eq i32 %m3, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %n4 = load i32, ptr %n, align 4
  ret i32 %n4

if.merge:                                         ; preds = %entry
  %m5 = load i32, ptr %m, align 4
  %n6 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %m5, %n6
  br i1 %sgt, label %if.then7, label %if.merge8

if.then7:                                         ; preds = %if.merge
  ret i32 -1

if.merge8:                                        ; preds = %if.merge
  %n9 = load i32, ptr %n, align 4
  %m10 = load i32, ptr %m, align 4
  %sub = sub nsw i32 %n9, %m10
  store i32 %sub, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge8
  %i11 = load i32, ptr %i, align 4
  %sge = icmp sge i32 %i11, 0
  br i1 %sge, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  store i1 true, ptr %hit, align 1
  store i32 0, ptr %j, align 4
  br label %for.cond12

for.update:                                       ; preds = %if.merge33
  %i35 = load i32, ptr %i, align 4
  %sub36 = sub nsw i32 %i35, 1
  store i32 %sub36, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i32 -1

for.cond12:                                       ; preds = %for.update14, %for.body
  %j16 = load i32, ptr %j, align 4
  %m17 = load i32, ptr %m, align 4
  %slt = icmp slt i32 %j16, %m17
  br i1 %slt, label %for.body13, label %for.end15

for.body13:                                       ; preds = %for.cond12
  %field18 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field18, align 8
  %i19 = load i32, ptr %i, align 4
  %j20 = load i32, ptr %j, align 4
  %add = add nsw i32 %i19, %j20
  %pi.idx = sext i32 %add to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %field21 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data22 = load ptr, ptr %field21, align 8
  %j23 = load i32, ptr %j, align 4
  %pi.idx24 = sext i32 %j23 to i64
  %ptr.idx25 = getelementptr i8, ptr %data22, i64 %pi.idx24
  %ptr.elem26 = load i8, ptr %ptr.idx25, align 1
  %ne = icmp ne i8 %ptr.elem, %ptr.elem26
  br i1 %ne, label %if.then27, label %if.merge28

for.update14:                                     ; preds = %if.merge28
  %j29 = load i32, ptr %j, align 4
  %add30 = add nsw i32 %j29, 1
  store i32 %add30, ptr %j, align 4
  br label %for.cond12

for.end15:                                        ; preds = %if.then27, %for.cond12
  %hit31 = load i1, ptr %hit, align 1
  br i1 %hit31, label %if.then32, label %if.merge33

if.then27:                                        ; preds = %for.body13
  store i1 false, ptr %hit, align 1
  br label %for.end15

if.merge28:                                       ; preds = %for.body13
  br label %for.update14

if.then32:                                        ; preds = %for.end15
  %i34 = load i32, ptr %i, align 4
  ret i32 %i34

if.merge33:                                       ; preds = %for.end15
  br label %for.update
}

define i32 @std_core_str_core__Str.count(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %p = alloca i32, align 4
  %i = alloca i32, align 4
  %total = alloca i32, align 4
  %n = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %m1 = load i32, ptr %m, align 4
  %eq = icmp eq i32 %m1, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret i32 0

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  store i32 %len3, ptr %n, align 4
  store i32 0, ptr %total, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge12, %if.merge
  br i1 true, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field4 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %n5 = load i32, ptr %n, align 4
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data7 = load ptr, ptr %field6, align 8
  %m8 = load i32, ptr %m, align 4
  %i9 = load i32, ptr %i, align 4
  %call = call i32 @__ls_str_find(ptr %data, i32 %n5, ptr %data7, i32 %m8, i32 %i9)
  store i32 %call, ptr %p, align 4
  %p10 = load i32, ptr %p, align 4
  %slt = icmp slt i32 %p10, 0
  br i1 %slt, label %if.then11, label %if.merge12

while.end:                                        ; preds = %if.then11, %while.cond
  %total17 = load i32, ptr %total, align 4
  ret i32 %total17

if.then11:                                        ; preds = %while.body
  br label %while.end

if.merge12:                                       ; preds = %while.body
  %total13 = load i32, ptr %total, align 4
  %add = add nsw i32 %total13, 1
  store i32 %add, ptr %total, align 4
  %p14 = load i32, ptr %p, align 4
  %m15 = load i32, ptr %m, align 4
  %add16 = add nsw i32 %p14, %m15
  store i32 %add16, ptr %i, align 4
  br label %while.cond
}

define %std_core_str_core__Str @std_core_str_core__Str.upper(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %b = alloca i32, align 4
  %i = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %n4 = load i32, ptr %n, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %n4)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i5 = load i32, ptr %i, align 4
  %n6 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %i5, %n6
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field7 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field7, align 8
  %i8 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i8 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %b, align 4
  %b9 = load i32, ptr %b, align 4
  %sge = icmp sge i32 %b9, 97
  br i1 %sge, label %sc.rhs, label %sc.merge

for.update:                                       ; preds = %if.merge
  %i16 = load i32, ptr %i, align 4
  %add = add nsw i32 %i16, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %n17 = load i32, ptr %n, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %n17, ptr %field.ptr, align 4
  %out18 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out18

sc.rhs:                                           ; preds = %for.body
  %b10 = load i32, ptr %b, align 4
  %sle = icmp sle i32 %b10, 122
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %for.body
  %sc = phi i1 [ %sge, %for.body ], [ %sle, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %b11 = load i32, ptr %b, align 4
  %sub = sub nsw i32 %b11, 32
  store i32 %sub, ptr %b, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %sc.merge
  %b12 = load i32, ptr %b, align 4
  %trunc = trunc i32 %b12 to i8
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %i15 = load i32, ptr %i, align 4
  %pis.idx = sext i32 %i15 to i64
  %pis.ep = getelementptr i8, ptr %data14, i64 %pis.idx
  store i8 %trunc, ptr %pis.ep, align 1
  br label %for.update
}

define %std_core_str_core__Str @std_core_str_core__Str.lower(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %b = alloca i32, align 4
  %i = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %n4 = load i32, ptr %n, align 4
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %n4)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i5 = load i32, ptr %i, align 4
  %n6 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %i5, %n6
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field7 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field7, align 8
  %i8 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i8 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %b, align 4
  %b9 = load i32, ptr %b, align 4
  %sge = icmp sge i32 %b9, 65
  br i1 %sge, label %sc.rhs, label %sc.merge

for.update:                                       ; preds = %if.merge
  %i16 = load i32, ptr %i, align 4
  %add17 = add nsw i32 %i16, 1
  store i32 %add17, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %n18 = load i32, ptr %n, align 4
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %n18, ptr %field.ptr, align 4
  %out19 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out19

sc.rhs:                                           ; preds = %for.body
  %b10 = load i32, ptr %b, align 4
  %sle = icmp sle i32 %b10, 90
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %for.body
  %sc = phi i1 [ %sge, %for.body ], [ %sle, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %b11 = load i32, ptr %b, align 4
  %add = add nsw i32 %b11, 32
  store i32 %add, ptr %b, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then, %sc.merge
  %b12 = load i32, ptr %b, align 4
  %trunc = trunc i32 %b12 to i8
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %i15 = load i32, ptr %i, align 4
  %pis.idx = sext i32 %i15 to i64
  %pis.ep = getelementptr i8, ptr %data14, i64 %pis.idx
  store i8 %trunc, ptr %pis.ep, align 1
  br label %for.update
}

define %std_core_str_core__Str @std_core_str_core__Str.trim(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %b25 = alloca i32, align 4
  %e = alloca i32, align 4
  %b = alloca i32, align 4
  %s = alloca i32, align 4
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  store i32 0, ptr %s, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge, %entry
  %s1 = load i32, ptr %s, align 4
  %n2 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %s1, %n2
  br i1 %slt, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field3 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field3, align 8
  %s4 = load i32, ptr %s, align 4
  %pi.idx = sext i32 %s4 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %b, align 4
  %b5 = load i32, ptr %b, align 4
  %eq = icmp eq i32 %b5, 32
  br i1 %eq, label %sc.merge, label %sc.rhs

while.end:                                        ; preds = %if.else, %while.cond
  %n19 = load i32, ptr %n, align 4
  store i32 %n19, ptr %e, align 4
  br label %while.cond20

sc.rhs:                                           ; preds = %while.body
  %b6 = load i32, ptr %b, align 4
  %eq7 = icmp eq i32 %b6, 9
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %while.body
  %sc = phi i1 [ %eq, %while.body ], [ %eq7, %sc.rhs ]
  br i1 %sc, label %sc.merge9, label %sc.rhs8

sc.rhs8:                                          ; preds = %sc.merge
  %b10 = load i32, ptr %b, align 4
  %eq11 = icmp eq i32 %b10, 10
  br label %sc.merge9

sc.merge9:                                        ; preds = %sc.rhs8, %sc.merge
  %sc12 = phi i1 [ %sc, %sc.merge ], [ %eq11, %sc.rhs8 ]
  br i1 %sc12, label %sc.merge14, label %sc.rhs13

sc.rhs13:                                         ; preds = %sc.merge9
  %b15 = load i32, ptr %b, align 4
  %eq16 = icmp eq i32 %b15, 13
  br label %sc.merge14

sc.merge14:                                       ; preds = %sc.rhs13, %sc.merge9
  %sc17 = phi i1 [ %sc12, %sc.merge9 ], [ %eq16, %sc.rhs13 ]
  br i1 %sc17, label %if.then, label %if.else

if.then:                                          ; preds = %sc.merge14
  %s18 = load i32, ptr %s, align 4
  %add = add nsw i32 %s18, 1
  store i32 %add, ptr %s, align 4
  br label %if.merge

if.merge:                                         ; preds = %if.then
  br label %while.cond

if.else:                                          ; preds = %sc.merge14
  br label %while.end

while.cond20:                                     ; preds = %if.merge51, %while.end
  %e23 = load i32, ptr %e, align 4
  %s24 = load i32, ptr %s, align 4
  %sgt = icmp sgt i32 %e23, %s24
  br i1 %sgt, label %while.body21, label %while.end22

while.body21:                                     ; preds = %while.cond20
  %field26 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data27 = load ptr, ptr %field26, align 8
  %e28 = load i32, ptr %e, align 4
  %sub = sub nsw i32 %e28, 1
  %pi.idx29 = sext i32 %sub to i64
  %ptr.idx30 = getelementptr i8, ptr %data27, i64 %pi.idx29
  %ptr.elem31 = load i8, ptr %ptr.idx30, align 1
  %widen.zext32 = zext i8 %ptr.elem31 to i32
  store i32 %widen.zext32, ptr %b25, align 4
  %b33 = load i32, ptr %b25, align 4
  %eq34 = icmp eq i32 %b33, 32
  br i1 %eq34, label %sc.merge36, label %sc.rhs35

while.end22:                                      ; preds = %if.else52, %while.cond20
  %s55 = load i32, ptr %s, align 4
  %e56 = load i32, ptr %e, align 4
  %s57 = load i32, ptr %s, align 4
  %sub58 = sub nsw i32 %e56, %s57
  %call = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 %s55, i32 %sub58)
  ret %std_core_str_core__Str %call

sc.rhs35:                                         ; preds = %while.body21
  %b37 = load i32, ptr %b25, align 4
  %eq38 = icmp eq i32 %b37, 9
  br label %sc.merge36

sc.merge36:                                       ; preds = %sc.rhs35, %while.body21
  %sc39 = phi i1 [ %eq34, %while.body21 ], [ %eq38, %sc.rhs35 ]
  br i1 %sc39, label %sc.merge41, label %sc.rhs40

sc.rhs40:                                         ; preds = %sc.merge36
  %b42 = load i32, ptr %b25, align 4
  %eq43 = icmp eq i32 %b42, 10
  br label %sc.merge41

sc.merge41:                                       ; preds = %sc.rhs40, %sc.merge36
  %sc44 = phi i1 [ %sc39, %sc.merge36 ], [ %eq43, %sc.rhs40 ]
  br i1 %sc44, label %sc.merge46, label %sc.rhs45

sc.rhs45:                                         ; preds = %sc.merge41
  %b47 = load i32, ptr %b25, align 4
  %eq48 = icmp eq i32 %b47, 13
  br label %sc.merge46

sc.merge46:                                       ; preds = %sc.rhs45, %sc.merge41
  %sc49 = phi i1 [ %sc44, %sc.merge41 ], [ %eq48, %sc.rhs45 ]
  br i1 %sc49, label %if.then50, label %if.else52

if.then50:                                        ; preds = %sc.merge46
  %e53 = load i32, ptr %e, align 4
  %sub54 = sub nsw i32 %e53, 1
  store i32 %sub54, ptr %e, align 4
  br label %if.merge51

if.merge51:                                       ; preds = %if.then50
  br label %while.cond20

if.else52:                                        ; preds = %sc.merge46
  br label %while.end22
}

define %std_core_str_core__Str @std_core_str_core__Str.concat(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %b = alloca i32, align 4
  %a = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %a, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %b, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z3 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z3, ptr %field_ptr, align 8
  %field_ptr4 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr4, align 4
  %field_ptr5 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr5, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %a6 = load i32, ptr %a, align 4
  %b7 = load i32, ptr %b, align 4
  %add = add nsw i32 %a6, %b7
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %add)
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field8, align 8
  %field9 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data10 = load ptr, ptr %field9, align 8
  %a11 = load i32, ptr %a, align 4
  %bc.i64 = sext i32 %a11 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 0
  %bc.src = getelementptr i8, ptr %data10, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i64, i1 false)
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data13 = load ptr, ptr %field12, align 8
  %a14 = load i32, ptr %a, align 4
  %field15 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data16 = load ptr, ptr %field15, align 8
  %b17 = load i32, ptr %b, align 4
  %bc.i6418 = sext i32 %a14 to i64
  %bc.i6419 = sext i32 %b17 to i64
  %bc.dst20 = getelementptr i8, ptr %data13, i64 %bc.i6418
  %bc.src21 = getelementptr i8, ptr %data16, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst20, ptr align 1 %bc.src21, i64 %bc.i6419, i1 false)
  %a22 = load i32, ptr %a, align 4
  %b23 = load i32, ptr %b, align 4
  %add24 = add nsw i32 %a22, %b23
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %add24, ptr %field.ptr, align 4
  %out25 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out25
}

define %std_core_str_core__Str @std_core_str_core__Str.repeat(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %t = alloca i32, align 4
  %n = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %times = alloca i32, align 4
  store i32 %1, ptr %times, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %times4 = load i32, ptr %times, align 4
  %sle = icmp sle i32 %times4, 0
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %out5 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out5

if.merge:                                         ; preds = %entry
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %n6 = load i32, ptr %n, align 4
  %times7 = load i32, ptr %times, align 4
  %mul = mul nsw i32 %n6, %times7
  call void @std_core_str_core__Str.reserve(ptr %out, i32 %mul)
  store i32 0, ptr %t, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %if.merge
  %t8 = load i32, ptr %t, align 4
  %times9 = load i32, ptr %times, align 4
  %slt = icmp slt i32 %t8, %times9
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field10 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data = load ptr, ptr %field10, align 8
  %t11 = load i32, ptr %t, align 4
  %n12 = load i32, ptr %n, align 4
  %mul13 = mul nsw i32 %t11, %n12
  %field14 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data15 = load ptr, ptr %field14, align 8
  %n16 = load i32, ptr %n, align 4
  %bc.i64 = sext i32 %mul13 to i64
  %bc.i6417 = sext i32 %n16 to i64
  %bc.dst = getelementptr i8, ptr %data, i64 %bc.i64
  %bc.src = getelementptr i8, ptr %data15, i64 0
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %bc.dst, ptr align 1 %bc.src, i64 %bc.i6417, i1 false)
  br label %for.update

for.update:                                       ; preds = %for.body
  %t18 = load i32, ptr %t, align 4
  %add = add nsw i32 %t18, 1
  store i32 %add, ptr %t, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %n19 = load i32, ptr %n, align 4
  %times20 = load i32, ptr %times, align 4
  %mul21 = mul nsw i32 %n19, %times20
  %field.ptr = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 1
  store i32 %mul21, ptr %field.ptr, align 4
  %out22 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out22
}

define %std_core_str_core__Str @std_core_str_core__Str.replace(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1, ptr nocapture nonnull readonly align 8 dereferenceable(16) %2) {
entry:
  %j = alloca i32, align 4
  %k66 = alloca i32, align 4
  %k = alloca i32, align 4
  %p41 = alloca i32, align 4
  %i34 = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved30 = alloca i1, align 1
  %out29 = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %p = alloca i32, align 4
  %i = alloca i32, align 4
  %rb = alloca i32, align 4
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %n = alloca i32, align 4
  %m = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %m, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %n, align 4
  %m3 = load i32, ptr %m, align 4
  %eq = icmp eq i32 %m3, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %call = call %std_core_str_core__Str @std_core_str_core__Str.copy(ptr %0)
  ret %std_core_str_core__Str %call

if.merge:                                         ; preds = %entry
  %m4 = load i32, ptr %m, align 4
  %eq5 = icmp eq i32 %m4, 1
  br i1 %eq5, label %sc.rhs, label %sc.merge

sc.rhs:                                           ; preds = %if.merge
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %2, i32 0, i32 1
  %len7 = load i32, ptr %field6, align 4
  %eq8 = icmp eq i32 %len7, 1
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %if.merge
  %sc = phi i1 [ %eq5, %if.merge ], [ %eq8, %sc.rhs ]
  br i1 %sc, label %if.then9, label %if.merge10

if.then9:                                         ; preds = %sc.merge
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  %call11 = call %std_core_str_core__Str @std_core_str_core__Str.copy(ptr %0)
  store %std_core_str_core__Str %call11, ptr %out, align 8
  %field12 = getelementptr inbounds %std_core_str_core__Str, ptr %2, i32 0, i32 0
  %data = load ptr, ptr %field12, align 8
  %ptr.idx = getelementptr i8, ptr %data, i64 0
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %rb, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond

if.merge10:                                       ; preds = %sc.merge
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out29)
  store i1 false, ptr %var.moved30, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out29, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z31 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z31, ptr %field_ptr, align 8
  %field_ptr32 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr32, align 4
  %field_ptr33 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr33, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out29, align 8
  store i32 0, ptr %i34, align 4
  br label %while.cond35

while.cond:                                       ; preds = %if.merge22, %if.then9
  br i1 true, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %n15 = load i32, ptr %n, align 4
  %field16 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data17 = load ptr, ptr %field16, align 8
  %i18 = load i32, ptr %i, align 4
  %call19 = call i32 @__ls_str_find(ptr %data14, i32 %n15, ptr %data17, i32 1, i32 %i18)
  store i32 %call19, ptr %p, align 4
  %p20 = load i32, ptr %p, align 4
  %slt = icmp slt i32 %p20, 0
  br i1 %slt, label %if.then21, label %if.merge22

while.end:                                        ; preds = %if.then21, %while.cond
  %out28 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out28

if.then21:                                        ; preds = %while.body
  br label %while.end

if.merge22:                                       ; preds = %while.body
  %rb23 = load i32, ptr %rb, align 4
  %trunc = trunc i32 %rb23 to i8
  %field24 = getelementptr inbounds %std_core_str_core__Str, ptr %out, i32 0, i32 0
  %data25 = load ptr, ptr %field24, align 8
  %p26 = load i32, ptr %p, align 4
  %pis.idx = sext i32 %p26 to i64
  %pis.ep = getelementptr i8, ptr %data25, i64 %pis.idx
  store i8 %trunc, ptr %pis.ep, align 1
  %p27 = load i32, ptr %p, align 4
  %add = add nsw i32 %p27, 1
  store i32 %add, ptr %i, align 4
  br label %while.cond

while.cond35:                                     ; preds = %for.end87, %if.merge10
  %i38 = load i32, ptr %i34, align 4
  %n39 = load i32, ptr %n, align 4
  %slt40 = icmp slt i32 %i38, %n39
  br i1 %slt40, label %while.body36, label %while.end37

while.body36:                                     ; preds = %while.cond35
  %field42 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data43 = load ptr, ptr %field42, align 8
  %n44 = load i32, ptr %n, align 4
  %field45 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data46 = load ptr, ptr %field45, align 8
  %m47 = load i32, ptr %m, align 4
  %i48 = load i32, ptr %i34, align 4
  %call49 = call i32 @__ls_str_find(ptr %data43, i32 %n44, ptr %data46, i32 %m47, i32 %i48)
  store i32 %call49, ptr %p41, align 4
  %p50 = load i32, ptr %p41, align 4
  %slt51 = icmp slt i32 %p50, 0
  br i1 %slt51, label %if.then52, label %if.merge53

while.end37:                                      ; preds = %for.end, %while.cond35
  %out104 = load %std_core_str_core__Str, ptr %out29, align 8
  ret %std_core_str_core__Str %out104

if.then52:                                        ; preds = %while.body36
  %i54 = load i32, ptr %i34, align 4
  store i32 %i54, ptr %k, align 4
  br label %for.cond

if.merge53:                                       ; preds = %while.body36
  %i67 = load i32, ptr %i34, align 4
  store i32 %i67, ptr %k66, align 4
  br label %for.cond68

for.cond:                                         ; preds = %for.update, %if.then52
  %k55 = load i32, ptr %k, align 4
  %n56 = load i32, ptr %n, align 4
  %slt57 = icmp slt i32 %k55, %n56
  br i1 %slt57, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field58 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data59 = load ptr, ptr %field58, align 8
  %k60 = load i32, ptr %k, align 4
  %pi.idx = sext i32 %k60 to i64
  %ptr.idx61 = getelementptr i8, ptr %data59, i64 %pi.idx
  %ptr.elem62 = load i8, ptr %ptr.idx61, align 1
  %widen.zext63 = zext i8 %ptr.elem62 to i32
  call void @std_core_str_core__Str.push_byte(ptr %out29, i32 %widen.zext63)
  br label %for.update

for.update:                                       ; preds = %for.body
  %k64 = load i32, ptr %k, align 4
  %add65 = add nsw i32 %k64, 1
  store i32 %add65, ptr %k, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  br label %while.end37

for.cond68:                                       ; preds = %for.update70, %if.merge53
  %k72 = load i32, ptr %k66, align 4
  %p73 = load i32, ptr %p41, align 4
  %slt74 = icmp slt i32 %k72, %p73
  br i1 %slt74, label %for.body69, label %for.end71

for.body69:                                       ; preds = %for.cond68
  %field75 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data76 = load ptr, ptr %field75, align 8
  %k77 = load i32, ptr %k66, align 4
  %pi.idx78 = sext i32 %k77 to i64
  %ptr.idx79 = getelementptr i8, ptr %data76, i64 %pi.idx78
  %ptr.elem80 = load i8, ptr %ptr.idx79, align 1
  %widen.zext81 = zext i8 %ptr.elem80 to i32
  call void @std_core_str_core__Str.push_byte(ptr %out29, i32 %widen.zext81)
  br label %for.update70

for.update70:                                     ; preds = %for.body69
  %k82 = load i32, ptr %k66, align 4
  %add83 = add nsw i32 %k82, 1
  store i32 %add83, ptr %k66, align 4
  br label %for.cond68

for.end71:                                        ; preds = %for.cond68
  store i32 0, ptr %j, align 4
  br label %for.cond84

for.cond84:                                       ; preds = %for.update86, %for.end71
  %j88 = load i32, ptr %j, align 4
  %field89 = getelementptr inbounds %std_core_str_core__Str, ptr %2, i32 0, i32 1
  %len90 = load i32, ptr %field89, align 4
  %slt91 = icmp slt i32 %j88, %len90
  br i1 %slt91, label %for.body85, label %for.end87

for.body85:                                       ; preds = %for.cond84
  %field92 = getelementptr inbounds %std_core_str_core__Str, ptr %2, i32 0, i32 0
  %data93 = load ptr, ptr %field92, align 8
  %j94 = load i32, ptr %j, align 4
  %pi.idx95 = sext i32 %j94 to i64
  %ptr.idx96 = getelementptr i8, ptr %data93, i64 %pi.idx95
  %ptr.elem97 = load i8, ptr %ptr.idx96, align 1
  %widen.zext98 = zext i8 %ptr.elem97 to i32
  call void @std_core_str_core__Str.push_byte(ptr %out29, i32 %widen.zext98)
  br label %for.update86

for.update86:                                     ; preds = %for.body85
  %j99 = load i32, ptr %j, align 4
  %add100 = add nsw i32 %j99, 1
  store i32 %add100, ptr %j, align 4
  br label %for.cond84

for.end87:                                        ; preds = %for.cond84
  %p101 = load i32, ptr %p41, align 4
  %m102 = load i32, ptr %m, align 4
  %add103 = add nsw i32 %p101, %m102
  store i32 %add103, ptr %i34, align 4
  br label %while.cond35
}

define %std_core_str_core__Str @std_core_str_core__Str.pad_left(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1, i32 %2) {
entry:
  %i12 = alloca i32, align 4
  %i = alloca i32, align 4
  %pad = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %n = alloca i32, align 4
  %fill = alloca i32, align 4
  %width = alloca i32, align 4
  store i32 %1, ptr %width, align 4
  store i32 %2, ptr %fill, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  %width4 = load i32, ptr %width, align 4
  %n5 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %width4, %n5
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %width6 = load i32, ptr %width, align 4
  %n7 = load i32, ptr %n, align 4
  %sub = sub nsw i32 %width6, %n7
  store i32 %sub, ptr %pad, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

if.merge:                                         ; preds = %for.end, %entry
  store i32 0, ptr %i12, align 4
  br label %for.cond13

for.cond:                                         ; preds = %for.update, %if.then
  %i8 = load i32, ptr %i, align 4
  %pad9 = load i32, ptr %pad, align 4
  %slt = icmp slt i32 %i8, %pad9
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %fill10 = load i32, ptr %fill, align 4
  call void @std_core_str_core__Str.push_byte(ptr %out, i32 %fill10)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i11 = load i32, ptr %i, align 4
  %add = add nsw i32 %i11, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  br label %if.merge

for.cond13:                                       ; preds = %for.update15, %if.merge
  %i17 = load i32, ptr %i12, align 4
  %n18 = load i32, ptr %n, align 4
  %slt19 = icmp slt i32 %i17, %n18
  br i1 %slt19, label %for.body14, label %for.end16

for.body14:                                       ; preds = %for.cond13
  %field20 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field20, align 8
  %i21 = load i32, ptr %i12, align 4
  %pi.idx = sext i32 %i21 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  call void @std_core_str_core__Str.push_byte(ptr %out, i32 %widen.zext)
  br label %for.update15

for.update15:                                     ; preds = %for.body14
  %i22 = load i32, ptr %i12, align 4
  %add23 = add nsw i32 %i22, 1
  store i32 %add23, ptr %i12, align 4
  br label %for.cond13

for.end16:                                        ; preds = %for.cond13
  %out24 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out24
}

define %std_core_str_core__Str @std_core_str_core__Str.pad_right(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1, i32 %2) {
entry:
  %i13 = alloca i32, align 4
  %pad = alloca i32, align 4
  %i = alloca i32, align 4
  %sl.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %out = alloca %std_core_str_core__Str, align 8
  %z = alloca ptr, align 8
  %n = alloca i32, align 4
  %fill = alloca i32, align 4
  %width = alloca i32, align 4
  store i32 %1, ptr %width, align 4
  store i32 %2, ptr %fill, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  store ptr null, ptr %z, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %out, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %sl.tmp, align 8
  %z1 = load ptr, ptr %z, align 8
  %field_ptr = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 0
  store ptr %z1, ptr %field_ptr, align 8
  %field_ptr2 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 1
  store i32 0, ptr %field_ptr2, align 4
  %field_ptr3 = getelementptr inbounds %std_core_str_core__Str, ptr %sl.tmp, i32 0, i32 2
  store i32 0, ptr %field_ptr3, align 4
  %sl.val = load %std_core_str_core__Str, ptr %sl.tmp, align 8
  store %std_core_str_core__Str %sl.val, ptr %out, align 8
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i4 = load i32, ptr %i, align 4
  %n5 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %i4, %n5
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field6, align 8
  %i7 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i7 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  call void @std_core_str_core__Str.push_byte(ptr %out, i32 %widen.zext)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i8 = load i32, ptr %i, align 4
  %add = add nsw i32 %i8, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %width9 = load i32, ptr %width, align 4
  %n10 = load i32, ptr %n, align 4
  %sgt = icmp sgt i32 %width9, %n10
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %for.end
  %width11 = load i32, ptr %width, align 4
  %n12 = load i32, ptr %n, align 4
  %sub = sub nsw i32 %width11, %n12
  store i32 %sub, ptr %pad, align 4
  store i32 0, ptr %i13, align 4
  br label %for.cond14

if.merge:                                         ; preds = %for.end17, %for.end
  %out24 = load %std_core_str_core__Str, ptr %out, align 8
  ret %std_core_str_core__Str %out24

for.cond14:                                       ; preds = %for.update16, %if.then
  %i18 = load i32, ptr %i13, align 4
  %pad19 = load i32, ptr %pad, align 4
  %slt20 = icmp slt i32 %i18, %pad19
  br i1 %slt20, label %for.body15, label %for.end17

for.body15:                                       ; preds = %for.cond14
  %fill21 = load i32, ptr %fill, align 4
  call void @std_core_str_core__Str.push_byte(ptr %out, i32 %fill21)
  br label %for.update16

for.update16:                                     ; preds = %for.body15
  %i22 = load i32, ptr %i13, align 4
  %add23 = add nsw i32 %i22, 1
  store i32 %add23, ptr %i13, align 4
  br label %for.cond14

for.end17:                                        ; preds = %for.cond14
  br label %if.merge
}

define %"Vec(int)" @std_core_str_core__Str.bytes(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  %sl.tmp = alloca %"Vec(int)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(int)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(int)" zeroinitializer, ptr %out, align 8
  store %"Vec(int)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(int)", ptr %sl.tmp, align 8
  store %"Vec(int)" %sl.val, ptr %out, align 8
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %slt = icmp slt i32 %i1, %len
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %i3 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i3 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  call void @"Vec(int).push"(ptr %out, i32 %widen.zext)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i4 = load i32, ptr %i, align 4
  %add = add nsw i32 %i4, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %out5 = load %"Vec(int)", ptr %out, align 8
  ret %"Vec(int)" %out5
}

define void @"Vec(int).push"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %x = alloca i32, align 4
  store i32 %1, ptr %x, align 4
  %field = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %add = add nsw i32 %len, 1
  call void @"Vec(int).reserve"(ptr %0, i32 %add)
  %x1 = load i32, ptr %x, align 4
  %field2 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %pis.idx = sext i32 %len4 to i64
  %pis.ep = getelementptr i32, ptr %data, i64 %pis.idx
  store i32 %x1, ptr %pis.ep, align 4
  %field5 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %add7 = add nsw i32 %len6, 1
  %field.ptr = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  store i32 %add7, ptr %field.ptr, align 4
  ret void
}

define %"Vec(std_core_str_core__Str)" @std_core_str_core__Str.split(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, ptr nocapture nonnull readonly align 8 dereferenceable(16) %1) {
entry:
  %p = alloca i32, align 4
  %i = alloca i32, align 4
  %start = alloca i32, align 4
  %n = alloca i32, align 4
  %sn = alloca i32, align 4
  %sl.tmp = alloca %"Vec(std_core_str_core__Str)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(std_core_str_core__Str)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %out, align 8
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(std_core_str_core__Str)", ptr %sl.tmp, align 8
  store %"Vec(std_core_str_core__Str)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %sn, align 4
  %field1 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len2 = load i32, ptr %field1, align 4
  store i32 %len2, ptr %n, align 4
  %sn3 = load i32, ptr %sn, align 4
  %eq = icmp eq i32 %sn3, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %n4 = load i32, ptr %n, align 4
  %call = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 0, i32 %n4)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %call)
  %out5 = load %"Vec(std_core_str_core__Str)", ptr %out, align 8
  ret %"Vec(std_core_str_core__Str)" %out5

if.merge:                                         ; preds = %entry
  store i32 0, ptr %start, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge15, %if.merge
  br i1 true, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field6 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field6, align 8
  %n7 = load i32, ptr %n, align 4
  %field8 = getelementptr inbounds %std_core_str_core__Str, ptr %1, i32 0, i32 0
  %data9 = load ptr, ptr %field8, align 8
  %sn10 = load i32, ptr %sn, align 4
  %i11 = load i32, ptr %i, align 4
  %call12 = call i32 @__ls_str_find(ptr %data, i32 %n7, ptr %data9, i32 %sn10, i32 %i11)
  store i32 %call12, ptr %p, align 4
  %p13 = load i32, ptr %p, align 4
  %slt = icmp slt i32 %p13, 0
  br i1 %slt, label %if.then14, label %if.merge15

while.end:                                        ; preds = %if.then14, %while.cond
  %start23 = load i32, ptr %start, align 4
  %n24 = load i32, ptr %n, align 4
  %start25 = load i32, ptr %start, align 4
  %sub26 = sub nsw i32 %n24, %start25
  %call27 = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 %start23, i32 %sub26)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %call27)
  %out28 = load %"Vec(std_core_str_core__Str)", ptr %out, align 8
  ret %"Vec(std_core_str_core__Str)" %out28

if.then14:                                        ; preds = %while.body
  br label %while.end

if.merge15:                                       ; preds = %while.body
  %start16 = load i32, ptr %start, align 4
  %p17 = load i32, ptr %p, align 4
  %start18 = load i32, ptr %start, align 4
  %sub = sub nsw i32 %p17, %start18
  %call19 = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 %start16, i32 %sub)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %call19)
  %p20 = load i32, ptr %p, align 4
  %sn21 = load i32, ptr %sn, align 4
  %add = add nsw i32 %p20, %sn21
  store i32 %add, ptr %i, align 4
  %i22 = load i32, ptr %i, align 4
  store i32 %i22, ptr %start, align 4
  br label %while.cond
}

define void @"Vec(std_core_str_core__Str).push"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, %std_core_str_core__Str %1) {
entry:
  %param.moved = alloca i1, align 1
  %x = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str %1, ptr %x, align 8
  store i1 false, ptr %param.moved, align 1
  %field = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %add = add nsw i32 %len, 1
  call void @"Vec(std_core_str_core__Str).reserve"(ptr %0, i32 %add)
  %x1 = load %std_core_str_core__Str, ptr %x, align 8
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %pis.idx = sext i32 %len4 to i64
  %pis.ep = getelementptr %std_core_str_core__Str, ptr %data, i64 %pis.idx
  store %std_core_str_core__Str %x1, ptr %pis.ep, align 8
  store i1 true, ptr %param.moved, align 1
  %field5 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %add7 = add nsw i32 %len6, 1
  %field.ptr = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  store i32 %add7, ptr %field.ptr, align 4
  br label %cleanup

cleanup:                                          ; preds = %entry
  %drop.flag = load i1, ptr %param.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  ret void

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %x)
  br label %drop.skip0
}

define %"Vec(std_core_str_core__Str)" @std_core_str_core__Str.lines(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %prev = alloca i32, align 4
  %cut = alloca i32, align 4
  %ch = alloca i32, align 4
  %i = alloca i32, align 4
  %start = alloca i32, align 4
  %n = alloca i32, align 4
  %sl.tmp = alloca %"Vec(std_core_str_core__Str)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(std_core_str_core__Str)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %out, align 8
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(std_core_str_core__Str)", ptr %sl.tmp, align 8
  store %"Vec(std_core_str_core__Str)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %eq = icmp eq i32 %n1, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %out2 = load %"Vec(std_core_str_core__Str)", ptr %out, align 8
  ret %"Vec(std_core_str_core__Str)" %out2

if.merge:                                         ; preds = %entry
  store i32 0, ptr %start, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge10, %if.merge
  %i3 = load i32, ptr %i, align 4
  %n4 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %i3, %n4
  br i1 %slt, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field5 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field5, align 8
  %i6 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i6 to i64
  %ptr.idx = getelementptr i8, ptr %data, i64 %pi.idx
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %ch, align 4
  %ch7 = load i32, ptr %ch, align 4
  %eq8 = icmp eq i32 %ch7, 10
  br i1 %eq8, label %if.then9, label %if.merge10

while.end:                                        ; preds = %while.cond
  %start36 = load i32, ptr %start, align 4
  %n37 = load i32, ptr %n, align 4
  %slt38 = icmp slt i32 %start36, %n37
  br i1 %slt38, label %if.then39, label %if.merge40

if.then9:                                         ; preds = %while.body
  %i11 = load i32, ptr %i, align 4
  store i32 %i11, ptr %cut, align 4
  %cut12 = load i32, ptr %cut, align 4
  %start13 = load i32, ptr %start, align 4
  %sgt = icmp sgt i32 %cut12, %start13
  br i1 %sgt, label %if.then14, label %if.merge15

if.merge10:                                       ; preds = %if.merge15, %while.body
  %i34 = load i32, ptr %i, align 4
  %add35 = add nsw i32 %i34, 1
  store i32 %add35, ptr %i, align 4
  br label %while.cond

if.then14:                                        ; preds = %if.then9
  %field16 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data17 = load ptr, ptr %field16, align 8
  %cut18 = load i32, ptr %cut, align 4
  %sub = sub nsw i32 %cut18, 1
  %pi.idx19 = sext i32 %sub to i64
  %ptr.idx20 = getelementptr i8, ptr %data17, i64 %pi.idx19
  %ptr.elem21 = load i8, ptr %ptr.idx20, align 1
  %widen.zext22 = zext i8 %ptr.elem21 to i32
  store i32 %widen.zext22, ptr %prev, align 4
  %prev23 = load i32, ptr %prev, align 4
  %eq24 = icmp eq i32 %prev23, 13
  br i1 %eq24, label %if.then25, label %if.merge26

if.merge15:                                       ; preds = %if.merge26, %if.then9
  %start29 = load i32, ptr %start, align 4
  %cut30 = load i32, ptr %cut, align 4
  %start31 = load i32, ptr %start, align 4
  %sub32 = sub nsw i32 %cut30, %start31
  %call = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 %start29, i32 %sub32)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %call)
  %i33 = load i32, ptr %i, align 4
  %add = add nsw i32 %i33, 1
  store i32 %add, ptr %start, align 4
  br label %if.merge10

if.then25:                                        ; preds = %if.then14
  %cut27 = load i32, ptr %cut, align 4
  %sub28 = sub nsw i32 %cut27, 1
  store i32 %sub28, ptr %cut, align 4
  br label %if.merge26

if.merge26:                                       ; preds = %if.then25, %if.then14
  br label %if.merge15

if.then39:                                        ; preds = %while.end
  %start41 = load i32, ptr %start, align 4
  %n42 = load i32, ptr %n, align 4
  %start43 = load i32, ptr %start, align 4
  %sub44 = sub nsw i32 %n42, %start43
  %call45 = call %std_core_str_core__Str @std_core_str_core__Str.substr(ptr %0, i32 %start41, i32 %sub44)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %call45)
  br label %if.merge40

if.merge40:                                       ; preds = %if.then39, %while.end
  %out46 = load %"Vec(std_core_str_core__Str)", ptr %out, align 8
  ret %"Vec(std_core_str_core__Str)" %out46
}

define void @"Result(int,std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.end
}

define %"Result(int,std_core_str_core__Str)" @std_core_str_core__Str.to_int(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %enum.ctor172 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %enum.ctor155 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %d = alloca i32, align 4
  %val = alloca i32, align 4
  %enum.ctor128 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %enum.ctor113 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %dig = alloca i32, align 4
  %hd = alloca i32, align 4
  %hv = alloca i32, align 4
  %enum.ctor62 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %enum.ctor15 = alloca %"Result(int,std_core_str_core__Str)", align 8
  %first = alloca i32, align 4
  %neg = alloca i1, align 1
  %i = alloca i32, align 4
  %enum.ctor = alloca %"Result(int,std_core_str_core__Str)", align 8
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %eq = icmp eq i32 %n1, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 1, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.64, i32 12, i32 0 }, ptr %field.p, align 8
  %enum.val = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  store i1 false, ptr %neg, align 1
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %ptr.idx = getelementptr i8, ptr %data, i64 0
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %first, align 4
  %first3 = load i32, ptr %first, align 4
  %eq4 = icmp eq i32 %first3, 45
  br i1 %eq4, label %if.then5, label %if.else

if.then5:                                         ; preds = %if.merge
  store i1 true, ptr %neg, align 1
  store i32 1, ptr %i, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.merge10, %if.then5
  %i11 = load i32, ptr %i, align 4
  %n12 = load i32, ptr %n, align 4
  %sge = icmp sge i32 %i11, %n12
  br i1 %sge, label %if.then13, label %if.merge14

if.else:                                          ; preds = %if.merge
  %first7 = load i32, ptr %first, align 4
  %eq8 = icmp eq i32 %first7, 43
  br i1 %eq8, label %if.then9, label %if.merge10

if.then9:                                         ; preds = %if.else
  store i32 1, ptr %i, align 4
  br label %if.merge10

if.merge10:                                       ; preds = %if.then9, %if.else
  br label %if.merge6

if.then13:                                        ; preds = %if.merge6
  %2 = call ptr @memset(ptr %enum.ctor15, i32 0, i64 24)
  %disc.p16 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor15, i32 0, i32 0
  store i8 1, ptr %disc.p16, align 1
  %payload.p17 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor15, i32 0, i32 1
  %field.p18 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p17, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.65, i32 9, i32 0 }, ptr %field.p18, align 8
  %enum.val19 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor15, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val19

if.merge14:                                       ; preds = %if.merge6
  %i20 = load i32, ptr %i, align 4
  %add = add nsw i32 %i20, 1
  %n21 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %add, %n21
  br i1 %slt, label %sc.rhs, label %sc.merge

sc.rhs:                                           ; preds = %if.merge14
  %field22 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data23 = load ptr, ptr %field22, align 8
  %i24 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i24 to i64
  %ptr.idx25 = getelementptr i8, ptr %data23, i64 %pi.idx
  %ptr.elem26 = load i8, ptr %ptr.idx25, align 1
  %widen.zext27 = zext i8 %ptr.elem26 to i32
  %eq28 = icmp eq i32 %widen.zext27, 48
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %if.merge14
  %sc = phi i1 [ %slt, %if.merge14 ], [ %eq28, %sc.rhs ]
  br i1 %sc, label %sc.rhs29, label %sc.merge30

sc.rhs29:                                         ; preds = %sc.merge
  %field31 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data32 = load ptr, ptr %field31, align 8
  %i33 = load i32, ptr %i, align 4
  %add34 = add nsw i32 %i33, 1
  %pi.idx35 = sext i32 %add34 to i64
  %ptr.idx36 = getelementptr i8, ptr %data32, i64 %pi.idx35
  %ptr.elem37 = load i8, ptr %ptr.idx36, align 1
  %widen.zext38 = zext i8 %ptr.elem37 to i32
  %eq39 = icmp eq i32 %widen.zext38, 120
  br i1 %eq39, label %sc.merge41, label %sc.rhs40

sc.merge30:                                       ; preds = %sc.merge41, %sc.merge
  %sc52 = phi i1 [ %sc, %sc.merge ], [ %sc51, %sc.merge41 ]
  br i1 %sc52, label %if.then53, label %if.merge54

sc.rhs40:                                         ; preds = %sc.rhs29
  %field42 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data43 = load ptr, ptr %field42, align 8
  %i44 = load i32, ptr %i, align 4
  %add45 = add nsw i32 %i44, 1
  %pi.idx46 = sext i32 %add45 to i64
  %ptr.idx47 = getelementptr i8, ptr %data43, i64 %pi.idx46
  %ptr.elem48 = load i8, ptr %ptr.idx47, align 1
  %widen.zext49 = zext i8 %ptr.elem48 to i32
  %eq50 = icmp eq i32 %widen.zext49, 88
  br label %sc.merge41

sc.merge41:                                       ; preds = %sc.rhs40, %sc.rhs29
  %sc51 = phi i1 [ %eq39, %sc.rhs29 ], [ %eq50, %sc.rhs40 ]
  br label %sc.merge30

if.then53:                                        ; preds = %sc.merge30
  %i55 = load i32, ptr %i, align 4
  %add56 = add nsw i32 %i55, 2
  store i32 %add56, ptr %i, align 4
  %i57 = load i32, ptr %i, align 4
  %n58 = load i32, ptr %n, align 4
  %sge59 = icmp sge i32 %i57, %n58
  br i1 %sge59, label %if.then60, label %if.merge61

if.merge54:                                       ; preds = %sc.merge30
  store i32 0, ptr %val, align 4
  br label %while.cond134

if.then60:                                        ; preds = %if.then53
  %3 = call ptr @memset(ptr %enum.ctor62, i32 0, i64 24)
  %disc.p63 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor62, i32 0, i32 0
  store i8 1, ptr %disc.p63, align 1
  %payload.p64 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor62, i32 0, i32 1
  %field.p65 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p64, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.66, i32 13, i32 0 }, ptr %field.p65, align 8
  %enum.val66 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor62, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val66

if.merge61:                                       ; preds = %if.then53
  store i32 0, ptr %hv, align 4
  br label %while.cond

while.cond:                                       ; preds = %if.merge84, %if.merge61
  %i67 = load i32, ptr %i, align 4
  %n68 = load i32, ptr %n, align 4
  %slt69 = icmp slt i32 %i67, %n68
  br i1 %slt69, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field70 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data71 = load ptr, ptr %field70, align 8
  %i72 = load i32, ptr %i, align 4
  %pi.idx73 = sext i32 %i72 to i64
  %ptr.idx74 = getelementptr i8, ptr %data71, i64 %pi.idx73
  %ptr.elem75 = load i8, ptr %ptr.idx74, align 1
  %widen.zext76 = zext i8 %ptr.elem75 to i32
  store i32 %widen.zext76, ptr %hd, align 4
  store i32 0, ptr %dig, align 4
  %hd77 = load i32, ptr %hd, align 4
  %sge78 = icmp sge i32 %hd77, 48
  br i1 %sge78, label %sc.rhs79, label %sc.merge80

while.end:                                        ; preds = %while.cond
  %neg123 = load i1, ptr %neg, align 1
  br i1 %neg123, label %if.then124, label %if.merge125

sc.rhs79:                                         ; preds = %while.body
  %hd81 = load i32, ptr %hd, align 4
  %sle = icmp sle i32 %hd81, 57
  br label %sc.merge80

sc.merge80:                                       ; preds = %sc.rhs79, %while.body
  %sc82 = phi i1 [ %sge78, %while.body ], [ %sle, %sc.rhs79 ]
  br i1 %sc82, label %if.then83, label %if.else85

if.then83:                                        ; preds = %sc.merge80
  %hd86 = load i32, ptr %hd, align 4
  %sub = sub nsw i32 %hd86, 48
  store i32 %sub, ptr %dig, align 4
  br label %if.merge84

if.merge84:                                       ; preds = %if.merge95, %if.then83
  %hv118 = load i32, ptr %hv, align 4
  %mul = mul nsw i32 %hv118, 16
  %dig119 = load i32, ptr %dig, align 4
  %add120 = add nsw i32 %mul, %dig119
  store i32 %add120, ptr %hv, align 4
  %i121 = load i32, ptr %i, align 4
  %add122 = add nsw i32 %i121, 1
  store i32 %add122, ptr %i, align 4
  br label %while.cond

if.else85:                                        ; preds = %sc.merge80
  %hd87 = load i32, ptr %hd, align 4
  %sge88 = icmp sge i32 %hd87, 97
  br i1 %sge88, label %sc.rhs89, label %sc.merge90

sc.rhs89:                                         ; preds = %if.else85
  %hd91 = load i32, ptr %hd, align 4
  %sle92 = icmp sle i32 %hd91, 102
  br label %sc.merge90

sc.merge90:                                       ; preds = %sc.rhs89, %if.else85
  %sc93 = phi i1 [ %sge88, %if.else85 ], [ %sle92, %sc.rhs89 ]
  br i1 %sc93, label %if.then94, label %if.else96

if.then94:                                        ; preds = %sc.merge90
  %hd97 = load i32, ptr %hd, align 4
  %sub98 = sub nsw i32 %hd97, 97
  %add99 = add nsw i32 %sub98, 10
  store i32 %add99, ptr %dig, align 4
  br label %if.merge95

if.merge95:                                       ; preds = %if.merge108, %if.then94
  br label %if.merge84

if.else96:                                        ; preds = %sc.merge90
  %hd100 = load i32, ptr %hd, align 4
  %sge101 = icmp sge i32 %hd100, 65
  br i1 %sge101, label %sc.rhs102, label %sc.merge103

sc.rhs102:                                        ; preds = %if.else96
  %hd104 = load i32, ptr %hd, align 4
  %sle105 = icmp sle i32 %hd104, 70
  br label %sc.merge103

sc.merge103:                                      ; preds = %sc.rhs102, %if.else96
  %sc106 = phi i1 [ %sge101, %if.else96 ], [ %sle105, %sc.rhs102 ]
  br i1 %sc106, label %if.then107, label %if.else109

if.then107:                                       ; preds = %sc.merge103
  %hd110 = load i32, ptr %hd, align 4
  %sub111 = sub nsw i32 %hd110, 65
  %add112 = add nsw i32 %sub111, 10
  store i32 %add112, ptr %dig, align 4
  br label %if.merge108

if.merge108:                                      ; preds = %if.then107
  br label %if.merge95

if.else109:                                       ; preds = %sc.merge103
  %4 = call ptr @memset(ptr %enum.ctor113, i32 0, i64 24)
  %disc.p114 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor113, i32 0, i32 0
  store i8 1, ptr %disc.p114, align 1
  %payload.p115 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor113, i32 0, i32 1
  %field.p116 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p115, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.67, i32 17, i32 0 }, ptr %field.p116, align 8
  %enum.val117 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor113, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val117

if.then124:                                       ; preds = %while.end
  %hv126 = load i32, ptr %hv, align 4
  %sub127 = sub nsw i32 0, %hv126
  store i32 %sub127, ptr %hv, align 4
  br label %if.merge125

if.merge125:                                      ; preds = %if.then124, %while.end
  %5 = call ptr @memset(ptr %enum.ctor128, i32 0, i64 24)
  %disc.p129 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor128, i32 0, i32 0
  store i8 0, ptr %disc.p129, align 1
  %payload.p130 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor128, i32 0, i32 1
  %hv131 = load i32, ptr %hv, align 4
  %field.p132 = getelementptr inbounds { i32 }, ptr %payload.p130, i32 0, i32 0
  store i32 %hv131, ptr %field.p132, align 4
  %enum.val133 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor128, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val133

while.cond134:                                    ; preds = %if.merge154, %if.merge54
  %i137 = load i32, ptr %i, align 4
  %n138 = load i32, ptr %n, align 4
  %slt139 = icmp slt i32 %i137, %n138
  br i1 %slt139, label %while.body135, label %while.end136

while.body135:                                    ; preds = %while.cond134
  %field140 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data141 = load ptr, ptr %field140, align 8
  %i142 = load i32, ptr %i, align 4
  %pi.idx143 = sext i32 %i142 to i64
  %ptr.idx144 = getelementptr i8, ptr %data141, i64 %pi.idx143
  %ptr.elem145 = load i8, ptr %ptr.idx144, align 1
  %widen.zext146 = zext i8 %ptr.elem145 to i32
  store i32 %widen.zext146, ptr %d, align 4
  %d147 = load i32, ptr %d, align 4
  %slt148 = icmp slt i32 %d147, 48
  br i1 %slt148, label %sc.merge150, label %sc.rhs149

while.end136:                                     ; preds = %while.cond134
  %neg167 = load i1, ptr %neg, align 1
  br i1 %neg167, label %if.then168, label %if.merge169

sc.rhs149:                                        ; preds = %while.body135
  %d151 = load i32, ptr %d, align 4
  %sgt = icmp sgt i32 %d151, 57
  br label %sc.merge150

sc.merge150:                                      ; preds = %sc.rhs149, %while.body135
  %sc152 = phi i1 [ %slt148, %while.body135 ], [ %sgt, %sc.rhs149 ]
  br i1 %sc152, label %if.then153, label %if.merge154

if.then153:                                       ; preds = %sc.merge150
  %6 = call ptr @memset(ptr %enum.ctor155, i32 0, i64 24)
  %disc.p156 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor155, i32 0, i32 0
  store i8 1, ptr %disc.p156, align 1
  %payload.p157 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor155, i32 0, i32 1
  %field.p158 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p157, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.68, i32 13, i32 0 }, ptr %field.p158, align 8
  %enum.val159 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor155, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val159

if.merge154:                                      ; preds = %sc.merge150
  %val160 = load i32, ptr %val, align 4
  %mul161 = mul nsw i32 %val160, 10
  %d162 = load i32, ptr %d, align 4
  %sub163 = sub nsw i32 %d162, 48
  %add164 = add nsw i32 %mul161, %sub163
  store i32 %add164, ptr %val, align 4
  %i165 = load i32, ptr %i, align 4
  %add166 = add nsw i32 %i165, 1
  store i32 %add166, ptr %i, align 4
  br label %while.cond134

if.then168:                                       ; preds = %while.end136
  %val170 = load i32, ptr %val, align 4
  %sub171 = sub nsw i32 0, %val170
  store i32 %sub171, ptr %val, align 4
  br label %if.merge169

if.merge169:                                      ; preds = %if.then168, %while.end136
  %7 = call ptr @memset(ptr %enum.ctor172, i32 0, i64 24)
  %disc.p173 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor172, i32 0, i32 0
  store i8 0, ptr %disc.p173, align 1
  %payload.p174 = getelementptr inbounds %"Result(int,std_core_str_core__Str)", ptr %enum.ctor172, i32 0, i32 1
  %val175 = load i32, ptr %val, align 4
  %field.p176 = getelementptr inbounds { i32 }, ptr %payload.p174, i32 0, i32 0
  store i32 %val175, ptr %field.p176, align 4
  %enum.val177 = load %"Result(int,std_core_str_core__Str)", ptr %enum.ctor172, align 8
  ret %"Result(int,std_core_str_core__Str)" %enum.val177
}

declare ptr @memset(ptr %0, i32 %1, i64 %2)

define void @"Result(i64,std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.end
}

define %"Result(i64,std_core_str_core__Str)" @std_core_str_core__Str.to_i64(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %enum.ctor173 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %enum.ctor155 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %d = alloca i32, align 4
  %val = alloca i64, align 8
  %enum.ctor128 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %enum.ctor113 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %dig = alloca i32, align 4
  %hd = alloca i32, align 4
  %hv = alloca i64, align 8
  %enum.ctor62 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %enum.ctor15 = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %first = alloca i32, align 4
  %neg = alloca i1, align 1
  %i = alloca i32, align 4
  %enum.ctor = alloca %"Result(i64,std_core_str_core__Str)", align 8
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %eq = icmp eq i32 %n1, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 1, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.69, i32 12, i32 0 }, ptr %field.p, align 8
  %enum.val = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  store i1 false, ptr %neg, align 1
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %ptr.idx = getelementptr i8, ptr %data, i64 0
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %first, align 4
  %first3 = load i32, ptr %first, align 4
  %eq4 = icmp eq i32 %first3, 45
  br i1 %eq4, label %if.then5, label %if.else

if.then5:                                         ; preds = %if.merge
  store i1 true, ptr %neg, align 1
  store i32 1, ptr %i, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.merge10, %if.then5
  %i11 = load i32, ptr %i, align 4
  %n12 = load i32, ptr %n, align 4
  %sge = icmp sge i32 %i11, %n12
  br i1 %sge, label %if.then13, label %if.merge14

if.else:                                          ; preds = %if.merge
  %first7 = load i32, ptr %first, align 4
  %eq8 = icmp eq i32 %first7, 43
  br i1 %eq8, label %if.then9, label %if.merge10

if.then9:                                         ; preds = %if.else
  store i32 1, ptr %i, align 4
  br label %if.merge10

if.merge10:                                       ; preds = %if.then9, %if.else
  br label %if.merge6

if.then13:                                        ; preds = %if.merge6
  %2 = call ptr @memset(ptr %enum.ctor15, i32 0, i64 24)
  %disc.p16 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor15, i32 0, i32 0
  store i8 1, ptr %disc.p16, align 1
  %payload.p17 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor15, i32 0, i32 1
  %field.p18 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p17, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.70, i32 9, i32 0 }, ptr %field.p18, align 8
  %enum.val19 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor15, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val19

if.merge14:                                       ; preds = %if.merge6
  %i20 = load i32, ptr %i, align 4
  %add = add nsw i32 %i20, 1
  %n21 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %add, %n21
  br i1 %slt, label %sc.rhs, label %sc.merge

sc.rhs:                                           ; preds = %if.merge14
  %field22 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data23 = load ptr, ptr %field22, align 8
  %i24 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i24 to i64
  %ptr.idx25 = getelementptr i8, ptr %data23, i64 %pi.idx
  %ptr.elem26 = load i8, ptr %ptr.idx25, align 1
  %widen.zext27 = zext i8 %ptr.elem26 to i32
  %eq28 = icmp eq i32 %widen.zext27, 48
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %if.merge14
  %sc = phi i1 [ %slt, %if.merge14 ], [ %eq28, %sc.rhs ]
  br i1 %sc, label %sc.rhs29, label %sc.merge30

sc.rhs29:                                         ; preds = %sc.merge
  %field31 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data32 = load ptr, ptr %field31, align 8
  %i33 = load i32, ptr %i, align 4
  %add34 = add nsw i32 %i33, 1
  %pi.idx35 = sext i32 %add34 to i64
  %ptr.idx36 = getelementptr i8, ptr %data32, i64 %pi.idx35
  %ptr.elem37 = load i8, ptr %ptr.idx36, align 1
  %widen.zext38 = zext i8 %ptr.elem37 to i32
  %eq39 = icmp eq i32 %widen.zext38, 120
  br i1 %eq39, label %sc.merge41, label %sc.rhs40

sc.merge30:                                       ; preds = %sc.merge41, %sc.merge
  %sc52 = phi i1 [ %sc, %sc.merge ], [ %sc51, %sc.merge41 ]
  br i1 %sc52, label %if.then53, label %if.merge54

sc.rhs40:                                         ; preds = %sc.rhs29
  %field42 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data43 = load ptr, ptr %field42, align 8
  %i44 = load i32, ptr %i, align 4
  %add45 = add nsw i32 %i44, 1
  %pi.idx46 = sext i32 %add45 to i64
  %ptr.idx47 = getelementptr i8, ptr %data43, i64 %pi.idx46
  %ptr.elem48 = load i8, ptr %ptr.idx47, align 1
  %widen.zext49 = zext i8 %ptr.elem48 to i32
  %eq50 = icmp eq i32 %widen.zext49, 88
  br label %sc.merge41

sc.merge41:                                       ; preds = %sc.rhs40, %sc.rhs29
  %sc51 = phi i1 [ %eq39, %sc.rhs29 ], [ %eq50, %sc.rhs40 ]
  br label %sc.merge30

if.then53:                                        ; preds = %sc.merge30
  %i55 = load i32, ptr %i, align 4
  %add56 = add nsw i32 %i55, 2
  store i32 %add56, ptr %i, align 4
  %i57 = load i32, ptr %i, align 4
  %n58 = load i32, ptr %n, align 4
  %sge59 = icmp sge i32 %i57, %n58
  br i1 %sge59, label %if.then60, label %if.merge61

if.merge54:                                       ; preds = %sc.merge30
  store i64 0, ptr %val, align 8
  br label %while.cond134

if.then60:                                        ; preds = %if.then53
  %3 = call ptr @memset(ptr %enum.ctor62, i32 0, i64 24)
  %disc.p63 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor62, i32 0, i32 0
  store i8 1, ptr %disc.p63, align 1
  %payload.p64 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor62, i32 0, i32 1
  %field.p65 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p64, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.71, i32 13, i32 0 }, ptr %field.p65, align 8
  %enum.val66 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor62, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val66

if.merge61:                                       ; preds = %if.then53
  store i64 0, ptr %hv, align 8
  br label %while.cond

while.cond:                                       ; preds = %if.merge84, %if.merge61
  %i67 = load i32, ptr %i, align 4
  %n68 = load i32, ptr %n, align 4
  %slt69 = icmp slt i32 %i67, %n68
  br i1 %slt69, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field70 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data71 = load ptr, ptr %field70, align 8
  %i72 = load i32, ptr %i, align 4
  %pi.idx73 = sext i32 %i72 to i64
  %ptr.idx74 = getelementptr i8, ptr %data71, i64 %pi.idx73
  %ptr.elem75 = load i8, ptr %ptr.idx74, align 1
  %widen.zext76 = zext i8 %ptr.elem75 to i32
  store i32 %widen.zext76, ptr %hd, align 4
  store i32 0, ptr %dig, align 4
  %hd77 = load i32, ptr %hd, align 4
  %sge78 = icmp sge i32 %hd77, 48
  br i1 %sge78, label %sc.rhs79, label %sc.merge80

while.end:                                        ; preds = %while.cond
  %neg123 = load i1, ptr %neg, align 1
  br i1 %neg123, label %if.then124, label %if.merge125

sc.rhs79:                                         ; preds = %while.body
  %hd81 = load i32, ptr %hd, align 4
  %sle = icmp sle i32 %hd81, 57
  br label %sc.merge80

sc.merge80:                                       ; preds = %sc.rhs79, %while.body
  %sc82 = phi i1 [ %sge78, %while.body ], [ %sle, %sc.rhs79 ]
  br i1 %sc82, label %if.then83, label %if.else85

if.then83:                                        ; preds = %sc.merge80
  %hd86 = load i32, ptr %hd, align 4
  %sub = sub nsw i32 %hd86, 48
  store i32 %sub, ptr %dig, align 4
  br label %if.merge84

if.merge84:                                       ; preds = %if.merge95, %if.then83
  %hv118 = load i64, ptr %hv, align 8
  %mul = mul nsw i64 %hv118, 16
  %dig119 = load i32, ptr %dig, align 4
  %sext = sext i32 %dig119 to i64
  %add120 = add nsw i64 %mul, %sext
  store i64 %add120, ptr %hv, align 8
  %i121 = load i32, ptr %i, align 4
  %add122 = add nsw i32 %i121, 1
  store i32 %add122, ptr %i, align 4
  br label %while.cond

if.else85:                                        ; preds = %sc.merge80
  %hd87 = load i32, ptr %hd, align 4
  %sge88 = icmp sge i32 %hd87, 97
  br i1 %sge88, label %sc.rhs89, label %sc.merge90

sc.rhs89:                                         ; preds = %if.else85
  %hd91 = load i32, ptr %hd, align 4
  %sle92 = icmp sle i32 %hd91, 102
  br label %sc.merge90

sc.merge90:                                       ; preds = %sc.rhs89, %if.else85
  %sc93 = phi i1 [ %sge88, %if.else85 ], [ %sle92, %sc.rhs89 ]
  br i1 %sc93, label %if.then94, label %if.else96

if.then94:                                        ; preds = %sc.merge90
  %hd97 = load i32, ptr %hd, align 4
  %sub98 = sub nsw i32 %hd97, 97
  %add99 = add nsw i32 %sub98, 10
  store i32 %add99, ptr %dig, align 4
  br label %if.merge95

if.merge95:                                       ; preds = %if.merge108, %if.then94
  br label %if.merge84

if.else96:                                        ; preds = %sc.merge90
  %hd100 = load i32, ptr %hd, align 4
  %sge101 = icmp sge i32 %hd100, 65
  br i1 %sge101, label %sc.rhs102, label %sc.merge103

sc.rhs102:                                        ; preds = %if.else96
  %hd104 = load i32, ptr %hd, align 4
  %sle105 = icmp sle i32 %hd104, 70
  br label %sc.merge103

sc.merge103:                                      ; preds = %sc.rhs102, %if.else96
  %sc106 = phi i1 [ %sge101, %if.else96 ], [ %sle105, %sc.rhs102 ]
  br i1 %sc106, label %if.then107, label %if.else109

if.then107:                                       ; preds = %sc.merge103
  %hd110 = load i32, ptr %hd, align 4
  %sub111 = sub nsw i32 %hd110, 65
  %add112 = add nsw i32 %sub111, 10
  store i32 %add112, ptr %dig, align 4
  br label %if.merge108

if.merge108:                                      ; preds = %if.then107
  br label %if.merge95

if.else109:                                       ; preds = %sc.merge103
  %4 = call ptr @memset(ptr %enum.ctor113, i32 0, i64 24)
  %disc.p114 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor113, i32 0, i32 0
  store i8 1, ptr %disc.p114, align 1
  %payload.p115 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor113, i32 0, i32 1
  %field.p116 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p115, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.72, i32 17, i32 0 }, ptr %field.p116, align 8
  %enum.val117 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor113, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val117

if.then124:                                       ; preds = %while.end
  %hv126 = load i64, ptr %hv, align 8
  %sub127 = sub nsw i64 0, %hv126
  store i64 %sub127, ptr %hv, align 8
  br label %if.merge125

if.merge125:                                      ; preds = %if.then124, %while.end
  %5 = call ptr @memset(ptr %enum.ctor128, i32 0, i64 24)
  %disc.p129 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor128, i32 0, i32 0
  store i8 0, ptr %disc.p129, align 1
  %payload.p130 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor128, i32 0, i32 1
  %hv131 = load i64, ptr %hv, align 8
  %field.p132 = getelementptr inbounds { i64 }, ptr %payload.p130, i32 0, i32 0
  store i64 %hv131, ptr %field.p132, align 8
  %enum.val133 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor128, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val133

while.cond134:                                    ; preds = %if.merge154, %if.merge54
  %i137 = load i32, ptr %i, align 4
  %n138 = load i32, ptr %n, align 4
  %slt139 = icmp slt i32 %i137, %n138
  br i1 %slt139, label %while.body135, label %while.end136

while.body135:                                    ; preds = %while.cond134
  %field140 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data141 = load ptr, ptr %field140, align 8
  %i142 = load i32, ptr %i, align 4
  %pi.idx143 = sext i32 %i142 to i64
  %ptr.idx144 = getelementptr i8, ptr %data141, i64 %pi.idx143
  %ptr.elem145 = load i8, ptr %ptr.idx144, align 1
  %widen.zext146 = zext i8 %ptr.elem145 to i32
  store i32 %widen.zext146, ptr %d, align 4
  %d147 = load i32, ptr %d, align 4
  %slt148 = icmp slt i32 %d147, 48
  br i1 %slt148, label %sc.merge150, label %sc.rhs149

while.end136:                                     ; preds = %while.cond134
  %neg168 = load i1, ptr %neg, align 1
  br i1 %neg168, label %if.then169, label %if.merge170

sc.rhs149:                                        ; preds = %while.body135
  %d151 = load i32, ptr %d, align 4
  %sgt = icmp sgt i32 %d151, 57
  br label %sc.merge150

sc.merge150:                                      ; preds = %sc.rhs149, %while.body135
  %sc152 = phi i1 [ %slt148, %while.body135 ], [ %sgt, %sc.rhs149 ]
  br i1 %sc152, label %if.then153, label %if.merge154

if.then153:                                       ; preds = %sc.merge150
  %6 = call ptr @memset(ptr %enum.ctor155, i32 0, i64 24)
  %disc.p156 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor155, i32 0, i32 0
  store i8 1, ptr %disc.p156, align 1
  %payload.p157 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor155, i32 0, i32 1
  %field.p158 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p157, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.73, i32 13, i32 0 }, ptr %field.p158, align 8
  %enum.val159 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor155, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val159

if.merge154:                                      ; preds = %sc.merge150
  %val160 = load i64, ptr %val, align 8
  %mul161 = mul nsw i64 %val160, 10
  %d162 = load i32, ptr %d, align 4
  %sub163 = sub nsw i32 %d162, 48
  %sext164 = sext i32 %sub163 to i64
  %add165 = add nsw i64 %mul161, %sext164
  store i64 %add165, ptr %val, align 8
  %i166 = load i32, ptr %i, align 4
  %add167 = add nsw i32 %i166, 1
  store i32 %add167, ptr %i, align 4
  br label %while.cond134

if.then169:                                       ; preds = %while.end136
  %val171 = load i64, ptr %val, align 8
  %sub172 = sub nsw i64 0, %val171
  store i64 %sub172, ptr %val, align 8
  br label %if.merge170

if.merge170:                                      ; preds = %if.then169, %while.end136
  %7 = call ptr @memset(ptr %enum.ctor173, i32 0, i64 24)
  %disc.p174 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor173, i32 0, i32 0
  store i8 0, ptr %disc.p174, align 1
  %payload.p175 = getelementptr inbounds %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor173, i32 0, i32 1
  %val176 = load i64, ptr %val, align 8
  %field.p177 = getelementptr inbounds { i64 }, ptr %payload.p175, i32 0, i32 0
  store i64 %val176, ptr %field.p177, align 8
  %enum.val178 = load %"Result(i64,std_core_str_core__Str)", ptr %enum.ctor173, align 8
  ret %"Result(i64,std_core_str_core__Str)" %enum.val178
}

define void @"Result(f64,std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.end
}

define %"Result(f64,std_core_str_core__Str)" @std_core_str_core__Str.to_float(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %enum.ctor235 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %t = alloca i32, align 4
  %p10 = alloca double, align 8
  %enum.ctor204 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %d187 = alloca i32, align 4
  %ev = alloca i32, align 4
  %enum.ctor176 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %sc151 = alloca i32, align 4
  %eneg = alloca i1, align 1
  %enum.ctor139 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %ec = alloca i32, align 4
  %enum.ctor114 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %enum.ctor95 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %d69 = alloca i32, align 4
  %scale = alloca double, align 8
  %enum.ctor37 = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %d = alloca i32, align 4
  %any = alloca i1, align 1
  %val = alloca double, align 8
  %first = alloca i32, align 4
  %neg = alloca i1, align 1
  %i = alloca i32, align 4
  %enum.ctor = alloca %"Result(f64,std_core_str_core__Str)", align 8
  %n = alloca i32, align 4
  %field = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  store i32 %len, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %eq = icmp eq i32 %n1, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 1, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.74, i32 12, i32 0 }, ptr %field.p, align 8
  %enum.val = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val

if.merge:                                         ; preds = %entry
  store i32 0, ptr %i, align 4
  store i1 false, ptr %neg, align 1
  %field2 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %ptr.idx = getelementptr i8, ptr %data, i64 0
  %ptr.elem = load i8, ptr %ptr.idx, align 1
  %widen.zext = zext i8 %ptr.elem to i32
  store i32 %widen.zext, ptr %first, align 4
  %first3 = load i32, ptr %first, align 4
  %eq4 = icmp eq i32 %first3, 45
  br i1 %eq4, label %if.then5, label %if.else

if.then5:                                         ; preds = %if.merge
  store i1 true, ptr %neg, align 1
  store i32 1, ptr %i, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.merge10, %if.then5
  store double 0.000000e+00, ptr %val, align 8
  store i1 false, ptr %any, align 1
  br label %while.cond

if.else:                                          ; preds = %if.merge
  %first7 = load i32, ptr %first, align 4
  %eq8 = icmp eq i32 %first7, 43
  br i1 %eq8, label %if.then9, label %if.merge10

if.then9:                                         ; preds = %if.else
  store i32 1, ptr %i, align 4
  br label %if.merge10

if.merge10:                                       ; preds = %if.then9, %if.else
  br label %if.merge6

while.cond:                                       ; preds = %if.merge36, %if.merge6
  %i11 = load i32, ptr %i, align 4
  %n12 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %i11, %n12
  br i1 %slt, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %field13 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data14 = load ptr, ptr %field13, align 8
  %i15 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i15 to i64
  %ptr.idx16 = getelementptr i8, ptr %data14, i64 %pi.idx
  %ptr.elem17 = load i8, ptr %ptr.idx16, align 1
  %widen.zext18 = zext i8 %ptr.elem17 to i32
  store i32 %widen.zext18, ptr %d, align 4
  %d19 = load i32, ptr %d, align 4
  %eq20 = icmp eq i32 %d19, 46
  br i1 %eq20, label %if.then21, label %if.merge22

while.end:                                        ; preds = %if.then27, %if.then21, %while.cond
  %i45 = load i32, ptr %i, align 4
  %n46 = load i32, ptr %n, align 4
  %slt47 = icmp slt i32 %i45, %n46
  br i1 %slt47, label %sc.rhs48, label %sc.merge49

if.then21:                                        ; preds = %while.body
  br label %while.end

if.merge22:                                       ; preds = %while.body
  %d23 = load i32, ptr %d, align 4
  %eq24 = icmp eq i32 %d23, 101
  br i1 %eq24, label %sc.merge, label %sc.rhs

sc.rhs:                                           ; preds = %if.merge22
  %d25 = load i32, ptr %d, align 4
  %eq26 = icmp eq i32 %d25, 69
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %if.merge22
  %sc = phi i1 [ %eq24, %if.merge22 ], [ %eq26, %sc.rhs ]
  br i1 %sc, label %if.then27, label %if.merge28

if.then27:                                        ; preds = %sc.merge
  br label %while.end

if.merge28:                                       ; preds = %sc.merge
  %d29 = load i32, ptr %d, align 4
  %slt30 = icmp slt i32 %d29, 48
  br i1 %slt30, label %sc.merge32, label %sc.rhs31

sc.rhs31:                                         ; preds = %if.merge28
  %d33 = load i32, ptr %d, align 4
  %sgt = icmp sgt i32 %d33, 57
  br label %sc.merge32

sc.merge32:                                       ; preds = %sc.rhs31, %if.merge28
  %sc34 = phi i1 [ %slt30, %if.merge28 ], [ %sgt, %sc.rhs31 ]
  br i1 %sc34, label %if.then35, label %if.merge36

if.then35:                                        ; preds = %sc.merge32
  %2 = call ptr @memset(ptr %enum.ctor37, i32 0, i64 24)
  %disc.p38 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor37, i32 0, i32 0
  store i8 1, ptr %disc.p38, align 1
  %payload.p39 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor37, i32 0, i32 1
  %field.p40 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p39, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.75, i32 13, i32 0 }, ptr %field.p40, align 8
  %enum.val41 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor37, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val41

if.merge36:                                       ; preds = %sc.merge32
  %val42 = load double, ptr %val, align 8
  %fmul = fmul contract double %val42, 1.000000e+01
  %d43 = load i32, ptr %d, align 4
  %sub = sub nsw i32 %d43, 48
  %sitofp = sitofp i32 %sub to double
  %fadd = fadd contract double %fmul, %sitofp
  store double %fadd, ptr %val, align 8
  store i1 true, ptr %any, align 1
  %i44 = load i32, ptr %i, align 4
  %add = add nsw i32 %i44, 1
  store i32 %add, ptr %i, align 4
  br label %while.cond

sc.rhs48:                                         ; preds = %while.end
  %field50 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data51 = load ptr, ptr %field50, align 8
  %i52 = load i32, ptr %i, align 4
  %pi.idx53 = sext i32 %i52 to i64
  %ptr.idx54 = getelementptr i8, ptr %data51, i64 %pi.idx53
  %ptr.elem55 = load i8, ptr %ptr.idx54, align 1
  %widen.zext56 = zext i8 %ptr.elem55 to i32
  %eq57 = icmp eq i32 %widen.zext56, 46
  br label %sc.merge49

sc.merge49:                                       ; preds = %sc.rhs48, %while.end
  %sc58 = phi i1 [ %slt47, %while.end ], [ %eq57, %sc.rhs48 ]
  br i1 %sc58, label %if.then59, label %if.merge60

if.then59:                                        ; preds = %sc.merge49
  %i61 = load i32, ptr %i, align 4
  %add62 = add nsw i32 %i61, 1
  store i32 %add62, ptr %i, align 4
  store double 1.000000e-01, ptr %scale, align 8
  br label %while.cond63

if.merge60:                                       ; preds = %while.end65, %sc.merge49
  %any111 = load i1, ptr %any, align 1
  %not = xor i1 %any111, true
  br i1 %not, label %if.then112, label %if.merge113

while.cond63:                                     ; preds = %if.merge94, %if.then59
  %i66 = load i32, ptr %i, align 4
  %n67 = load i32, ptr %n, align 4
  %slt68 = icmp slt i32 %i66, %n67
  br i1 %slt68, label %while.body64, label %while.end65

while.body64:                                     ; preds = %while.cond63
  %field70 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data71 = load ptr, ptr %field70, align 8
  %i72 = load i32, ptr %i, align 4
  %pi.idx73 = sext i32 %i72 to i64
  %ptr.idx74 = getelementptr i8, ptr %data71, i64 %pi.idx73
  %ptr.elem75 = load i8, ptr %ptr.idx74, align 1
  %widen.zext76 = zext i8 %ptr.elem75 to i32
  store i32 %widen.zext76, ptr %d69, align 4
  %d77 = load i32, ptr %d69, align 4
  %eq78 = icmp eq i32 %d77, 101
  br i1 %eq78, label %sc.merge80, label %sc.rhs79

while.end65:                                      ; preds = %if.then84, %while.cond63
  br label %if.merge60

sc.rhs79:                                         ; preds = %while.body64
  %d81 = load i32, ptr %d69, align 4
  %eq82 = icmp eq i32 %d81, 69
  br label %sc.merge80

sc.merge80:                                       ; preds = %sc.rhs79, %while.body64
  %sc83 = phi i1 [ %eq78, %while.body64 ], [ %eq82, %sc.rhs79 ]
  br i1 %sc83, label %if.then84, label %if.merge85

if.then84:                                        ; preds = %sc.merge80
  br label %while.end65

if.merge85:                                       ; preds = %sc.merge80
  %d86 = load i32, ptr %d69, align 4
  %slt87 = icmp slt i32 %d86, 48
  br i1 %slt87, label %sc.merge89, label %sc.rhs88

sc.rhs88:                                         ; preds = %if.merge85
  %d90 = load i32, ptr %d69, align 4
  %sgt91 = icmp sgt i32 %d90, 57
  br label %sc.merge89

sc.merge89:                                       ; preds = %sc.rhs88, %if.merge85
  %sc92 = phi i1 [ %slt87, %if.merge85 ], [ %sgt91, %sc.rhs88 ]
  br i1 %sc92, label %if.then93, label %if.merge94

if.then93:                                        ; preds = %sc.merge89
  %3 = call ptr @memset(ptr %enum.ctor95, i32 0, i64 24)
  %disc.p96 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor95, i32 0, i32 0
  store i8 1, ptr %disc.p96, align 1
  %payload.p97 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor95, i32 0, i32 1
  %field.p98 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p97, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.76, i32 13, i32 0 }, ptr %field.p98, align 8
  %enum.val99 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor95, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val99

if.merge94:                                       ; preds = %sc.merge89
  %val100 = load double, ptr %val, align 8
  %d101 = load i32, ptr %d69, align 4
  %sub102 = sub nsw i32 %d101, 48
  %sitofp103 = sitofp i32 %sub102 to double
  %scale104 = load double, ptr %scale, align 8
  %fmul105 = fmul contract double %sitofp103, %scale104
  %fadd106 = fadd contract double %val100, %fmul105
  store double %fadd106, ptr %val, align 8
  %scale107 = load double, ptr %scale, align 8
  %fmul108 = fmul contract double %scale107, 1.000000e-01
  store double %fmul108, ptr %scale, align 8
  store i1 true, ptr %any, align 1
  %i109 = load i32, ptr %i, align 4
  %add110 = add nsw i32 %i109, 1
  store i32 %add110, ptr %i, align 4
  br label %while.cond63

if.then112:                                       ; preds = %if.merge60
  %4 = call ptr @memset(ptr %enum.ctor114, i32 0, i64 24)
  %disc.p115 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor114, i32 0, i32 0
  store i8 1, ptr %disc.p115, align 1
  %payload.p116 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor114, i32 0, i32 1
  %field.p117 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p116, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.77, i32 9, i32 0 }, ptr %field.p117, align 8
  %enum.val118 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor114, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val118

if.merge113:                                      ; preds = %if.merge60
  %i119 = load i32, ptr %i, align 4
  %n120 = load i32, ptr %n, align 4
  %slt121 = icmp slt i32 %i119, %n120
  br i1 %slt121, label %if.then122, label %if.merge123

if.then122:                                       ; preds = %if.merge113
  %field124 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data125 = load ptr, ptr %field124, align 8
  %i126 = load i32, ptr %i, align 4
  %pi.idx127 = sext i32 %i126 to i64
  %ptr.idx128 = getelementptr i8, ptr %data125, i64 %pi.idx127
  %ptr.elem129 = load i8, ptr %ptr.idx128, align 1
  %widen.zext130 = zext i8 %ptr.elem129 to i32
  store i32 %widen.zext130, ptr %ec, align 4
  %ec131 = load i32, ptr %ec, align 4
  %ne = icmp ne i32 %ec131, 101
  br i1 %ne, label %sc.rhs132, label %sc.merge133

if.merge123:                                      ; preds = %if.merge224, %if.merge113
  %neg231 = load i1, ptr %neg, align 1
  br i1 %neg231, label %if.then232, label %if.merge233

sc.rhs132:                                        ; preds = %if.then122
  %ec134 = load i32, ptr %ec, align 4
  %ne135 = icmp ne i32 %ec134, 69
  br label %sc.merge133

sc.merge133:                                      ; preds = %sc.rhs132, %if.then122
  %sc136 = phi i1 [ %ne, %if.then122 ], [ %ne135, %sc.rhs132 ]
  br i1 %sc136, label %if.then137, label %if.merge138

if.then137:                                       ; preds = %sc.merge133
  %5 = call ptr @memset(ptr %enum.ctor139, i32 0, i64 24)
  %disc.p140 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor139, i32 0, i32 0
  store i8 1, ptr %disc.p140, align 1
  %payload.p141 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor139, i32 0, i32 1
  %field.p142 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p141, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.78, i32 13, i32 0 }, ptr %field.p142, align 8
  %enum.val143 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor139, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val143

if.merge138:                                      ; preds = %sc.merge133
  %i144 = load i32, ptr %i, align 4
  %add145 = add nsw i32 %i144, 1
  store i32 %add145, ptr %i, align 4
  store i1 false, ptr %eneg, align 1
  %i146 = load i32, ptr %i, align 4
  %n147 = load i32, ptr %n, align 4
  %slt148 = icmp slt i32 %i146, %n147
  br i1 %slt148, label %if.then149, label %if.merge150

if.then149:                                       ; preds = %if.merge138
  %field152 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data153 = load ptr, ptr %field152, align 8
  %i154 = load i32, ptr %i, align 4
  %pi.idx155 = sext i32 %i154 to i64
  %ptr.idx156 = getelementptr i8, ptr %data153, i64 %pi.idx155
  %ptr.elem157 = load i8, ptr %ptr.idx156, align 1
  %widen.zext158 = zext i8 %ptr.elem157 to i32
  store i32 %widen.zext158, ptr %sc151, align 4
  %sc159 = load i32, ptr %sc151, align 4
  %eq160 = icmp eq i32 %sc159, 45
  br i1 %eq160, label %if.then161, label %if.else163

if.merge150:                                      ; preds = %if.merge162, %if.merge138
  %i172 = load i32, ptr %i, align 4
  %n173 = load i32, ptr %n, align 4
  %sge = icmp sge i32 %i172, %n173
  br i1 %sge, label %if.then174, label %if.merge175

if.then161:                                       ; preds = %if.then149
  store i1 true, ptr %eneg, align 1
  %i164 = load i32, ptr %i, align 4
  %add165 = add nsw i32 %i164, 1
  store i32 %add165, ptr %i, align 4
  br label %if.merge162

if.merge162:                                      ; preds = %if.merge169, %if.then161
  br label %if.merge150

if.else163:                                       ; preds = %if.then149
  %sc166 = load i32, ptr %sc151, align 4
  %eq167 = icmp eq i32 %sc166, 43
  br i1 %eq167, label %if.then168, label %if.merge169

if.then168:                                       ; preds = %if.else163
  %i170 = load i32, ptr %i, align 4
  %add171 = add nsw i32 %i170, 1
  store i32 %add171, ptr %i, align 4
  br label %if.merge169

if.merge169:                                      ; preds = %if.then168, %if.else163
  br label %if.merge162

if.then174:                                       ; preds = %if.merge150
  %6 = call ptr @memset(ptr %enum.ctor176, i32 0, i64 24)
  %disc.p177 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor176, i32 0, i32 0
  store i8 1, ptr %disc.p177, align 1
  %payload.p178 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor176, i32 0, i32 1
  %field.p179 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p178, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.79, i32 9, i32 0 }, ptr %field.p179, align 8
  %enum.val180 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor176, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val180

if.merge175:                                      ; preds = %if.merge150
  store i32 0, ptr %ev, align 4
  br label %while.cond181

while.cond181:                                    ; preds = %if.merge203, %if.merge175
  %i184 = load i32, ptr %i, align 4
  %n185 = load i32, ptr %n, align 4
  %slt186 = icmp slt i32 %i184, %n185
  br i1 %slt186, label %while.body182, label %while.end183

while.body182:                                    ; preds = %while.cond181
  %field188 = getelementptr inbounds %std_core_str_core__Str, ptr %0, i32 0, i32 0
  %data189 = load ptr, ptr %field188, align 8
  %i190 = load i32, ptr %i, align 4
  %pi.idx191 = sext i32 %i190 to i64
  %ptr.idx192 = getelementptr i8, ptr %data189, i64 %pi.idx191
  %ptr.elem193 = load i8, ptr %ptr.idx192, align 1
  %widen.zext194 = zext i8 %ptr.elem193 to i32
  store i32 %widen.zext194, ptr %d187, align 4
  %d195 = load i32, ptr %d187, align 4
  %slt196 = icmp slt i32 %d195, 48
  br i1 %slt196, label %sc.merge198, label %sc.rhs197

while.end183:                                     ; preds = %while.cond181
  store double 1.000000e+00, ptr %p10, align 8
  store i32 0, ptr %t, align 4
  br label %for.cond

sc.rhs197:                                        ; preds = %while.body182
  %d199 = load i32, ptr %d187, align 4
  %sgt200 = icmp sgt i32 %d199, 57
  br label %sc.merge198

sc.merge198:                                      ; preds = %sc.rhs197, %while.body182
  %sc201 = phi i1 [ %slt196, %while.body182 ], [ %sgt200, %sc.rhs197 ]
  br i1 %sc201, label %if.then202, label %if.merge203

if.then202:                                       ; preds = %sc.merge198
  %7 = call ptr @memset(ptr %enum.ctor204, i32 0, i64 24)
  %disc.p205 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor204, i32 0, i32 0
  store i8 1, ptr %disc.p205, align 1
  %payload.p206 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor204, i32 0, i32 1
  %field.p207 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p206, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.80, i32 13, i32 0 }, ptr %field.p207, align 8
  %enum.val208 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor204, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val208

if.merge203:                                      ; preds = %sc.merge198
  %ev209 = load i32, ptr %ev, align 4
  %mul = mul nsw i32 %ev209, 10
  %d210 = load i32, ptr %d187, align 4
  %sub211 = sub nsw i32 %d210, 48
  %add212 = add nsw i32 %mul, %sub211
  store i32 %add212, ptr %ev, align 4
  %i213 = load i32, ptr %i, align 4
  %add214 = add nsw i32 %i213, 1
  store i32 %add214, ptr %i, align 4
  br label %while.cond181

for.cond:                                         ; preds = %for.update, %while.end183
  %t215 = load i32, ptr %t, align 4
  %ev216 = load i32, ptr %ev, align 4
  %slt217 = icmp slt i32 %t215, %ev216
  br i1 %slt217, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %p10218 = load double, ptr %p10, align 8
  %fmul219 = fmul contract double %p10218, 1.000000e+01
  store double %fmul219, ptr %p10, align 8
  br label %for.update

for.update:                                       ; preds = %for.body
  %t220 = load i32, ptr %t, align 4
  %add221 = add nsw i32 %t220, 1
  store i32 %add221, ptr %t, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %eneg222 = load i1, ptr %eneg, align 1
  br i1 %eneg222, label %if.then223, label %if.else225

if.then223:                                       ; preds = %for.end
  %val226 = load double, ptr %val, align 8
  %p10227 = load double, ptr %p10, align 8
  %fdiv = fdiv contract double %val226, %p10227
  store double %fdiv, ptr %val, align 8
  br label %if.merge224

if.merge224:                                      ; preds = %if.else225, %if.then223
  br label %if.merge123

if.else225:                                       ; preds = %for.end
  %val228 = load double, ptr %val, align 8
  %p10229 = load double, ptr %p10, align 8
  %fmul230 = fmul contract double %val228, %p10229
  store double %fmul230, ptr %val, align 8
  br label %if.merge224

if.then232:                                       ; preds = %if.merge123
  %val234 = load double, ptr %val, align 8
  %fsub = fsub contract double 0.000000e+00, %val234
  store double %fsub, ptr %val, align 8
  br label %if.merge233

if.merge233:                                      ; preds = %if.then232, %if.merge123
  %8 = call ptr @memset(ptr %enum.ctor235, i32 0, i64 24)
  %disc.p236 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor235, i32 0, i32 0
  store i8 0, ptr %disc.p236, align 1
  %payload.p237 = getelementptr inbounds %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor235, i32 0, i32 1
  %val238 = load double, ptr %val, align 8
  %field.p239 = getelementptr inbounds { double }, ptr %payload.p237, i32 0, i32 0
  store double %val238, ptr %field.p239, align 8
  %enum.val240 = load %"Result(f64,std_core_str_core__Str)", ptr %enum.ctor235, align 8
  ret %"Result(f64,std_core_str_core__Str)" %enum.val240
}

define void @"Result(bool,std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.end
}

define %"Result(bool,std_core_str_core__Str)" @std_core_str_core__Str.to_bool(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %enum.ctor20 = alloca %"Result(bool,std_core_str_core__Str)", align 8
  %enum.ctor8 = alloca %"Result(bool,std_core_str_core__Str)", align 8
  %enum.ctor = alloca %"Result(bool,std_core_str_core__Str)", align 8
  %var.moved1 = alloca i1, align 1
  %f = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %t = alloca %std_core_str_core__Str, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %t)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.81, i32 4, i32 0 }, ptr %t, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %f)
  store i1 false, ptr %var.moved1, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %f, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.82, i32 5, i32 0 }, ptr %f, align 8
  %t2 = load %std_core_str_core__Str, ptr %t, align 8
  %call = call i1 @"std_core_str_core__Str.eq?"(ptr %0, ptr %t)
  br i1 %call, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %field.p = getelementptr inbounds { i1 }, ptr %payload.p, i32 0, i32 0
  store i1 true, ptr %field.p, align 1
  %enum.val = load %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor, align 8
  br label %cleanup

if.merge:                                         ; preds = %entry
  %f4 = load %std_core_str_core__Str, ptr %f, align 8
  %call5 = call i1 @"std_core_str_core__Str.eq?"(ptr %0, ptr %f)
  br i1 %call5, label %if.then6, label %if.merge7

cleanup:                                          ; preds = %if.then
  %drop.flag = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  %drop.flag3 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag3, label %drop.skip1, label %drop.call1

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %f)
  br label %drop.skip0

drop.skip1:                                       ; preds = %drop.call1, %drop.skip0
  call void @llvm.lifetime.end.p0(i64 16, ptr %f)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t)
  ret %"Result(bool,std_core_str_core__Str)" %enum.val

drop.call1:                                       ; preds = %drop.skip0
  call void @std_core_str_core__Str.__drop(ptr %t)
  br label %drop.skip1

if.then6:                                         ; preds = %if.merge
  %2 = call ptr @memset(ptr %enum.ctor8, i32 0, i64 24)
  %disc.p9 = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor8, i32 0, i32 0
  store i8 0, ptr %disc.p9, align 1
  %payload.p10 = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor8, i32 0, i32 1
  %field.p11 = getelementptr inbounds { i1 }, ptr %payload.p10, i32 0, i32 0
  store i1 false, ptr %field.p11, align 1
  %enum.val12 = load %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor8, align 8
  br label %cleanup13

if.merge7:                                        ; preds = %if.merge
  %3 = call ptr @memset(ptr %enum.ctor20, i32 0, i64 24)
  %disc.p21 = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor20, i32 0, i32 0
  store i8 1, ptr %disc.p21, align 1
  %payload.p22 = getelementptr inbounds %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor20, i32 0, i32 1
  %field.p23 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p22, i32 0, i32 0
  store %std_core_str_core__Str { ptr @.ls.strlit.83, i32 12, i32 0 }, ptr %field.p23, align 8
  %enum.val24 = load %"Result(bool,std_core_str_core__Str)", ptr %enum.ctor20, align 8
  br label %cleanup25

cleanup13:                                        ; preds = %if.then6
  %drop.flag16 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag16, label %drop.skip014, label %drop.call015

drop.skip014:                                     ; preds = %drop.call015, %cleanup13
  %drop.flag19 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag19, label %drop.skip117, label %drop.call118

drop.call015:                                     ; preds = %cleanup13
  call void @std_core_str_core__Str.__drop(ptr %f)
  br label %drop.skip014

drop.skip117:                                     ; preds = %drop.call118, %drop.skip014
  call void @llvm.lifetime.end.p0(i64 16, ptr %f)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t)
  ret %"Result(bool,std_core_str_core__Str)" %enum.val12

drop.call118:                                     ; preds = %drop.skip014
  call void @std_core_str_core__Str.__drop(ptr %t)
  br label %drop.skip117

cleanup25:                                        ; preds = %if.merge7
  %drop.flag28 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag28, label %drop.skip026, label %drop.call027

drop.skip026:                                     ; preds = %drop.call027, %cleanup25
  %drop.flag31 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag31, label %drop.skip129, label %drop.call130

drop.call027:                                     ; preds = %cleanup25
  call void @std_core_str_core__Str.__drop(ptr %f)
  br label %drop.skip026

drop.skip129:                                     ; preds = %drop.call130, %drop.skip026
  call void @llvm.lifetime.end.p0(i64 16, ptr %f)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t)
  ret %"Result(bool,std_core_str_core__Str)" %enum.val24

drop.call130:                                     ; preds = %drop.skip026
  call void @std_core_str_core__Str.__drop(ptr %t)
  br label %drop.skip129
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg %0, ptr nocapture %1) #1

define i32 @name(%Color %0) {
entry:
  %match.subj = alloca %Color, align 8
  %match.res = alloca i32, align 4
  store i32 0, ptr %match.res, align 4
  %c = alloca %Color, align 8
  store %Color %0, ptr %c, align 1
  %c1 = load %Color, ptr %c, align 1
  store %Color %c1, ptr %match.subj, align 1
  %disc.p = getelementptr inbounds %Color, ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %Color, ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case2
    i8 2, label %match.case3
  ]

match.end:                                        ; preds = %match.case3, %match.case2, %match.case
  %match.val = load i32, ptr %match.res, align 4
  ret i32 %match.val

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  store i32 1, ptr %match.res, align 4
  br label %match.end

match.case2:                                      ; preds = %entry
  store i32 2, ptr %match.res, align 4
  br label %match.end

match.case3:                                      ; preds = %entry
  store i32 3, ptr %match.res, align 4
  br label %match.end
}

define i32 @main(i32 %0, ptr %1) {
entry:
  call void @__ls_set_args(i32 %0, ptr %1)
  %enum.ctor4 = alloca %Color, align 8
  %c = alloca %Color, align 8
  %enum.ctor1 = alloca %Color, align 8
  %b = alloca %Color, align 8
  %enum.ctor = alloca %Color, align 8
  %a = alloca %Color, align 8
  call void @llvm.lifetime.start.p0(i64 1, ptr %a)
  %2 = call ptr @memset(ptr %enum.ctor, i32 0, i64 1)
  %disc.p = getelementptr inbounds %Color, ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p, align 1
  %enum.val = load %Color, ptr %enum.ctor, align 1
  store %Color %enum.val, ptr %a, align 1
  call void @llvm.lifetime.start.p0(i64 1, ptr %b)
  %3 = call ptr @memset(ptr %enum.ctor1, i32 0, i64 1)
  %disc.p2 = getelementptr inbounds %Color, ptr %enum.ctor1, i32 0, i32 0
  store i8 1, ptr %disc.p2, align 1
  %enum.val3 = load %Color, ptr %enum.ctor1, align 1
  store %Color %enum.val3, ptr %b, align 1
  call void @llvm.lifetime.start.p0(i64 1, ptr %c)
  %4 = call ptr @memset(ptr %enum.ctor4, i32 0, i64 1)
  %disc.p5 = getelementptr inbounds %Color, ptr %enum.ctor4, i32 0, i32 0
  store i8 2, ptr %disc.p5, align 1
  %enum.val6 = load %Color, ptr %enum.ctor4, align 1
  store %Color %enum.val6, ptr %c, align 1
  %a7 = load %Color, ptr %a, align 1
  %call = call i32 @name(%Color %a7)
  %5 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.84, i32 %call)
  %b8 = load %Color, ptr %b, align 1
  %call9 = call i32 @name(%Color %b8)
  %6 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.85, i32 %call9)
  %c10 = load %Color, ptr %c, align 1
  %call11 = call i32 @name(%Color %c10)
  %7 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.86, i32 %call11)
  call void @llvm.lifetime.end.p0(i64 1, ptr %c)
  call void @llvm.lifetime.end.p0(i64 1, ptr %b)
  call void @llvm.lifetime.end.p0(i64 1, ptr %a)
  call void @__ls_flush_out()
  ret i32 0
}

define void @"Vec(std_core_str_core__StrSlice).reserve"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %n = alloca i32, align 4
  %need = alloca i32, align 4
  store i32 %1, ptr %need, align 4
  %need1 = load i32, ptr %need, align 4
  %field = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sle = icmp sle i32 %need1, %cap
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret void

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 2
  %cap3 = load i32, ptr %field2, align 4
  store i32 %cap3, ptr %n, align 4
  %n4 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %n4, 4
  br i1 %slt, label %if.then5, label %if.merge6

if.then5:                                         ; preds = %if.merge
  store i32 4, ptr %n, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.then5, %if.merge
  br label %while.cond

while.cond:                                       ; preds = %while.body, %if.merge6
  %n7 = load i32, ptr %n, align 4
  %need8 = load i32, ptr %need, align 4
  %slt9 = icmp slt i32 %n7, %need8
  br i1 %slt9, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %n10 = load i32, ptr %n, align 4
  %mul = mul nsw i32 %n10, 2
  store i32 %mul, ptr %n, align 4
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %field11 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field11, align 8
  %n12 = load i32, ptr %n, align 4
  %widen.sext = sext i32 %n12 to i64
  %mul13 = mul nsw i64 %widen.sext, ptrtoint (ptr getelementptr (%std_core_str_core__StrSlice, ptr null, i32 1) to i64)
  %2 = call ptr @realloc(ptr %data, i64 %mul13)
  %field.ptr = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  store ptr %2, ptr %field.ptr, align 8
  %n14 = load i32, ptr %n, align 4
  %field.ptr15 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 2
  store i32 %n14, ptr %field.ptr15, align 4
  ret void
}

define void @"Vec(std_core_str_core__StrSlice).__from_list"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, %std_core_str_core__StrSlice %1) {
entry:
  %x = alloca %std_core_str_core__StrSlice, align 8
  store %std_core_str_core__StrSlice %1, ptr %x, align 8
  %x1 = load %std_core_str_core__StrSlice, ptr %x, align 8
  call void @"Vec(std_core_str_core__StrSlice).push"(ptr %0, %std_core_str_core__StrSlice %x1)
  ret void
}

define %"Vec(std_core_str_core__StrSlice)" @"Vec(std_core_str_core__StrSlice).copy"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  %sl.tmp = alloca %"Vec(std_core_str_core__StrSlice)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(std_core_str_core__StrSlice)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__StrSlice)" zeroinitializer, ptr %out, align 8
  store %"Vec(std_core_str_core__StrSlice)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(std_core_str_core__StrSlice)", ptr %sl.tmp, align 8
  store %"Vec(std_core_str_core__StrSlice)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @"Vec(std_core_str_core__StrSlice).reserve"(ptr %out, i32 %len)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %slt = icmp slt i32 %i1, %len3
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field4 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %i5 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i5 to i64
  %ptr.elem.ptr = getelementptr %std_core_str_core__StrSlice, ptr %data, i64 %lp.idx
  %dup.src = load %std_core_str_core__StrSlice, ptr %ptr.elem.ptr, align 8
  call void @"Vec(std_core_str_core__StrSlice).push"(ptr %out, %std_core_str_core__StrSlice %dup.src)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i6 = load i32, ptr %i, align 4
  %add = add nsw i32 %i6, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %out7 = load %"Vec(std_core_str_core__StrSlice)", ptr %out, align 8
  ret %"Vec(std_core_str_core__StrSlice)" %out7
}

define %"Vec(std_core_str_core__StrSlice)" @"Vec(std_core_str_core__StrSlice).__clone"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %call = call %"Vec(std_core_str_core__StrSlice)" @"Vec(std_core_str_core__StrSlice).copy"(ptr %0)
  ret %"Vec(std_core_str_core__StrSlice)" %call
}

define void @"Vec(std_core_str_core__StrSlice).__drop"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %slt = icmp slt i32 %i1, %len
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %i3 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i3 to i64
  %ptr.elem.ptr = getelementptr %std_core_str_core__StrSlice, ptr %data, i64 %lp.idx
  br label %for.update

for.update:                                       ; preds = %for.body
  %i4 = load i32, ptr %i, align 4
  %add = add nsw i32 %i4, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %field5 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field5, align 4
  %sgt = icmp sgt i32 %cap, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %for.end
  %field6 = getelementptr inbounds %"Vec(std_core_str_core__StrSlice)", ptr %0, i32 0, i32 0
  %data7 = load ptr, ptr %field6, align 8
  call void @free(ptr %data7)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %for.end
  ret void
}

define void @"Vec(int).reserve"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %n = alloca i32, align 4
  %need = alloca i32, align 4
  store i32 %1, ptr %need, align 4
  %need1 = load i32, ptr %need, align 4
  %field = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sle = icmp sle i32 %need1, %cap
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret void

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 2
  %cap3 = load i32, ptr %field2, align 4
  store i32 %cap3, ptr %n, align 4
  %n4 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %n4, 4
  br i1 %slt, label %if.then5, label %if.merge6

if.then5:                                         ; preds = %if.merge
  store i32 4, ptr %n, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.then5, %if.merge
  br label %while.cond

while.cond:                                       ; preds = %while.body, %if.merge6
  %n7 = load i32, ptr %n, align 4
  %need8 = load i32, ptr %need, align 4
  %slt9 = icmp slt i32 %n7, %need8
  br i1 %slt9, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %n10 = load i32, ptr %n, align 4
  %mul = mul nsw i32 %n10, 2
  store i32 %mul, ptr %n, align 4
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %field11 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field11, align 8
  %n12 = load i32, ptr %n, align 4
  %widen.sext = sext i32 %n12 to i64
  %mul13 = mul nsw i64 %widen.sext, ptrtoint (ptr getelementptr (i32, ptr null, i32 1) to i64)
  %2 = call ptr @realloc(ptr %data, i64 %mul13)
  %field.ptr = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  store ptr %2, ptr %field.ptr, align 8
  %n14 = load i32, ptr %n, align 4
  %field.ptr15 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 2
  store i32 %n14, ptr %field.ptr15, align 4
  ret void
}

define void @"Vec(int).__from_list"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %x = alloca i32, align 4
  store i32 %1, ptr %x, align 4
  %x1 = load i32, ptr %x, align 4
  call void @"Vec(int).push"(ptr %0, i32 %x1)
  ret void
}

define %"Vec(int)" @"Vec(int).copy"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  %sl.tmp = alloca %"Vec(int)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(int)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(int)" zeroinitializer, ptr %out, align 8
  store %"Vec(int)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(int)", ptr %sl.tmp, align 8
  store %"Vec(int)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @"Vec(int).reserve"(ptr %out, i32 %len)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field2 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %slt = icmp slt i32 %i1, %len3
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field4 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %i5 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i5 to i64
  %ptr.elem.ptr = getelementptr i32, ptr %data, i64 %lp.idx
  %dup.src = load i32, ptr %ptr.elem.ptr, align 4
  call void @"Vec(int).push"(ptr %out, i32 %dup.src)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i6 = load i32, ptr %i, align 4
  %add = add nsw i32 %i6, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %out7 = load %"Vec(int)", ptr %out, align 8
  ret %"Vec(int)" %out7
}

define %"Vec(int)" @"Vec(int).__clone"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %call = call %"Vec(int)" @"Vec(int).copy"(ptr %0)
  ret %"Vec(int)" %call
}

define void @"Vec(int).__drop"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %slt = icmp slt i32 %i1, %len
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field2 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %i3 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i3 to i64
  %ptr.elem.ptr = getelementptr i32, ptr %data, i64 %lp.idx
  br label %for.update

for.update:                                       ; preds = %for.body
  %i4 = load i32, ptr %i, align 4
  %add = add nsw i32 %i4, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %field5 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field5, align 4
  %sgt = icmp sgt i32 %cap, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %for.end
  %field6 = getelementptr inbounds %"Vec(int)", ptr %0, i32 0, i32 0
  %data7 = load ptr, ptr %field6, align 8
  call void @free(ptr %data7)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %for.end
  ret void
}

define void @"Vec(std_core_str_core__Str).reserve"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %n = alloca i32, align 4
  %need = alloca i32, align 4
  store i32 %1, ptr %need, align 4
  %need1 = load i32, ptr %need, align 4
  %field = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sle = icmp sle i32 %need1, %cap
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret void

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 2
  %cap3 = load i32, ptr %field2, align 4
  store i32 %cap3, ptr %n, align 4
  %n4 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %n4, 4
  br i1 %slt, label %if.then5, label %if.merge6

if.then5:                                         ; preds = %if.merge
  store i32 4, ptr %n, align 4
  br label %if.merge6

if.merge6:                                        ; preds = %if.then5, %if.merge
  br label %while.cond

while.cond:                                       ; preds = %while.body, %if.merge6
  %n7 = load i32, ptr %n, align 4
  %need8 = load i32, ptr %need, align 4
  %slt9 = icmp slt i32 %n7, %need8
  br i1 %slt9, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %n10 = load i32, ptr %n, align 4
  %mul = mul nsw i32 %n10, 2
  store i32 %mul, ptr %n, align 4
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %field11 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field11, align 8
  %n12 = load i32, ptr %n, align 4
  %widen.sext = sext i32 %n12 to i64
  %mul13 = mul nsw i64 %widen.sext, ptrtoint (ptr getelementptr (%std_core_str_core__Str, ptr null, i32 1) to i64)
  %2 = call ptr @realloc(ptr %data, i64 %mul13)
  %field.ptr = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  store ptr %2, ptr %field.ptr, align 8
  %n14 = load i32, ptr %n, align 4
  %field.ptr15 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 2
  store i32 %n14, ptr %field.ptr15, align 4
  ret void
}

define void @"Vec(std_core_str_core__Str).__from_list"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, %std_core_str_core__Str %1) {
entry:
  %param.moved = alloca i1, align 1
  %x = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str %1, ptr %x, align 8
  store i1 false, ptr %param.moved, align 1
  %x1 = load %std_core_str_core__Str, ptr %x, align 8
  store i1 true, ptr %param.moved, align 1
  call void @"Vec(std_core_str_core__Str).push"(ptr %0, %std_core_str_core__Str %x1)
  br label %cleanup

cleanup:                                          ; preds = %entry
  %drop.flag = load i1, ptr %param.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  ret void

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %x)
  br label %drop.skip0
}

define %"Vec(std_core_str_core__Str)" @"Vec(std_core_str_core__Str).copy"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %uc.self = alloca %std_core_str_core__Str, align 8
  %i = alloca i32, align 4
  %sl.tmp = alloca %"Vec(std_core_str_core__Str)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(std_core_str_core__Str)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %out, align 8
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(std_core_str_core__Str)", ptr %sl.tmp, align 8
  store %"Vec(std_core_str_core__Str)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @"Vec(std_core_str_core__Str).reserve"(ptr %out, i32 %len)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %slt = icmp slt i32 %i1, %len3
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field4 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %i5 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i5 to i64
  %ptr.elem.ptr = getelementptr %std_core_str_core__Str, ptr %data, i64 %lp.idx
  %dup.src = load %std_core_str_core__Str, ptr %ptr.elem.ptr, align 8
  store %std_core_str_core__Str %dup.src, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  call void @"Vec(std_core_str_core__Str).push"(ptr %out, %std_core_str_core__Str %uc.r)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i6 = load i32, ptr %i, align 4
  %add = add nsw i32 %i6, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %out7 = load %"Vec(std_core_str_core__Str)", ptr %out, align 8
  ret %"Vec(std_core_str_core__Str)" %out7
}

define %"Vec(std_core_str_core__Str)" @"Vec(std_core_str_core__Str).__clone"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %call = call %"Vec(std_core_str_core__Str)" @"Vec(std_core_str_core__Str).copy"(ptr %0)
  ret %"Vec(std_core_str_core__Str)" %call
}

define void @"Vec(std_core_str_core__Str).__drop"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %slt = icmp slt i32 %i1, %len
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field2 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %i3 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i3 to i64
  %ptr.elem.ptr = getelementptr %std_core_str_core__Str, ptr %data, i64 %lp.idx
  call void @std_core_str_core__Str.__drop(ptr %ptr.elem.ptr)
  br label %for.update

for.update:                                       ; preds = %for.body
  %i4 = load i32, ptr %i, align 4
  %add = add nsw i32 %i4, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %field5 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field5, align 4
  %sgt = icmp sgt i32 %cap, 0
  br i1 %sgt, label %if.then, label %if.merge

if.then:                                          ; preds = %for.end
  %field6 = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 0
  %data7 = load ptr, ptr %field6, align 8
  call void @free(ptr %data7)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %for.end
  ret void
}

declare void @__ls_set_args(i32 %0, ptr %1)

declare void @__ls_flush_out()

attributes #0 = { cold noreturn }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!0 = !{i8 0, i8 3}
