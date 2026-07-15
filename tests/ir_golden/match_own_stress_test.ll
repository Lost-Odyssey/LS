; ModuleID = 'samples/match_own_stress_test.lls'
source_filename = "samples/match_own_stress_test.lls"
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
%"Result(std_core_str_core__Str,std_core_str_core__Str)" = type { i8, [2 x i64] }
%"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" = type { i8, [2 x i64] }
%"Option(Carton)" = type { i8, [5 x i64] }
%Carton = type { %std_core_str_core__Str, %"Result(std_core_str_core__Str,std_core_str_core__Str)" }
%"Option(Block() -> int)" = type { i8, [2 x i64] }
%"Vec(Block() -> int)" = type { ptr, i32, i32 }

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
@.ls.strlit.84 = private unnamed_addr constant [7 x i8] c"even: \00", align 1
@.ls.strlit.85 = private unnamed_addr constant [13 x i8] c"heap payload\00", align 1
@.ls.strlit.86 = private unnamed_addr constant [6 x i8] c"odd: \00", align 1
@.ls.strlit.87 = private unnamed_addr constant [19 x i8] c"heap error payload\00", align 1
@.ls.strlit.88 = private unnamed_addr constant [6 x i8] c"neg: \00", align 1
@.ls.strlit.89 = private unnamed_addr constant [7 x i8] c"no vec\00", align 1
@fstr.fmt = private unnamed_addr constant [9 x i8] c"elem %d \00", align 1
@.ls.strlit.90 = private unnamed_addr constant [21 x i8] c"padded to force heap\00", align 1
@.ls.strlit.91 = private unnamed_addr constant [32 x i8] c"default after non-returning arm\00", align 1
@.ls.strlit.92 = private unnamed_addr constant [30 x i8] c"FAIL: bare_vec unexpected err\00", align 1
@.ls.fmt.93 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.94 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.95 = private unnamed_addr constant [31 x i8] c"alpha (outer local kept alive)\00", align 1
@.ls.strlit.96 = private unnamed_addr constant [17 x i8] c"beta block tail \00", align 1
@.ls.strlit.97 = private unnamed_addr constant [5 x i8] c"heap\00", align 1
@fstr.fmt.98 = private unnamed_addr constant [35 x i8] c"other %d padded to spill onto heap\00", align 1
@.ls.strlit.99 = private unnamed_addr constant [28 x i8] c"FAIL: outer local corrupted\00", align 1
@.ls.fmt.100 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.101 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.102 = private unnamed_addr constant [10 x i8] c"cond one \00", align 1
@.ls.strlit.103 = private unnamed_addr constant [5 x i8] c"heap\00", align 1
@fstr.fmt.104 = private unnamed_addr constant [23 x i8] c"cond two-and-a-half %f\00", align 1
@.ls.strlit.105 = private unnamed_addr constant [11 x i8] c" after try\00", align 1
@fstr.fmt.106 = private unnamed_addr constant [11 x i8] c"carton %d \00", align 1
@.ls.strlit.107 = private unnamed_addr constant [9 x i8] c"heap tag\00", align 1
@.ls.strlit.108 = private unnamed_addr constant [27 x i8] c"FAIL: binder tag corrupted\00", align 1
@.ls.fmt.109 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.110 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@fstr.fmt.111 = private unnamed_addr constant [8 x i8] c"none %d\00", align 1
@fstr.fmt.112 = private unnamed_addr constant [10 x i8] c"churn %d \00", align 1
@.ls.strlit.113 = private unnamed_addr constant [33 x i8] c"0123456789abcdef0123456789abcdef\00", align 1
@fuw.fmt = private unnamed_addr constant [87 x i8] c"[unwrap] %d:%d: unwrap failed: expected Some, got None (type: Option(Block() -> int))\0A\00", align 1
@fuw.fmt.114 = private unnamed_addr constant [87 x i8] c"[unwrap] %d:%d: unwrap failed: expected Some, got None (type: Option(Block() -> int))\0A\00", align 1
@.ls.strlit.115 = private unnamed_addr constant [13 x i8] c"FAIL: nested\00", align 1
@.ls.fmt.116 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.117 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.118 = private unnamed_addr constant [16 x i8] c"FAIL: loop_drop\00", align 1
@.ls.fmt.119 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.120 = private unnamed_addr constant [5 x i8] c" %d\0A\00", align 1
@.ls.strlit.121 = private unnamed_addr constant [12 x i8] c"FAIL: early\00", align 1
@.ls.fmt.122 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.123 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.124 = private unnamed_addr constant [11 x i8] c"FAIL: pick\00", align 1
@.ls.fmt.125 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.126 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.127 = private unnamed_addr constant [22 x i8] c"fallback heap string \00", align 1
@.ls.strlit.128 = private unnamed_addr constant [7 x i8] c"padded\00", align 1
@.ls.strlit.129 = private unnamed_addr constant [13 x i8] c"FAIL: pick_f\00", align 1
@.ls.fmt.130 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.131 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.132 = private unnamed_addr constant [24 x i8] c"FAIL: fallback consumed\00", align 1
@.ls.fmt.133 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.134 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@fstr.fmt.135 = private unnamed_addr constant [15 x i8] c"BAD ok path %d\00", align 1
@fstr.fmt.136 = private unnamed_addr constant [16 x i8] c"BAD err path %d\00", align 1
@fstr.fmt.137 = private unnamed_addr constant [20 x i8] c"BAD arm-err path %d\00", align 1
@.ls.strlit.138 = private unnamed_addr constant [5 x i8] c"even\00", align 1
@.ls.strlit.139 = private unnamed_addr constant [20 x i8] c"FAIL: try_in_arm ok\00", align 1
@.ls.fmt.140 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.141 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.142 = private unnamed_addr constant [4 x i8] c"odd\00", align 1
@.ls.strlit.143 = private unnamed_addr constant [21 x i8] c"FAIL: try_in_arm err\00", align 1
@.ls.fmt.144 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.145 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.146 = private unnamed_addr constant [4 x i8] c"odd\00", align 1
@.ls.strlit.147 = private unnamed_addr constant [25 x i8] c"FAIL: try_in_arm arm-err\00", align 1
@.ls.fmt.148 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.149 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.150 = private unnamed_addr constant [5 x i8] c"even\00", align 1
@.ls.strlit.151 = private unnamed_addr constant [22 x i8] c"FAIL: binder_field ok\00", align 1
@.ls.fmt.152 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.153 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.154 = private unnamed_addr constant [4 x i8] c"odd\00", align 1
@.ls.strlit.155 = private unnamed_addr constant [23 x i8] c"FAIL: binder_field err\00", align 1
@.ls.fmt.156 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.157 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.158 = private unnamed_addr constant [5 x i8] c"none\00", align 1
@.ls.strlit.159 = private unnamed_addr constant [24 x i8] c"FAIL: binder_field none\00", align 1
@.ls.fmt.160 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.161 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@.ls.strlit.162 = private unnamed_addr constant [21 x i8] c"FAIL: binder_move ok\00", align 1
@.ls.fmt.163 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.164 = private unnamed_addr constant [5 x i8] c" %d\0A\00", align 1
@.ls.strlit.165 = private unnamed_addr constant [22 x i8] c"FAIL: binder_move err\00", align 1
@.ls.fmt.166 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.167 = private unnamed_addr constant [5 x i8] c" %d\0A\00", align 1
@.ls.strlit.168 = private unnamed_addr constant [18 x i8] c"FAIL: deep_blocks\00", align 1
@.ls.fmt.169 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.170 = private unnamed_addr constant [5 x i8] c" %d\0A\00", align 1
@.ls.strlit.171 = private unnamed_addr constant [17 x i8] c"MATCHSTRESS PASS\00", align 1
@.ls.fmt.172 = private unnamed_addr constant [5 x i8] c"%.*s\00", align 1
@.ls.fmt.173 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

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

define void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 0, label %drop.case
    i8 1, label %drop.case1
  ]

drop.end:                                         ; preds = %drop.case1, %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.end

drop.case1:                                       ; preds = %entry
  %drop.field2 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field2)
  br label %drop.end
}

define %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %0) {
entry:
  %enum.ctor5 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %struct.borrow.tmp4 = alloca %std_core_str_core__Str, align 8
  %var.moved3 = alloca i1, align 1
  %e = alloca %std_core_str_core__Str, align 8
  %enum.ctor = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %s = alloca %std_core_str_core__Str, align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %srem = srem i32 %i1, 2
  %eq = icmp eq i32 %srem, 0
  br i1 %eq, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %s)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %s, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.84, i32 6, i32 0 }, ptr %s, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.85, i32 12, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %s, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %s2 = load %std_core_str_core__Str, ptr %s, align 8
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  store %std_core_str_core__Str %s2, ptr %field.p, align 8
  store i1 true, ptr %var.moved, align 1
  %enum.val = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, align 8
  br label %cleanup

if.merge:                                         ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %e)
  store i1 false, ptr %var.moved3, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %e, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.86, i32 5, i32 0 }, ptr %e, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.87, i32 18, i32 0 }, ptr %struct.borrow.tmp4, align 8
  call void @std_core_str_core__Str.push_str(ptr %e, ptr %struct.borrow.tmp4)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp4)
  %2 = call ptr @memset(ptr %enum.ctor5, i32 0, i64 24)
  %disc.p6 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor5, i32 0, i32 0
  store i8 1, ptr %disc.p6, align 1
  %payload.p7 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor5, i32 0, i32 1
  %e8 = load %std_core_str_core__Str, ptr %e, align 8
  %field.p9 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p7, i32 0, i32 0
  store %std_core_str_core__Str %e8, ptr %field.p9, align 8
  store i1 true, ptr %var.moved3, align 1
  %enum.val10 = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor5, align 8
  br label %cleanup11

cleanup:                                          ; preds = %if.then
  %drop.flag = load i1, ptr %var.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %s)
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %enum.val

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %s)
  br label %drop.skip0

cleanup11:                                        ; preds = %if.merge
  %drop.flag14 = load i1, ptr %var.moved3, align 1
  br i1 %drop.flag14, label %drop.skip012, label %drop.call013

drop.skip012:                                     ; preds = %drop.call013, %cleanup11
  call void @llvm.lifetime.end.p0(i64 16, ptr %e)
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %enum.val10

drop.call013:                                     ; preds = %cleanup11
  call void @std_core_str_core__Str.__drop(ptr %e)
  br label %drop.skip012
}

define void @"Result(Vec(std_core_str_core__Str),std_core_str_core__Str).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 0, label %drop.case
    i8 1, label %drop.case1
  ]

drop.end:                                         ; preds = %drop.case1, %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %"Vec(std_core_str_core__Str)" }, ptr %payload.p, i32 0, i32 0
  call void @"Vec(std_core_str_core__Str).__drop"(ptr %drop.field)
  br label %drop.end

drop.case1:                                       ; preds = %entry
  %drop.field2 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field2)
  br label %drop.end
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

define %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" @mk_vec(i32 %0) {
entry:
  %enum.ctor14 = alloca %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", align 8
  %struct.borrow.tmp8 = alloca %std_core_str_core__Str, align 8
  %fstr.tmp = alloca i8, i32 256, align 1
  %var.moved5 = alloca i1, align 1
  %s = alloca %std_core_str_core__Str, align 8
  %i = alloca i32, align 4
  %var.moved3 = alloca i1, align 1
  %v = alloca %"Vec(std_core_str_core__Str)", align 8
  %enum.ctor = alloca %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %e = alloca %std_core_str_core__Str, align 8
  %n = alloca i32, align 4
  store i32 %0, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %slt = icmp slt i32 %n1, 0
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %e)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %e, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.88, i32 5, i32 0 }, ptr %e, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.89, i32 6, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %e, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 1, ptr %disc.p, align 1
  %payload.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %e2 = load %std_core_str_core__Str, ptr %e, align 8
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  store %std_core_str_core__Str %e2, ptr %field.p, align 8
  store i1 true, ptr %var.moved, align 1
  %enum.val = load %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor, align 8
  br label %cleanup

if.merge:                                         ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %v)
  store i1 false, ptr %var.moved3, align 1
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %v, align 8
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %v, align 8
  %n4 = load i32, ptr %n, align 4
  store i32 0, ptr %i, align 4
  br label %foreach.cond

cleanup:                                          ; preds = %if.then
  %drop.flag = load i1, ptr %var.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %e)
  ret %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" %enum.val

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %e)
  br label %drop.skip0

foreach.cond:                                     ; preds = %foreach.update, %if.merge
  %cur = load i32, ptr %i, align 4
  %foreach.lt = icmp slt i32 %cur, %n4
  br i1 %foreach.lt, label %foreach.body, label %foreach.end

foreach.body:                                     ; preds = %foreach.cond
  call void @llvm.lifetime.start.p0(i64 16, ptr %s)
  store i1 false, ptr %var.moved5, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %s, align 8
  %i6 = load i32, ptr %i, align 4
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt, i32 %i6)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits7, label %fstr.big

foreach.update:                                   ; preds = %drop.skip011
  %cur2 = load i32, ptr %i, align 4
  %next = add i32 %cur2, 1
  store i32 %next, ptr %i, align 4
  br label %foreach.cond

foreach.end:                                      ; preds = %foreach.cond
  %2 = call ptr @memset(ptr %enum.ctor14, i32 0, i64 24)
  %disc.p15 = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor14, i32 0, i32 0
  store i8 0, ptr %disc.p15, align 1
  %payload.p16 = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor14, i32 0, i32 1
  %v17 = load %"Vec(std_core_str_core__Str)", ptr %v, align 8
  %field.p18 = getelementptr inbounds { %"Vec(std_core_str_core__Str)" }, ptr %payload.p16, i32 0, i32 0
  store %"Vec(std_core_str_core__Str)" %v17, ptr %field.p18, align 8
  store i1 true, ptr %var.moved3, align 1
  %enum.val19 = load %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %enum.ctor14, align 8
  br label %cleanup20

fstr.fits7:                                       ; preds = %foreach.body
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %foreach.body
  %3 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt, i32 %i6)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits7
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %s, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.90, i32 20, i32 0 }, ptr %struct.borrow.tmp8, align 8
  call void @std_core_str_core__Str.push_str(ptr %s, ptr %struct.borrow.tmp8)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp8)
  %s9 = load %std_core_str_core__Str, ptr %s, align 8
  store i1 true, ptr %var.moved5, align 1
  call void @"Vec(std_core_str_core__Str).push"(ptr %v, %std_core_str_core__Str %s9)
  br label %cleanup10

cleanup10:                                        ; preds = %fstr.done
  %drop.flag13 = load i1, ptr %var.moved5, align 1
  br i1 %drop.flag13, label %drop.skip011, label %drop.call012

drop.skip011:                                     ; preds = %drop.call012, %cleanup10
  call void @llvm.lifetime.end.p0(i64 16, ptr %s)
  br label %foreach.update

drop.call012:                                     ; preds = %cleanup10
  call void @std_core_str_core__Str.__drop(ptr %s)
  br label %drop.skip011

cleanup20:                                        ; preds = %foreach.end
  %drop.flag23 = load i1, ptr %var.moved3, align 1
  br i1 %drop.flag23, label %drop.skip021, label %drop.call022

drop.skip021:                                     ; preds = %drop.call022, %cleanup20
  call void @llvm.lifetime.end.p0(i64 16, ptr %v)
  ret %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" %enum.val19

drop.call022:                                     ; preds = %cleanup20
  call void @"Vec(std_core_str_core__Str).__drop"(ptr %v)
  br label %drop.skip021
}

define %std_core_str_core__Str @nested(i32 %0) {
entry:
  %binder.moved32 = alloca i1, align 1
  %e31 = alloca %std_core_str_core__Str, align 8
  %uc.self29 = alloca %std_core_str_core__Str, align 8
  %binder.moved24 = alloca i1, align 1
  %e23 = alloca %std_core_str_core__Str, align 8
  %uc.self21 = alloca %std_core_str_core__Str, align 8
  %binder.moved17 = alloca i1, align 1
  %t16 = alloca %std_core_str_core__Str, align 8
  %uc.self14 = alloca %std_core_str_core__Str, align 8
  %match.subj7 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res6 = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res6, align 8
  %binder.moved = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res, align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case26
  ]

match.end:                                        ; preds = %match.case26, %drop.cont
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p34 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p34, align 1
  %match.val35 = load %std_core_str_core__Str, ptr %match.res, align 8
  ret %std_core_str_core__Str %match.val35

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %s2, align 8
  store i1 false, ptr %binder.moved, align 1
  %i3 = load i32, ptr %i, align 4
  %add = add nsw i32 %i3, 1
  %call4 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %add)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call4, ptr %match.subj7, align 8
  %disc.p8 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj7, i32 0, i32 0
  %disc9 = load i8, ptr %disc.p8, align 1, !range !1
  %payload.p10 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj7, i32 0, i32 1
  switch i8 %disc9, label %match.default11 [
    i8 0, label %match.case12
    i8 1, label %match.case19
  ]

match.end5:                                       ; preds = %match.case19, %match.case12
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj7)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj7, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  %match.val = load %std_core_str_core__Str, ptr %match.res6, align 8
  store %std_core_str_core__Str %match.val, ptr %match.res, align 8
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip, label %drop.call

match.default11:                                  ; preds = %match.case
  unreachable

match.case12:                                     ; preds = %match.case
  %binder.p13 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p10, i32 0, i32 0
  %t = load %std_core_str_core__Str, ptr %binder.p13, align 8
  store %std_core_str_core__Str %t, ptr %uc.self14, align 8
  %uc.r15 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self14)
  store %std_core_str_core__Str %uc.r15, ptr %t16, align 8
  store i1 false, ptr %binder.moved17, align 1
  %t18 = load %std_core_str_core__Str, ptr %t16, align 8
  store %std_core_str_core__Str %t18, ptr %match.res6, align 8
  br label %match.end5

match.case19:                                     ; preds = %match.case
  %binder.p20 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p10, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p20, align 8
  store %std_core_str_core__Str %e, ptr %uc.self21, align 8
  %uc.r22 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self21)
  store %std_core_str_core__Str %uc.r22, ptr %e23, align 8
  store i1 false, ptr %binder.moved24, align 1
  %e25 = load %std_core_str_core__Str, ptr %e23, align 8
  store %std_core_str_core__Str %e25, ptr %match.res6, align 8
  br label %match.end5

drop.call:                                        ; preds = %match.end5
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.cont

drop.skip:                                        ; preds = %match.end5
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end

match.case26:                                     ; preds = %entry
  %binder.p27 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e28 = load %std_core_str_core__Str, ptr %binder.p27, align 8
  store %std_core_str_core__Str %e28, ptr %uc.self29, align 8
  %uc.r30 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self29)
  store %std_core_str_core__Str %uc.r30, ptr %e31, align 8
  store i1 false, ptr %binder.moved32, align 1
  %e33 = load %std_core_str_core__Str, ptr %e31, align 8
  store %std_core_str_core__Str %e33, ptr %match.res, align 8
  br label %match.end
}

define i32 @loop_drop() {
entry:
  %binder.moved10 = alloca i1, align 1
  %e9 = alloca %std_core_str_core__Str, align 8
  %uc.self7 = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %i = alloca i32, align 4
  %n = alloca i32, align 4
  store i32 0, ptr %n, align 4
  store i32 0, ptr %i, align 4
  br label %foreach.cond

foreach.cond:                                     ; preds = %foreach.update, %entry
  %cur = load i32, ptr %i, align 4
  %foreach.lt = icmp slt i32 %cur, 50
  br i1 %foreach.lt, label %foreach.body, label %foreach.end

foreach.body:                                     ; preds = %foreach.cond
  %i1 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case5
  ]

foreach.update:                                   ; preds = %match.end
  %cur2 = load i32, ptr %i, align 4
  %next = add i32 %cur2, 1
  store i32 %next, ptr %i, align 4
  br label %foreach.cond

foreach.end:                                      ; preds = %foreach.cond
  %n17 = load i32, ptr %n, align 4
  ret i32 %n17

match.end:                                        ; preds = %drop.cont15, %drop.cont
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  br label %foreach.update

match.default:                                    ; preds = %foreach.body
  unreachable

match.case:                                       ; preds = %foreach.body
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %s2, align 8
  store i1 false, ptr %binder.moved, align 1
  %n3 = load i32, ptr %n, align 4
  %call4 = call i32 @std_core_str_core__Str.len(ptr %s2)
  %add = add nsw i32 %n3, %call4
  store i32 %add, ptr %n, align 4
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %match.case
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.cont

drop.skip:                                        ; preds = %match.case
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end

match.case5:                                      ; preds = %foreach.body
  %binder.p6 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p6, align 8
  store %std_core_str_core__Str %e, ptr %uc.self7, align 8
  %uc.r8 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self7)
  store %std_core_str_core__Str %uc.r8, ptr %e9, align 8
  store i1 false, ptr %binder.moved10, align 1
  %n11 = load i32, ptr %n, align 4
  %add12 = add nsw i32 %n11, 1
  store i32 %add12, ptr %n, align 4
  %drop.flag16 = load i1, ptr %binder.moved10, align 1
  br i1 %drop.flag16, label %drop.skip14, label %drop.call13

drop.call13:                                      ; preds = %match.case5
  call void @std_core_str_core__Str.__drop(ptr %e9)
  br label %drop.cont15

drop.skip14:                                      ; preds = %match.case5
  br label %drop.cont15

drop.cont15:                                      ; preds = %drop.call13, %drop.skip14
  br label %match.end
}

define %std_core_str_core__Str @early(i32 %0) {
entry:
  %var.moved = alloca i1, align 1
  %d = alloca %std_core_str_core__Str, align 8
  %binder.moved9 = alloca i1, align 1
  %e8 = alloca %std_core_str_core__Str, align 8
  %uc.self6 = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case4
  ]

match.end:                                        ; preds = %drop.cont
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p10 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p10, align 1
  call void @llvm.lifetime.start.p0(i64 16, ptr %d)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %d, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.91, i32 31, i32 0 }, ptr %d, align 8
  %d11 = load %std_core_str_core__Str, ptr %d, align 8
  ret %std_core_str_core__Str %d11

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %s2, align 8
  store i1 false, ptr %binder.moved, align 1
  %s3 = load %std_core_str_core__Str, ptr %s2, align 8
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  ret %std_core_str_core__Str %s3

match.case4:                                      ; preds = %entry
  %binder.p5 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p5, align 8
  store %std_core_str_core__Str %e, ptr %uc.self6, align 8
  %uc.r7 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self6)
  store %std_core_str_core__Str %uc.r7, ptr %e8, align 8
  store i1 false, ptr %binder.moved9, align 1
  %drop.flag = load i1, ptr %binder.moved9, align 1
  br i1 %drop.flag, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %match.case4
  call void @std_core_str_core__Str.__drop(ptr %e8)
  br label %drop.cont

drop.skip:                                        ; preds = %match.case4
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end
}

define void @bare_vec() {
entry:
  %binder.moved7 = alloca i1, align 1
  %e6 = alloca %std_core_str_core__Str, align 8
  %uc.self4 = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %v1 = alloca %"Vec(std_core_str_core__Str)", align 8
  %uc.self = alloca %"Vec(std_core_str_core__Str)", align 8
  %match.subj = alloca %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", align 8
  %call = call %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" @mk_vec(i32 4)
  store %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case2
  ]

match.end:                                        ; preds = %drop.cont
  call void @"Result(Vec(std_core_str_core__Str),std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p9 = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p9, align 1
  ret void

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %"Vec(std_core_str_core__Str)" }, ptr %payload.p, i32 0, i32 0
  %v = load %"Vec(std_core_str_core__Str)", ptr %binder.p, align 8
  store %"Vec(std_core_str_core__Str)" %v, ptr %uc.self, align 8
  %uc.r = call %"Vec(std_core_str_core__Str)" @"Vec(std_core_str_core__Str).__clone"(ptr %uc.self)
  store %"Vec(std_core_str_core__Str)" %uc.r, ptr %v1, align 8
  store i1 false, ptr %binder.moved, align 1
  call void @"Result(Vec(std_core_str_core__Str),std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(Vec(std_core_str_core__Str),std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  br label %cleanup

cleanup:                                          ; preds = %match.case
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  ret void

drop.call0:                                       ; preds = %cleanup
  call void @"Vec(std_core_str_core__Str).__drop"(ptr %v1)
  br label %drop.skip0

match.case2:                                      ; preds = %entry
  %binder.p3 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p3, align 8
  store %std_core_str_core__Str %e, ptr %uc.self4, align 8
  %uc.r5 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self4)
  store %std_core_str_core__Str %uc.r5, ptr %e6, align 8
  store i1 false, ptr %binder.moved7, align 1
  %0 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.93, i32 29, ptr @.ls.strlit.92)
  %1 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.94)
  %drop.flag8 = load i1, ptr %binder.moved7, align 1
  br i1 %drop.flag8, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %match.case2
  call void @std_core_str_core__Str.__drop(ptr %e6)
  br label %drop.cont

drop.skip:                                        ; preds = %match.case2
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end
}

define %std_core_str_core__Str @pick(i32 %0) {
entry:
  %fstr.tmp = alloca i8, i32 256, align 1
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved5 = alloca i1, align 1
  %b = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.res = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res, align 8
  %var.moved1 = alloca i1, align 1
  %r = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %a = alloca %std_core_str_core__Str, align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  call void @llvm.lifetime.start.p0(i64 16, ptr %a)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %a, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.95, i32 30, i32 0 }, ptr %a, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %r)
  store i1 false, ptr %var.moved1, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %r, align 8
  %i2 = load i32, ptr %i, align 4
  switch i32 %i2, label %match.default [
    i32 0, label %match.case
    i32 1, label %match.case4
    i32 2, label %match.case4
  ]

match.end:                                        ; preds = %fstr.done, %match.case4, %match.case
  %match.val = load %std_core_str_core__Str, ptr %match.res, align 8
  store %std_core_str_core__Str %match.val, ptr %r, align 8
  %call = call i32 @std_core_str_core__Str.len(ptr %a)
  %slt = icmp slt i32 %call, 5
  br i1 %slt, label %if.then, label %if.merge

match.default:                                    ; preds = %entry
  %i7 = load i32, ptr %i, align 4
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.98, i32 %i7)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits8, label %fstr.big

match.case:                                       ; preds = %entry
  %a3 = load %std_core_str_core__Str, ptr %a, align 8
  store %std_core_str_core__Str %a3, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %match.res, align 8
  br label %match.end

match.case4:                                      ; preds = %entry, %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %b)
  store i1 false, ptr %var.moved5, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %b, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.96, i32 16, i32 0 }, ptr %b, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.97, i32 4, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %b, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %b6 = load %std_core_str_core__Str, ptr %b, align 8
  store %std_core_str_core__Str %b6, ptr %match.res, align 8
  br label %match.end

fstr.fits8:                                       ; preds = %match.default
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %match.default
  %1 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt.98, i32 %i7)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits8
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %match.res, align 8
  br label %match.end

if.then:                                          ; preds = %match.end
  %2 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.100, i32 27, ptr @.ls.strlit.99)
  %3 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.101)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %match.end
  %r9 = load %std_core_str_core__Str, ptr %r, align 8
  br label %cleanup

cleanup:                                          ; preds = %if.merge
  %drop.flag = load i1, ptr %var.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %a)
  ret %std_core_str_core__Str %r9

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %a)
  br label %drop.skip0
}

define %std_core_str_core__Str @pick_f(double %0, %std_core_str_core__Str %1) {
entry:
  %uc.self = alloca %std_core_str_core__Str, align 8
  %fstr.tmp = alloca i8, i32 256, align 1
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %v = alloca %std_core_str_core__Str, align 8
  %match.res = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res, align 8
  %param.moved = alloca i1, align 1
  %fallback = alloca %std_core_str_core__Str, align 8
  %x = alloca double, align 8
  store double %0, ptr %x, align 8
  store %std_core_str_core__Str %1, ptr %fallback, align 8
  store i1 false, ptr %param.moved, align 1
  %x1 = load double, ptr %x, align 8
  %match.cmp = fcmp oeq double %x1, 1.000000e+00
  br i1 %match.cmp, label %match.then, label %match.next

match.end:                                        ; preds = %match.next4, %fstr.done, %match.then
  %match.val = load %std_core_str_core__Str, ptr %match.res, align 8
  br label %cleanup

match.then:                                       ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %v)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %v, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.102, i32 9, i32 0 }, ptr %v, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.103, i32 4, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %v, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %v2 = load %std_core_str_core__Str, ptr %v, align 8
  store %std_core_str_core__Str %v2, ptr %match.res, align 8
  br label %match.end

match.next:                                       ; preds = %entry
  %match.cmp5 = fcmp oeq double %x1, 2.500000e+00
  br i1 %match.cmp5, label %match.then3, label %match.next4

match.then3:                                      ; preds = %match.next
  %x6 = load double, ptr %x, align 8
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.104, double %x6)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits7, label %fstr.big

match.next4:                                      ; preds = %match.next
  %fallback8 = load %std_core_str_core__Str, ptr %fallback, align 8
  store %std_core_str_core__Str %fallback8, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %match.res, align 8
  br label %match.end

fstr.fits7:                                       ; preds = %match.then3
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %match.then3
  %2 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt.104, double %x6)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits7
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %match.res, align 8
  br label %match.end

cleanup:                                          ; preds = %match.end
  %drop.flag = load i1, ptr %param.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  ret %std_core_str_core__Str %match.val

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %fallback)
  br label %drop.skip0
}

define %"Result(std_core_str_core__Str,std_core_str_core__Str)" @try_in_arm(i32 %0, i32 %1) {
entry:
  %enum.ctor20 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %binder.moved19 = alloca i1, align 1
  %e18 = alloca %std_core_str_core__Str, align 8
  %uc.self16 = alloca %std_core_str_core__Str, align 8
  %enum.ctor = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %try.inner = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %try.ret = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %try.unwrapped = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %t = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %j = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  store i32 %1, ptr %j, align 4
  %i1 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case14
  ]

match.end:                                        ; No predecessors!
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p31 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p31, align 1
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" zeroinitializer

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %s2, align 8
  store i1 false, ptr %binder.moved, align 1
  call void @llvm.lifetime.start.p0(i64 16, ptr %t)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t, align 8
  %j3 = load i32, ptr %j, align 4
  %call4 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %j3)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call4, ptr %try.inner, align 8
  %try.disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.inner, i32 0, i32 0
  %try.disc = load i8, ptr %try.disc.p, align 1, !range !1
  %try.is_ok = icmp eq i8 %try.disc, 0
  br i1 %try.is_ok, label %try.ok, label %try.err

try.ok:                                           ; preds = %match.case
  %try.in.payload = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.inner, i32 0, i32 1
  %try.ok.field = getelementptr inbounds { %std_core_str_core__Str }, ptr %try.in.payload, i32 0, i32 0
  %try.ok.val = load %std_core_str_core__Str, ptr %try.ok.field, align 8
  store %std_core_str_core__Str %try.ok.val, ptr %try.unwrapped, align 8
  br label %try.merge

try.err:                                          ; preds = %match.case
  %2 = call ptr @memset(ptr %try.ret, i32 0, i64 24)
  %try.ret.disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.ret, i32 0, i32 0
  store i8 1, ptr %try.ret.disc.p, align 1
  %try.err.in.payload = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.inner, i32 0, i32 1
  %try.err.out.payload = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.ret, i32 0, i32 1
  %3 = call ptr @memcpy(ptr %try.err.out.payload, ptr %try.err.in.payload, i64 16)
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  br label %cleanup

try.merge:                                        ; preds = %try.ok
  %try.val = load %std_core_str_core__Str, ptr %try.unwrapped, align 8
  store %std_core_str_core__Str %try.val, ptr %t, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.105, i32 10, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %t, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %4 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p5 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p5, align 1
  %payload.p6 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, i32 0, i32 1
  %t7 = load %std_core_str_core__Str, ptr %t, align 8
  %field.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p6, i32 0, i32 0
  store %std_core_str_core__Str %t7, ptr %field.p, align 8
  store i1 true, ptr %var.moved, align 1
  %enum.val = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor, align 8
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p8 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p8, align 1
  br label %cleanup9

cleanup:                                          ; preds = %try.err
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  %try.ret.val = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %try.ret, align 8
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %try.ret.val

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip0

cleanup9:                                         ; preds = %try.merge
  %drop.flag12 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag12, label %drop.skip010, label %drop.call011

drop.skip010:                                     ; preds = %drop.call011, %cleanup9
  %drop.flag13 = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag13, label %drop.skip1, label %drop.call1

drop.call011:                                     ; preds = %cleanup9
  call void @std_core_str_core__Str.__drop(ptr %t)
  br label %drop.skip010

drop.skip1:                                       ; preds = %drop.call1, %drop.skip010
  call void @llvm.lifetime.end.p0(i64 16, ptr %t)
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %enum.val

drop.call1:                                       ; preds = %drop.skip010
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip1

match.case14:                                     ; preds = %entry
  %binder.p15 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p15, align 8
  store %std_core_str_core__Str %e, ptr %uc.self16, align 8
  %uc.r17 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self16)
  store %std_core_str_core__Str %uc.r17, ptr %e18, align 8
  store i1 false, ptr %binder.moved19, align 1
  %5 = call ptr @memset(ptr %enum.ctor20, i32 0, i64 24)
  %disc.p21 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor20, i32 0, i32 0
  store i8 1, ptr %disc.p21, align 1
  %payload.p22 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor20, i32 0, i32 1
  %e23 = load %std_core_str_core__Str, ptr %e18, align 8
  %field.p24 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p22, i32 0, i32 0
  store %std_core_str_core__Str %e23, ptr %field.p24, align 8
  store i1 true, ptr %binder.moved19, align 1
  %enum.val25 = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %enum.ctor20, align 8
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p26 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p26, align 1
  br label %cleanup27

cleanup27:                                        ; preds = %match.case14
  %drop.flag30 = load i1, ptr %binder.moved19, align 1
  br i1 %drop.flag30, label %drop.skip028, label %drop.call029

drop.skip028:                                     ; preds = %drop.call029, %cleanup27
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %enum.val25

drop.call029:                                     ; preds = %cleanup27
  call void @std_core_str_core__Str.__drop(ptr %e18)
  br label %drop.skip028
}

define void @"Option(Carton).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Option(Carton)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Option(Carton)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %drop.case, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { %Carton }, ptr %payload.p, i32 0, i32 0
  call void @Carton.__drop(ptr %drop.field)
  br label %drop.end
}

define void @Carton.__drop(ptr %self) {
entry:
  %drop.enomfield = getelementptr inbounds %Carton, ptr %self, i32 0, i32 1
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %drop.enomfield)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %drop.enomfield, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  %drop.field = getelementptr inbounds %Carton, ptr %self, i32 0, i32 0
  call void @std_core_str_core__Str.__drop(ptr %drop.field)
  br label %drop.next

drop.next:                                        ; preds = %entry
  ret void
}

define %"Option(Carton)" @mk_carton(i32 %0) {
entry:
  %sl.tmp = alloca %Carton, align 8
  %enum.ctor4 = alloca %"Option(Carton)", align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %fstr.tmp = alloca i8, i32 256, align 1
  %var.moved = alloca i1, align 1
  %t = alloca %std_core_str_core__Str, align 8
  %enum.ctor = alloca %"Option(Carton)", align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %slt = icmp slt i32 %i1, 0
  br i1 %slt, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  %1 = call ptr @memset(ptr %enum.ctor, i32 0, i64 48)
  %disc.p = getelementptr inbounds %"Option(Carton)", ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p, align 1
  %enum.val = load %"Option(Carton)", ptr %enum.ctor, align 8
  ret %"Option(Carton)" %enum.val

if.merge:                                         ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 16, ptr %t)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t, align 8
  %i2 = load i32, ptr %i, align 4
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.106, i32 %i2)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits3, label %fstr.big

fstr.fits3:                                       ; preds = %if.merge
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %if.merge
  %2 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt.106, i32 %i2)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits3
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %t, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.107, i32 8, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %t, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  %3 = call ptr @memset(ptr %enum.ctor4, i32 0, i64 48)
  %disc.p5 = getelementptr inbounds %"Option(Carton)", ptr %enum.ctor4, i32 0, i32 0
  store i8 1, ptr %disc.p5, align 1
  %payload.p = getelementptr inbounds %"Option(Carton)", ptr %enum.ctor4, i32 0, i32 1
  store %Carton zeroinitializer, ptr %sl.tmp, align 8
  %t6 = load %std_core_str_core__Str, ptr %t, align 8
  %field_ptr = getelementptr inbounds %Carton, ptr %sl.tmp, i32 0, i32 0
  store %std_core_str_core__Str %t6, ptr %field_ptr, align 8
  store i1 true, ptr %var.moved, align 1
  %i7 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i7)
  %field_ptr8 = getelementptr inbounds %Carton, ptr %sl.tmp, i32 0, i32 1
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %field_ptr8, align 8
  %sl.val = load %Carton, ptr %sl.tmp, align 8
  %field.p = getelementptr inbounds { %Carton }, ptr %payload.p, i32 0, i32 0
  store %Carton %sl.val, ptr %field.p, align 8
  %enum.val9 = load %"Option(Carton)", ptr %enum.ctor4, align 8
  br label %cleanup

cleanup:                                          ; preds = %fstr.done
  %drop.flag = load i1, ptr %var.moved, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %t)
  ret %"Option(Carton)" %enum.val9

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %t)
  br label %drop.skip0
}

define %std_core_str_core__Str @binder_field_subject(i32 %0) {
entry:
  %fstr.tmp = alloca i8, i32 256, align 1
  %binder.moved26 = alloca i1, align 1
  %e25 = alloca %std_core_str_core__Str, align 8
  %uc.self23 = alloca %std_core_str_core__Str, align 8
  %binder.moved19 = alloca i1, align 1
  %s18 = alloca %std_core_str_core__Str, align 8
  %uc.self16 = alloca %std_core_str_core__Str, align 8
  %match.subj9 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res8 = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res8, align 8
  %ec.tmp5 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %var.moved = alloca i1, align 1
  %r = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %b4 = alloca %Carton, align 8
  %ec.tmp = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Option(Carton)", align 8
  %match.res = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res, align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %call = call %"Option(Carton)" @mk_carton(i32 %i1)
  store %"Option(Carton)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Option(Carton)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Option(Carton)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 1, label %match.case
    i8 0, label %match.case30
  ]

match.end:                                        ; preds = %fstr.done, %drop.cont
  call void @"Option(Carton).__drop"(ptr %match.subj)
  %dead.tag.p33 = getelementptr inbounds %"Option(Carton)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p33, align 1
  %match.val34 = load %std_core_str_core__Str, ptr %match.res, align 8
  ret %std_core_str_core__Str %match.val34

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %Carton }, ptr %payload.p, i32 0, i32 0
  %b = load %Carton, ptr %binder.p, align 8
  %sc.fld = extractvalue %Carton %b, 0
  store %std_core_str_core__Str %sc.fld, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  %sc.ins = insertvalue %Carton %b, %std_core_str_core__Str %uc.r, 0
  %sc.fld2 = extractvalue %Carton %sc.ins, 1
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %sc.fld2, ptr %ec.tmp, align 8
  %ec.r = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @"Result(std_core_str_core__Str,std_core_str_core__Str).__clone"(ptr %ec.tmp)
  %sc.ins3 = insertvalue %Carton %sc.ins, %"Result(std_core_str_core__Str,std_core_str_core__Str)" %ec.r, 1
  store %Carton %sc.ins3, ptr %b4, align 8
  store i1 false, ptr %binder.moved, align 1
  call void @llvm.lifetime.start.p0(i64 16, ptr %r)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %r, align 8
  %field = getelementptr inbounds %Carton, ptr %b4, i32 0, i32 1
  %inner = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %field, align 8
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %inner, ptr %ec.tmp5, align 8
  %ec.r6 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @"Result(std_core_str_core__Str,std_core_str_core__Str).__clone"(ptr %ec.tmp5)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %ec.r6, ptr %match.subj9, align 8
  %disc.p10 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj9, i32 0, i32 0
  %disc11 = load i8, ptr %disc.p10, align 1, !range !1
  %payload.p12 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj9, i32 0, i32 1
  switch i8 %disc11, label %match.default13 [
    i8 0, label %match.case14
    i8 1, label %match.case21
  ]

match.end7:                                       ; preds = %match.case21, %match.case14
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj9)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj9, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  %match.val = load %std_core_str_core__Str, ptr %match.res8, align 8
  store %std_core_str_core__Str %match.val, ptr %r, align 8
  %field.addr = getelementptr inbounds %Carton, ptr %b4, i32 0, i32 0
  %call28 = call i32 @std_core_str_core__Str.len(ptr %field.addr)
  %slt = icmp slt i32 %call28, 5
  br i1 %slt, label %if.then, label %if.merge

match.default13:                                  ; preds = %match.case
  unreachable

match.case14:                                     ; preds = %match.case
  %binder.p15 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p12, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p15, align 8
  store %std_core_str_core__Str %s, ptr %uc.self16, align 8
  %uc.r17 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self16)
  store %std_core_str_core__Str %uc.r17, ptr %s18, align 8
  store i1 false, ptr %binder.moved19, align 1
  %s20 = load %std_core_str_core__Str, ptr %s18, align 8
  store %std_core_str_core__Str %s20, ptr %match.res8, align 8
  br label %match.end7

match.case21:                                     ; preds = %match.case
  %binder.p22 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p12, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p22, align 8
  store %std_core_str_core__Str %e, ptr %uc.self23, align 8
  %uc.r24 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self23)
  store %std_core_str_core__Str %uc.r24, ptr %e25, align 8
  store i1 false, ptr %binder.moved26, align 1
  %e27 = load %std_core_str_core__Str, ptr %e25, align 8
  store %std_core_str_core__Str %e27, ptr %match.res8, align 8
  br label %match.end7

if.then:                                          ; preds = %match.end7
  %1 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.109, i32 26, ptr @.ls.strlit.108)
  %2 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.110)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %match.end7
  %r29 = load %std_core_str_core__Str, ptr %r, align 8
  store %std_core_str_core__Str %r29, ptr %match.res, align 8
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %if.merge
  call void @Carton.__drop(ptr %b4)
  br label %drop.cont

drop.skip:                                        ; preds = %if.merge
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end

match.case30:                                     ; preds = %entry
  %i31 = load i32, ptr %i, align 4
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.111, i32 %i31)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits32, label %fstr.big

fstr.fits32:                                      ; preds = %match.case30
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %match.case30
  %3 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt.111, i32 %i31)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits32
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %match.res, align 8
  br label %match.end
}

define i32 @binder_move(i32 %0) {
entry:
  %binder.moved11 = alloca i1, align 1
  %e10 = alloca %std_core_str_core__Str, align 8
  %uc.self8 = alloca %std_core_str_core__Str, align 8
  %uc.self4 = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %n = alloca i32, align 4
  %var.moved = alloca i1, align 1
  %v = alloca %"Vec(std_core_str_core__Str)", align 8
  %i = alloca i32, align 4
  store i32 %0, ptr %i, align 4
  call void @llvm.lifetime.start.p0(i64 16, ptr %v)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %v, align 8
  store %"Vec(std_core_str_core__Str)" zeroinitializer, ptr %v, align 8
  store i32 0, ptr %n, align 4
  %i1 = load i32, ptr %i, align 4
  %call = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @mk(i32 %i1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case6
  ]

match.end:                                        ; preds = %drop.cont15, %drop.cont
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  %n17 = load i32, ptr %n, align 4
  %call18 = call i32 @"Vec(std_core_str_core__Str).len"(ptr %v)
  %add = add nsw i32 %n17, %call18
  br label %cleanup

match.default:                                    ; preds = %entry
  unreachable

match.case:                                       ; preds = %entry
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %s2, align 8
  store i1 false, ptr %binder.moved, align 1
  %s3 = load %std_core_str_core__Str, ptr %s2, align 8
  store %std_core_str_core__Str %s3, ptr %uc.self4, align 8
  %uc.r5 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self4)
  call void @"Vec(std_core_str_core__Str).push"(ptr %v, %std_core_str_core__Str %uc.r5)
  store i32 100, ptr %n, align 4
  %drop.flag = load i1, ptr %binder.moved, align 1
  br i1 %drop.flag, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %match.case
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.cont

drop.skip:                                        ; preds = %match.case
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end

match.case6:                                      ; preds = %entry
  %binder.p7 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p7, align 8
  store %std_core_str_core__Str %e, ptr %uc.self8, align 8
  %uc.r9 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self8)
  store %std_core_str_core__Str %uc.r9, ptr %e10, align 8
  store i1 false, ptr %binder.moved11, align 1
  %call12 = call i32 @std_core_str_core__Str.len(ptr %e10)
  store i32 %call12, ptr %n, align 4
  %drop.flag16 = load i1, ptr %binder.moved11, align 1
  br i1 %drop.flag16, label %drop.skip14, label %drop.call13

drop.call13:                                      ; preds = %match.case6
  call void @std_core_str_core__Str.__drop(ptr %e10)
  br label %drop.cont15

drop.skip14:                                      ; preds = %match.case6
  br label %drop.cont15

drop.cont15:                                      ; preds = %drop.call13, %drop.skip14
  br label %match.end

cleanup:                                          ; preds = %match.end
  %drop.flag19 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag19, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %v)
  ret i32 %add

drop.call0:                                       ; preds = %cleanup
  call void @"Vec(std_core_str_core__Str).__drop"(ptr %v)
  br label %drop.skip0
}

define i32 @deep_blocks() {
entry:
  %blk.rval.tmp126 = alloca { ptr, ptr }, align 8
  %fuw.inner114 = alloca %"Option(Block() -> int)", align 8
  %fuw.val115 = alloca { ptr, ptr }, align 8
  %g1 = alloca { ptr, ptr }, align 8
  %blk.rval.tmp = alloca { ptr, ptr }, align 8
  %fuw.inner = alloca %"Option(Block() -> int)", align 8
  %fuw.val = alloca { ptr, ptr }, align 8
  %g0 = alloca { ptr, ptr }, align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %fstr.tmp = alloca i8, i32 256, align 1
  %var.moved108 = alloca i1, align 1
  %junk = alloca %std_core_str_core__Str, align 8
  %i = alloca i32, align 4
  %var.moved = alloca i1, align 1
  %v = alloca %"Vec(Block() -> int)", align 8
  %blk.lit.tmp84 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp84, align 8
  %b6 = alloca { ptr, ptr }, align 8
  %blk.lit.tmp65 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp65, align 8
  %b5 = alloca { ptr, ptr }, align 8
  %blk.lit.tmp46 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp46, align 8
  %b4 = alloca { ptr, ptr }, align 8
  %blk.lit.tmp27 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp27, align 8
  %b3 = alloca { ptr, ptr }, align 8
  %blk.lit.tmp8 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp8, align 8
  %b2 = alloca { ptr, ptr }, align 8
  %blk.lit.tmp = alloca { ptr, ptr }, align 8
  store { ptr, ptr } zeroinitializer, ptr %blk.lit.tmp, align 8
  %b1 = alloca { ptr, ptr }, align 8
  %base = alloca i32, align 4
  store i32 1, ptr %base, align 4
  call void @llvm.lifetime.start.p0(i64 16, ptr %b1)
  %cap.load = load i32, ptr %base, align 4
  %p = call ptr @malloc(i64 32)
  %env.dropslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 0
  store ptr null, ptr %env.dropslot, align 8
  %env.cloneslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 1
  store ptr @__env_clone_0, ptr %env.cloneslot, align 8
  %env.rcslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 2
  store i64 1, ptr %env.rcslot, align 8
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 3
  store i32 %cap.load, ptr %cap.slot, align 4
  %blk.env = insertvalue { ptr, ptr } { ptr @__closure_0, ptr undef }, ptr %p, 1
  store { ptr, ptr } %blk.env, ptr %blk.lit.tmp, align 8
  store { ptr, ptr } %blk.env, ptr %b1, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %b2)
  %cap.load1 = load { ptr, ptr }, ptr %b1, align 8
  %p2 = call ptr @malloc(i64 40)
  %env.dropslot3 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p2, i32 0, i32 0
  store ptr @__env_drop_1, ptr %env.dropslot3, align 8
  %env.cloneslot4 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p2, i32 0, i32 1
  store ptr @__env_clone_1, ptr %env.cloneslot4, align 8
  %env.rcslot5 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p2, i32 0, i32 2
  store i64 1, ptr %env.rcslot5, align 8
  %cap.slot6 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p2, i32 0, i32 3
  %bc.fn = extractvalue { ptr, ptr } %cap.load1, 0
  %bc.env = extractvalue { ptr, ptr } %cap.load1, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cap.slot6, align 8
  %blk.env7 = insertvalue { ptr, ptr } { ptr @__closure_1, ptr undef }, ptr %p2, 1
  store { ptr, ptr } %blk.env7, ptr %blk.lit.tmp8, align 8
  store { ptr, ptr } %blk.env7, ptr %b2, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %b3)
  %cap.load9 = load { ptr, ptr }, ptr %b2, align 8
  %p10 = call ptr @malloc(i64 40)
  %env.dropslot11 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p10, i32 0, i32 0
  store ptr @__env_drop_2, ptr %env.dropslot11, align 8
  %env.cloneslot12 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p10, i32 0, i32 1
  store ptr @__env_clone_2, ptr %env.cloneslot12, align 8
  %env.rcslot13 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p10, i32 0, i32 2
  store i64 1, ptr %env.rcslot13, align 8
  %cap.slot14 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p10, i32 0, i32 3
  %bc.fn15 = extractvalue { ptr, ptr } %cap.load9, 0
  %bc.env16 = extractvalue { ptr, ptr } %cap.load9, 1
  %bc.isnull17 = icmp eq ptr %bc.env16, null
  br i1 %bc.isnull17, label %bc.cont19, label %bc.clone18

bc.clone18:                                       ; preds = %bc.cont
  %bc.cfslot20 = getelementptr inbounds ptr, ptr %bc.env16, i64 1
  %bc.cf21 = load ptr, ptr %bc.cfslot20, align 8
  %bc.newenv22 = call ptr %bc.cf21(ptr %bc.env16)
  br label %bc.cont19

bc.cont19:                                        ; preds = %bc.clone18, %bc.cont
  %bc.envphi23 = phi ptr [ null, %bc.cont ], [ %bc.newenv22, %bc.clone18 ]
  %bc.rfn24 = insertvalue { ptr, ptr } undef, ptr %bc.fn15, 0
  %bc.renv25 = insertvalue { ptr, ptr } %bc.rfn24, ptr %bc.envphi23, 1
  store { ptr, ptr } %bc.renv25, ptr %cap.slot14, align 8
  %blk.env26 = insertvalue { ptr, ptr } { ptr @__closure_2, ptr undef }, ptr %p10, 1
  store { ptr, ptr } %blk.env26, ptr %blk.lit.tmp27, align 8
  store { ptr, ptr } %blk.env26, ptr %b3, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %b4)
  %cap.load28 = load { ptr, ptr }, ptr %b3, align 8
  %p29 = call ptr @malloc(i64 40)
  %env.dropslot30 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p29, i32 0, i32 0
  store ptr @__env_drop_3, ptr %env.dropslot30, align 8
  %env.cloneslot31 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p29, i32 0, i32 1
  store ptr @__env_clone_3, ptr %env.cloneslot31, align 8
  %env.rcslot32 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p29, i32 0, i32 2
  store i64 1, ptr %env.rcslot32, align 8
  %cap.slot33 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p29, i32 0, i32 3
  %bc.fn34 = extractvalue { ptr, ptr } %cap.load28, 0
  %bc.env35 = extractvalue { ptr, ptr } %cap.load28, 1
  %bc.isnull36 = icmp eq ptr %bc.env35, null
  br i1 %bc.isnull36, label %bc.cont38, label %bc.clone37

bc.clone37:                                       ; preds = %bc.cont19
  %bc.cfslot39 = getelementptr inbounds ptr, ptr %bc.env35, i64 1
  %bc.cf40 = load ptr, ptr %bc.cfslot39, align 8
  %bc.newenv41 = call ptr %bc.cf40(ptr %bc.env35)
  br label %bc.cont38

bc.cont38:                                        ; preds = %bc.clone37, %bc.cont19
  %bc.envphi42 = phi ptr [ null, %bc.cont19 ], [ %bc.newenv41, %bc.clone37 ]
  %bc.rfn43 = insertvalue { ptr, ptr } undef, ptr %bc.fn34, 0
  %bc.renv44 = insertvalue { ptr, ptr } %bc.rfn43, ptr %bc.envphi42, 1
  store { ptr, ptr } %bc.renv44, ptr %cap.slot33, align 8
  %blk.env45 = insertvalue { ptr, ptr } { ptr @__closure_3, ptr undef }, ptr %p29, 1
  store { ptr, ptr } %blk.env45, ptr %blk.lit.tmp46, align 8
  store { ptr, ptr } %blk.env45, ptr %b4, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %b5)
  %cap.load47 = load { ptr, ptr }, ptr %b4, align 8
  %p48 = call ptr @malloc(i64 40)
  %env.dropslot49 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p48, i32 0, i32 0
  store ptr @__env_drop_4, ptr %env.dropslot49, align 8
  %env.cloneslot50 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p48, i32 0, i32 1
  store ptr @__env_clone_4, ptr %env.cloneslot50, align 8
  %env.rcslot51 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p48, i32 0, i32 2
  store i64 1, ptr %env.rcslot51, align 8
  %cap.slot52 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p48, i32 0, i32 3
  %bc.fn53 = extractvalue { ptr, ptr } %cap.load47, 0
  %bc.env54 = extractvalue { ptr, ptr } %cap.load47, 1
  %bc.isnull55 = icmp eq ptr %bc.env54, null
  br i1 %bc.isnull55, label %bc.cont57, label %bc.clone56

bc.clone56:                                       ; preds = %bc.cont38
  %bc.cfslot58 = getelementptr inbounds ptr, ptr %bc.env54, i64 1
  %bc.cf59 = load ptr, ptr %bc.cfslot58, align 8
  %bc.newenv60 = call ptr %bc.cf59(ptr %bc.env54)
  br label %bc.cont57

bc.cont57:                                        ; preds = %bc.clone56, %bc.cont38
  %bc.envphi61 = phi ptr [ null, %bc.cont38 ], [ %bc.newenv60, %bc.clone56 ]
  %bc.rfn62 = insertvalue { ptr, ptr } undef, ptr %bc.fn53, 0
  %bc.renv63 = insertvalue { ptr, ptr } %bc.rfn62, ptr %bc.envphi61, 1
  store { ptr, ptr } %bc.renv63, ptr %cap.slot52, align 8
  %blk.env64 = insertvalue { ptr, ptr } { ptr @__closure_4, ptr undef }, ptr %p48, 1
  store { ptr, ptr } %blk.env64, ptr %blk.lit.tmp65, align 8
  store { ptr, ptr } %blk.env64, ptr %b5, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %b6)
  %cap.load66 = load { ptr, ptr }, ptr %b5, align 8
  %p67 = call ptr @malloc(i64 40)
  %env.dropslot68 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p67, i32 0, i32 0
  store ptr @__env_drop_5, ptr %env.dropslot68, align 8
  %env.cloneslot69 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p67, i32 0, i32 1
  store ptr @__env_clone_5, ptr %env.cloneslot69, align 8
  %env.rcslot70 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p67, i32 0, i32 2
  store i64 1, ptr %env.rcslot70, align 8
  %cap.slot71 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p67, i32 0, i32 3
  %bc.fn72 = extractvalue { ptr, ptr } %cap.load66, 0
  %bc.env73 = extractvalue { ptr, ptr } %cap.load66, 1
  %bc.isnull74 = icmp eq ptr %bc.env73, null
  br i1 %bc.isnull74, label %bc.cont76, label %bc.clone75

bc.clone75:                                       ; preds = %bc.cont57
  %bc.cfslot77 = getelementptr inbounds ptr, ptr %bc.env73, i64 1
  %bc.cf78 = load ptr, ptr %bc.cfslot77, align 8
  %bc.newenv79 = call ptr %bc.cf78(ptr %bc.env73)
  br label %bc.cont76

bc.cont76:                                        ; preds = %bc.clone75, %bc.cont57
  %bc.envphi80 = phi ptr [ null, %bc.cont57 ], [ %bc.newenv79, %bc.clone75 ]
  %bc.rfn81 = insertvalue { ptr, ptr } undef, ptr %bc.fn72, 0
  %bc.renv82 = insertvalue { ptr, ptr } %bc.rfn81, ptr %bc.envphi80, 1
  store { ptr, ptr } %bc.renv82, ptr %cap.slot71, align 8
  %blk.env83 = insertvalue { ptr, ptr } { ptr @__closure_5, ptr undef }, ptr %p67, 1
  store { ptr, ptr } %blk.env83, ptr %blk.lit.tmp84, align 8
  store { ptr, ptr } %blk.env83, ptr %b6, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %v)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(Block() -> int)" zeroinitializer, ptr %v, align 8
  store %"Vec(Block() -> int)" zeroinitializer, ptr %v, align 8
  %dup.src = load { ptr, ptr }, ptr %b3, align 8
  %bc.fn85 = extractvalue { ptr, ptr } %dup.src, 0
  %bc.env86 = extractvalue { ptr, ptr } %dup.src, 1
  %bc.isnull87 = icmp eq ptr %bc.env86, null
  br i1 %bc.isnull87, label %bc.cont89, label %bc.clone88

bc.clone88:                                       ; preds = %bc.cont76
  %bc.cfslot90 = getelementptr inbounds ptr, ptr %bc.env86, i64 1
  %bc.cf91 = load ptr, ptr %bc.cfslot90, align 8
  %bc.newenv92 = call ptr %bc.cf91(ptr %bc.env86)
  br label %bc.cont89

bc.cont89:                                        ; preds = %bc.clone88, %bc.cont76
  %bc.envphi93 = phi ptr [ null, %bc.cont76 ], [ %bc.newenv92, %bc.clone88 ]
  %bc.rfn94 = insertvalue { ptr, ptr } undef, ptr %bc.fn85, 0
  %bc.renv95 = insertvalue { ptr, ptr } %bc.rfn94, ptr %bc.envphi93, 1
  call void @"Vec(Block() -> int).push"(ptr %v, { ptr, ptr } %bc.renv95)
  %dup.src96 = load { ptr, ptr }, ptr %b6, align 8
  %bc.fn97 = extractvalue { ptr, ptr } %dup.src96, 0
  %bc.env98 = extractvalue { ptr, ptr } %dup.src96, 1
  %bc.isnull99 = icmp eq ptr %bc.env98, null
  br i1 %bc.isnull99, label %bc.cont101, label %bc.clone100

bc.clone100:                                      ; preds = %bc.cont89
  %bc.cfslot102 = getelementptr inbounds ptr, ptr %bc.env98, i64 1
  %bc.cf103 = load ptr, ptr %bc.cfslot102, align 8
  %bc.newenv104 = call ptr %bc.cf103(ptr %bc.env98)
  br label %bc.cont101

bc.cont101:                                       ; preds = %bc.clone100, %bc.cont89
  %bc.envphi105 = phi ptr [ null, %bc.cont89 ], [ %bc.newenv104, %bc.clone100 ]
  %bc.rfn106 = insertvalue { ptr, ptr } undef, ptr %bc.fn97, 0
  %bc.renv107 = insertvalue { ptr, ptr } %bc.rfn106, ptr %bc.envphi105, 1
  call void @"Vec(Block() -> int).push"(ptr %v, { ptr, ptr } %bc.renv107)
  store i32 0, ptr %i, align 4
  br label %foreach.cond

foreach.cond:                                     ; preds = %foreach.update, %bc.cont101
  %cur = load i32, ptr %i, align 4
  %foreach.lt = icmp slt i32 %cur, 64
  br i1 %foreach.lt, label %foreach.body, label %foreach.end

foreach.body:                                     ; preds = %foreach.cond
  call void @llvm.lifetime.start.p0(i64 16, ptr %junk)
  store i1 false, ptr %var.moved108, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %junk, align 8
  %i109 = load i32, ptr %i, align 4
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.112, i32 %i109)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p110 = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits111, label %fstr.big

foreach.update:                                   ; preds = %drop.skip0
  %cur2 = load i32, ptr %i, align 4
  %next = add i32 %cur2, 1
  store i32 %next, ptr %i, align 4
  br label %foreach.cond

foreach.end:                                      ; preds = %foreach.cond
  call void @llvm.lifetime.start.p0(i64 16, ptr %g0)
  %call = call %"Option(Block() -> int)" @"Vec(Block() -> int).get"(ptr %v, i32 0)
  store %"Option(Block() -> int)" %call, ptr %fuw.inner, align 8
  %fuw.disc.p = getelementptr inbounds %"Option(Block() -> int)", ptr %fuw.inner, i32 0, i32 0
  %fuw.disc = load i8, ptr %fuw.disc.p, align 1, !range !1
  %fuw.is_ok = icmp eq i8 %fuw.disc, 1
  br i1 %fuw.is_ok, label %fuw.ok, label %fuw.err

fstr.fits111:                                     ; preds = %foreach.body
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p110, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %foreach.body
  %0 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p110, i64 %fstr.cap64, ptr @fstr.fmt.112, i32 %i109)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits111
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p110, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %junk, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.113, i32 32, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %junk, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  br label %cleanup

cleanup:                                          ; preds = %fstr.done
  %drop.flag = load i1, ptr %var.moved108, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  call void @llvm.lifetime.end.p0(i64 16, ptr %junk)
  br label %foreach.update

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %junk)
  br label %drop.skip0

fuw.ok:                                           ; preds = %foreach.end
  %fuw.in.payload = getelementptr inbounds %"Option(Block() -> int)", ptr %fuw.inner, i32 0, i32 1
  %fuw.ok.field = getelementptr inbounds { { ptr, ptr } }, ptr %fuw.in.payload, i32 0, i32 0
  %fuw.ok.val = load { ptr, ptr }, ptr %fuw.ok.field, align 8
  store { ptr, ptr } %fuw.ok.val, ptr %fuw.val, align 8
  br label %fuw.merge

fuw.err:                                          ; preds = %foreach.end
  %1 = call i32 (ptr, ...) @printf(ptr @fuw.fmt, i32 208, i32 20)
  call void @__ls_proc_exit(i32 1)
  unreachable

fuw.merge:                                        ; preds = %fuw.ok
  %fuw.val112 = load { ptr, ptr }, ptr %fuw.val, align 8
  store { ptr, ptr } %fuw.val112, ptr %blk.rval.tmp, align 8
  store { ptr, ptr } %fuw.val112, ptr %g0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %g1)
  %call113 = call %"Option(Block() -> int)" @"Vec(Block() -> int).get"(ptr %v, i32 1)
  store %"Option(Block() -> int)" %call113, ptr %fuw.inner114, align 8
  %fuw.disc.p116 = getelementptr inbounds %"Option(Block() -> int)", ptr %fuw.inner114, i32 0, i32 0
  %fuw.disc117 = load i8, ptr %fuw.disc.p116, align 1, !range !1
  %fuw.is_ok118 = icmp eq i8 %fuw.disc117, 1
  br i1 %fuw.is_ok118, label %fuw.ok119, label %fuw.err120

fuw.ok119:                                        ; preds = %fuw.merge
  %fuw.in.payload122 = getelementptr inbounds %"Option(Block() -> int)", ptr %fuw.inner114, i32 0, i32 1
  %fuw.ok.field123 = getelementptr inbounds { { ptr, ptr } }, ptr %fuw.in.payload122, i32 0, i32 0
  %fuw.ok.val124 = load { ptr, ptr }, ptr %fuw.ok.field123, align 8
  store { ptr, ptr } %fuw.ok.val124, ptr %fuw.val115, align 8
  br label %fuw.merge121

fuw.err120:                                       ; preds = %fuw.merge
  %2 = call i32 (ptr, ...) @printf(ptr @fuw.fmt.114, i32 209, i32 20)
  call void @__ls_proc_exit(i32 1)
  unreachable

fuw.merge121:                                     ; preds = %fuw.ok119
  %fuw.val125 = load { ptr, ptr }, ptr %fuw.val115, align 8
  store { ptr, ptr } %fuw.val125, ptr %blk.rval.tmp126, align 8
  store { ptr, ptr } %fuw.val125, ptr %g1, align 8
  %b6127 = load { ptr, ptr }, ptr %b6, align 8
  %blk.fn = extractvalue { ptr, ptr } %b6127, 0
  %blk.env128 = extractvalue { ptr, ptr } %b6127, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env128)
  %g0129 = load { ptr, ptr }, ptr %g0, align 8
  %blk.fn130 = extractvalue { ptr, ptr } %g0129, 0
  %blk.env131 = extractvalue { ptr, ptr } %g0129, 1
  %blk.call132 = call i32 %blk.fn130(ptr %blk.env131)
  %add = add nsw i32 %blk.call, %blk.call132
  %g1133 = load { ptr, ptr }, ptr %g1, align 8
  %blk.fn134 = extractvalue { ptr, ptr } %g1133, 0
  %blk.env135 = extractvalue { ptr, ptr } %g1133, 1
  %blk.call136 = call i32 %blk.fn134(ptr %blk.env135)
  %add137 = add nsw i32 %add, %blk.call136
  br label %cleanup138

cleanup138:                                       ; preds = %fuw.merge121
  %blk.cleanup = load { ptr, ptr }, ptr %g1, align 8
  %blk.env.cleanup = extractvalue { ptr, ptr } %blk.cleanup, 1
  %blk.env.nn = icmp ne ptr %blk.env.cleanup, null
  br i1 %blk.env.nn, label %blk.maybe0, label %blk.cont0

blk.maybe0:                                       ; preds = %cleanup138
  %blk.drop = load ptr, ptr %blk.env.cleanup, align 8
  %blk.has_drop = icmp ne ptr %blk.drop, null
  br i1 %blk.has_drop, label %blk.dropcall0, label %blk.dofree0

blk.dropcall0:                                    ; preds = %blk.maybe0
  call void %blk.drop(ptr %blk.env.cleanup)
  br label %blk.dofree0

blk.dofree0:                                      ; preds = %blk.dropcall0, %blk.maybe0
  call void @free(ptr %blk.env.cleanup)
  br label %blk.cont0

blk.cont0:                                        ; preds = %blk.dofree0, %cleanup138
  %blk.cleanup139 = load { ptr, ptr }, ptr %g0, align 8
  %blk.env.cleanup140 = extractvalue { ptr, ptr } %blk.cleanup139, 1
  %blk.env.nn141 = icmp ne ptr %blk.env.cleanup140, null
  br i1 %blk.env.nn141, label %blk.maybe1, label %blk.cont1

blk.maybe1:                                       ; preds = %blk.cont0
  %blk.drop142 = load ptr, ptr %blk.env.cleanup140, align 8
  %blk.has_drop143 = icmp ne ptr %blk.drop142, null
  br i1 %blk.has_drop143, label %blk.dropcall1, label %blk.dofree1

blk.dropcall1:                                    ; preds = %blk.maybe1
  call void %blk.drop142(ptr %blk.env.cleanup140)
  br label %blk.dofree1

blk.dofree1:                                      ; preds = %blk.dropcall1, %blk.maybe1
  call void @free(ptr %blk.env.cleanup140)
  br label %blk.cont1

blk.cont1:                                        ; preds = %blk.dofree1, %blk.cont0
  %drop.flag144 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag144, label %drop.skip2, label %drop.call2

drop.skip2:                                       ; preds = %drop.call2, %blk.cont1
  %blk.cleanup145 = load { ptr, ptr }, ptr %b6, align 8
  %blk.env.cleanup146 = extractvalue { ptr, ptr } %blk.cleanup145, 1
  %blk.env.nn147 = icmp ne ptr %blk.env.cleanup146, null
  br i1 %blk.env.nn147, label %blk.maybe3, label %blk.cont3

drop.call2:                                       ; preds = %blk.cont1
  call void @"Vec(Block() -> int).__drop"(ptr %v)
  br label %drop.skip2

blk.maybe3:                                       ; preds = %drop.skip2
  %blk.drop148 = load ptr, ptr %blk.env.cleanup146, align 8
  %blk.has_drop149 = icmp ne ptr %blk.drop148, null
  br i1 %blk.has_drop149, label %blk.dropcall3, label %blk.dofree3

blk.dropcall3:                                    ; preds = %blk.maybe3
  call void %blk.drop148(ptr %blk.env.cleanup146)
  br label %blk.dofree3

blk.dofree3:                                      ; preds = %blk.dropcall3, %blk.maybe3
  call void @free(ptr %blk.env.cleanup146)
  br label %blk.cont3

blk.cont3:                                        ; preds = %blk.dofree3, %drop.skip2
  %blk.cleanup150 = load { ptr, ptr }, ptr %b5, align 8
  %blk.env.cleanup151 = extractvalue { ptr, ptr } %blk.cleanup150, 1
  %blk.env.nn152 = icmp ne ptr %blk.env.cleanup151, null
  br i1 %blk.env.nn152, label %blk.maybe4, label %blk.cont4

blk.maybe4:                                       ; preds = %blk.cont3
  %blk.drop153 = load ptr, ptr %blk.env.cleanup151, align 8
  %blk.has_drop154 = icmp ne ptr %blk.drop153, null
  br i1 %blk.has_drop154, label %blk.dropcall4, label %blk.dofree4

blk.dropcall4:                                    ; preds = %blk.maybe4
  call void %blk.drop153(ptr %blk.env.cleanup151)
  br label %blk.dofree4

blk.dofree4:                                      ; preds = %blk.dropcall4, %blk.maybe4
  call void @free(ptr %blk.env.cleanup151)
  br label %blk.cont4

blk.cont4:                                        ; preds = %blk.dofree4, %blk.cont3
  %blk.cleanup155 = load { ptr, ptr }, ptr %b4, align 8
  %blk.env.cleanup156 = extractvalue { ptr, ptr } %blk.cleanup155, 1
  %blk.env.nn157 = icmp ne ptr %blk.env.cleanup156, null
  br i1 %blk.env.nn157, label %blk.maybe5, label %blk.cont5

blk.maybe5:                                       ; preds = %blk.cont4
  %blk.drop158 = load ptr, ptr %blk.env.cleanup156, align 8
  %blk.has_drop159 = icmp ne ptr %blk.drop158, null
  br i1 %blk.has_drop159, label %blk.dropcall5, label %blk.dofree5

blk.dropcall5:                                    ; preds = %blk.maybe5
  call void %blk.drop158(ptr %blk.env.cleanup156)
  br label %blk.dofree5

blk.dofree5:                                      ; preds = %blk.dropcall5, %blk.maybe5
  call void @free(ptr %blk.env.cleanup156)
  br label %blk.cont5

blk.cont5:                                        ; preds = %blk.dofree5, %blk.cont4
  %blk.cleanup160 = load { ptr, ptr }, ptr %b3, align 8
  %blk.env.cleanup161 = extractvalue { ptr, ptr } %blk.cleanup160, 1
  %blk.env.nn162 = icmp ne ptr %blk.env.cleanup161, null
  br i1 %blk.env.nn162, label %blk.maybe6, label %blk.cont6

blk.maybe6:                                       ; preds = %blk.cont5
  %blk.drop163 = load ptr, ptr %blk.env.cleanup161, align 8
  %blk.has_drop164 = icmp ne ptr %blk.drop163, null
  br i1 %blk.has_drop164, label %blk.dropcall6, label %blk.dofree6

blk.dropcall6:                                    ; preds = %blk.maybe6
  call void %blk.drop163(ptr %blk.env.cleanup161)
  br label %blk.dofree6

blk.dofree6:                                      ; preds = %blk.dropcall6, %blk.maybe6
  call void @free(ptr %blk.env.cleanup161)
  br label %blk.cont6

blk.cont6:                                        ; preds = %blk.dofree6, %blk.cont5
  %blk.cleanup165 = load { ptr, ptr }, ptr %b2, align 8
  %blk.env.cleanup166 = extractvalue { ptr, ptr } %blk.cleanup165, 1
  %blk.env.nn167 = icmp ne ptr %blk.env.cleanup166, null
  br i1 %blk.env.nn167, label %blk.maybe7, label %blk.cont7

blk.maybe7:                                       ; preds = %blk.cont6
  %blk.drop168 = load ptr, ptr %blk.env.cleanup166, align 8
  %blk.has_drop169 = icmp ne ptr %blk.drop168, null
  br i1 %blk.has_drop169, label %blk.dropcall7, label %blk.dofree7

blk.dropcall7:                                    ; preds = %blk.maybe7
  call void %blk.drop168(ptr %blk.env.cleanup166)
  br label %blk.dofree7

blk.dofree7:                                      ; preds = %blk.dropcall7, %blk.maybe7
  call void @free(ptr %blk.env.cleanup166)
  br label %blk.cont7

blk.cont7:                                        ; preds = %blk.dofree7, %blk.cont6
  %blk.cleanup170 = load { ptr, ptr }, ptr %b1, align 8
  %blk.env.cleanup171 = extractvalue { ptr, ptr } %blk.cleanup170, 1
  %blk.env.nn172 = icmp ne ptr %blk.env.cleanup171, null
  br i1 %blk.env.nn172, label %blk.maybe8, label %blk.cont8

blk.maybe8:                                       ; preds = %blk.cont7
  %blk.drop173 = load ptr, ptr %blk.env.cleanup171, align 8
  %blk.has_drop174 = icmp ne ptr %blk.drop173, null
  br i1 %blk.has_drop174, label %blk.dropcall8, label %blk.dofree8

blk.dropcall8:                                    ; preds = %blk.maybe8
  call void %blk.drop173(ptr %blk.env.cleanup171)
  br label %blk.dofree8

blk.dofree8:                                      ; preds = %blk.dropcall8, %blk.maybe8
  call void @free(ptr %blk.env.cleanup171)
  br label %blk.cont8

blk.cont8:                                        ; preds = %blk.dofree8, %blk.cont7
  call void @llvm.lifetime.end.p0(i64 16, ptr %g1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %g0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %v)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b6)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b5)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b4)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b3)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %b1)
  ret i32 %add137
}

define i32 @main(i32 %0, ptr %1) {
entry:
  call void @__ls_set_args(i32 %0, ptr %1)
  call void @__ls_global_stmts()
  %db = alloca i32, align 4
  %bm1 = alloca i32, align 4
  %bm0 = alloca i32, align 4
  %struct.borrow.tmp521 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp464 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp413 = alloca %std_core_str_core__Str, align 8
  %var.moved411 = alloca i1, align 1
  %bfn = alloca %std_core_str_core__Str, align 8
  %var.moved409 = alloca i1, align 1
  %bf1 = alloca %std_core_str_core__Str, align 8
  %var.moved407 = alloca i1, align 1
  %bf0 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp359 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp311 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp270 = alloca %std_core_str_core__Str, align 8
  %binder.moved266 = alloca i1, align 1
  %e265 = alloca %std_core_str_core__Str, align 8
  %uc.self263 = alloca %std_core_str_core__Str, align 8
  %fstr.tmp244 = alloca i8, i32 256, align 1
  %binder.moved242 = alloca i1, align 1
  %s241 = alloca %std_core_str_core__Str, align 8
  %uc.self239 = alloca %std_core_str_core__Str, align 8
  %match.subj231 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res230 = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res230, align 8
  %var.moved227 = alloca i1, align 1
  %t_arm = alloca %std_core_str_core__Str, align 8
  %binder.moved223 = alloca i1, align 1
  %e222 = alloca %std_core_str_core__Str, align 8
  %uc.self220 = alloca %std_core_str_core__Str, align 8
  %fstr.tmp201 = alloca i8, i32 256, align 1
  %binder.moved199 = alloca i1, align 1
  %s198 = alloca %std_core_str_core__Str, align 8
  %uc.self196 = alloca %std_core_str_core__Str, align 8
  %match.subj188 = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res187 = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res187, align 8
  %var.moved184 = alloca i1, align 1
  %t_err = alloca %std_core_str_core__Str, align 8
  %fstr.tmp = alloca i8, i32 256, align 1
  %binder.moved180 = alloca i1, align 1
  %e179 = alloca %std_core_str_core__Str, align 8
  %uc.self177 = alloca %std_core_str_core__Str, align 8
  %binder.moved = alloca i1, align 1
  %s173 = alloca %std_core_str_core__Str, align 8
  %uc.self171 = alloca %std_core_str_core__Str, align 8
  %match.subj = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %match.res = alloca %std_core_str_core__Str, align 8
  store %std_core_str_core__Str zeroinitializer, ptr %match.res, align 8
  %var.moved169 = alloca i1, align 1
  %t_ok = alloca %std_core_str_core__Str, align 8
  %uc.self88 = alloca %std_core_str_core__Str, align 8
  %var.moved86 = alloca i1, align 1
  %s2 = alloca %std_core_str_core__Str, align 8
  %uc.self83 = alloca %std_core_str_core__Str, align 8
  %var.moved81 = alloca i1, align 1
  %s1 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %var.moved78 = alloca i1, align 1
  %s0 = alloca %std_core_str_core__Str, align 8
  %struct.borrow.tmp = alloca %std_core_str_core__Str, align 8
  %var.moved77 = alloca i1, align 1
  %fb = alloca %std_core_str_core__Str, align 8
  %var.moved45 = alloca i1, align 1
  %p2 = alloca %std_core_str_core__Str, align 8
  %var.moved43 = alloca i1, align 1
  %p1 = alloca %std_core_str_core__Str, align 8
  %var.moved41 = alloca i1, align 1
  %p0 = alloca %std_core_str_core__Str, align 8
  %var.moved21 = alloca i1, align 1
  %e1 = alloca %std_core_str_core__Str, align 8
  %var.moved19 = alloca i1, align 1
  %e0 = alloca %std_core_str_core__Str, align 8
  %l = alloca i32, align 4
  %var.moved1 = alloca i1, align 1
  %n1 = alloca %std_core_str_core__Str, align 8
  %var.moved = alloca i1, align 1
  %n0 = alloca %std_core_str_core__Str, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %n0)
  store i1 false, ptr %var.moved, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %n0, align 8
  %call = call %std_core_str_core__Str @nested(i32 0)
  store %std_core_str_core__Str %call, ptr %n0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %n1)
  store i1 false, ptr %var.moved1, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %n1, align 8
  %call2 = call %std_core_str_core__Str @nested(i32 1)
  store %std_core_str_core__Str %call2, ptr %n1, align 8
  %call3 = call i32 @std_core_str_core__Str.len(ptr %n0)
  %eq = icmp eq i32 %call3, 0
  br i1 %eq, label %sc.merge, label %sc.rhs

sc.rhs:                                           ; preds = %entry
  %call4 = call i32 @std_core_str_core__Str.len(ptr %n1)
  %eq5 = icmp eq i32 %call4, 0
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %entry
  %sc = phi i1 [ %eq, %entry ], [ %eq5, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %2 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.116, i32 12, ptr @.ls.strlit.115)
  %3 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.117)
  br label %cleanup

if.merge:                                         ; preds = %sc.merge
  %call7 = call i32 @loop_drop()
  store i32 %call7, ptr %l, align 4
  %l8 = load i32, ptr %l, align 4
  %slt = icmp slt i32 %l8, 50
  br i1 %slt, label %if.then9, label %if.merge10

cleanup:                                          ; preds = %if.then
  %drop.flag = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag, label %drop.skip0, label %drop.call0

drop.skip0:                                       ; preds = %drop.call0, %cleanup
  %drop.flag6 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag6, label %drop.skip1, label %drop.call1

drop.call0:                                       ; preds = %cleanup
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip0

drop.skip1:                                       ; preds = %drop.call1, %drop.skip0
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call1:                                       ; preds = %drop.skip0
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip1

if.then9:                                         ; preds = %if.merge
  %4 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.119, i32 15, ptr @.ls.strlit.118)
  %l11 = load i32, ptr %l, align 4
  %5 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.120, i32 %l11)
  br label %cleanup12

if.merge10:                                       ; preds = %if.merge
  call void @llvm.lifetime.start.p0(i64 16, ptr %e0)
  store i1 false, ptr %var.moved19, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %e0, align 8
  %call20 = call %std_core_str_core__Str @early(i32 0)
  store %std_core_str_core__Str %call20, ptr %e0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %e1)
  store i1 false, ptr %var.moved21, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %e1, align 8
  %call22 = call %std_core_str_core__Str @early(i32 1)
  store %std_core_str_core__Str %call22, ptr %e1, align 8
  %call23 = call i32 @std_core_str_core__Str.len(ptr %e0)
  %eq24 = icmp eq i32 %call23, 0
  br i1 %eq24, label %sc.merge26, label %sc.rhs25

cleanup12:                                        ; preds = %if.then9
  %drop.flag15 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag15, label %drop.skip013, label %drop.call014

drop.skip013:                                     ; preds = %drop.call014, %cleanup12
  %drop.flag18 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag18, label %drop.skip116, label %drop.call117

drop.call014:                                     ; preds = %cleanup12
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip013

drop.skip116:                                     ; preds = %drop.call117, %drop.skip013
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call117:                                     ; preds = %drop.skip013
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip116

sc.rhs25:                                         ; preds = %if.merge10
  %call27 = call i32 @std_core_str_core__Str.len(ptr %e1)
  %eq28 = icmp eq i32 %call27, 0
  br label %sc.merge26

sc.merge26:                                       ; preds = %sc.rhs25, %if.merge10
  %sc29 = phi i1 [ %eq24, %if.merge10 ], [ %eq28, %sc.rhs25 ]
  br i1 %sc29, label %if.then30, label %if.merge31

if.then30:                                        ; preds = %sc.merge26
  %6 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.122, i32 11, ptr @.ls.strlit.121)
  %7 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.123)
  br label %cleanup32

if.merge31:                                       ; preds = %sc.merge26
  call void @bare_vec()
  call void @llvm.lifetime.start.p0(i64 16, ptr %p0)
  store i1 false, ptr %var.moved41, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %p0, align 8
  %call42 = call %std_core_str_core__Str @pick(i32 0)
  store %std_core_str_core__Str %call42, ptr %p0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %p1)
  store i1 false, ptr %var.moved43, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %p1, align 8
  %call44 = call %std_core_str_core__Str @pick(i32 2)
  store %std_core_str_core__Str %call44, ptr %p1, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %p2)
  store i1 false, ptr %var.moved45, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %p2, align 8
  %call46 = call %std_core_str_core__Str @pick(i32 9)
  store %std_core_str_core__Str %call46, ptr %p2, align 8
  %call47 = call i32 @std_core_str_core__Str.len(ptr %p0)
  %eq48 = icmp eq i32 %call47, 0
  br i1 %eq48, label %sc.merge50, label %sc.rhs49

cleanup32:                                        ; preds = %if.then30
  %drop.flag35 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag35, label %drop.skip033, label %drop.call034

drop.skip033:                                     ; preds = %drop.call034, %cleanup32
  %drop.flag38 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag38, label %drop.skip136, label %drop.call137

drop.call034:                                     ; preds = %cleanup32
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip033

drop.skip136:                                     ; preds = %drop.call137, %drop.skip033
  %drop.flag39 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag39, label %drop.skip2, label %drop.call2

drop.call137:                                     ; preds = %drop.skip033
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip136

drop.skip2:                                       ; preds = %drop.call2, %drop.skip136
  %drop.flag40 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag40, label %drop.skip3, label %drop.call3

drop.call2:                                       ; preds = %drop.skip136
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip2

drop.skip3:                                       ; preds = %drop.call3, %drop.skip2
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call3:                                       ; preds = %drop.skip2
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip3

sc.rhs49:                                         ; preds = %if.merge31
  %call51 = call i32 @std_core_str_core__Str.len(ptr %p1)
  %eq52 = icmp eq i32 %call51, 0
  br label %sc.merge50

sc.merge50:                                       ; preds = %sc.rhs49, %if.merge31
  %sc53 = phi i1 [ %eq48, %if.merge31 ], [ %eq52, %sc.rhs49 ]
  br i1 %sc53, label %sc.merge55, label %sc.rhs54

sc.rhs54:                                         ; preds = %sc.merge50
  %call56 = call i32 @std_core_str_core__Str.len(ptr %p2)
  %eq57 = icmp eq i32 %call56, 0
  br label %sc.merge55

sc.merge55:                                       ; preds = %sc.rhs54, %sc.merge50
  %sc58 = phi i1 [ %sc53, %sc.merge50 ], [ %eq57, %sc.rhs54 ]
  br i1 %sc58, label %if.then59, label %if.merge60

if.then59:                                        ; preds = %sc.merge55
  %8 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.125, i32 10, ptr @.ls.strlit.124)
  %9 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.126)
  br label %cleanup61

if.merge60:                                       ; preds = %sc.merge55
  call void @llvm.lifetime.start.p0(i64 16, ptr %fb)
  store i1 false, ptr %var.moved77, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %fb, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.127, i32 21, i32 0 }, ptr %fb, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.128, i32 6, i32 0 }, ptr %struct.borrow.tmp, align 8
  call void @std_core_str_core__Str.push_str(ptr %fb, ptr %struct.borrow.tmp)
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp)
  call void @llvm.lifetime.start.p0(i64 16, ptr %s0)
  store i1 false, ptr %var.moved78, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %s0, align 8
  %fb79 = load %std_core_str_core__Str, ptr %fb, align 8
  store %std_core_str_core__Str %fb79, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  %call80 = call %std_core_str_core__Str @pick_f(double 1.000000e+00, %std_core_str_core__Str %uc.r)
  store %std_core_str_core__Str %call80, ptr %s0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %s1)
  store i1 false, ptr %var.moved81, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %s1, align 8
  %fb82 = load %std_core_str_core__Str, ptr %fb, align 8
  store %std_core_str_core__Str %fb82, ptr %uc.self83, align 8
  %uc.r84 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self83)
  %call85 = call %std_core_str_core__Str @pick_f(double 2.500000e+00, %std_core_str_core__Str %uc.r84)
  store %std_core_str_core__Str %call85, ptr %s1, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %s2)
  store i1 false, ptr %var.moved86, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %s2, align 8
  %fb87 = load %std_core_str_core__Str, ptr %fb, align 8
  store %std_core_str_core__Str %fb87, ptr %uc.self88, align 8
  %uc.r89 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self88)
  %call90 = call %std_core_str_core__Str @pick_f(double 9.000000e+00, %std_core_str_core__Str %uc.r89)
  store %std_core_str_core__Str %call90, ptr %s2, align 8
  %call91 = call i32 @std_core_str_core__Str.len(ptr %s0)
  %eq92 = icmp eq i32 %call91, 0
  br i1 %eq92, label %sc.merge94, label %sc.rhs93

cleanup61:                                        ; preds = %if.then59
  %drop.flag64 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag64, label %drop.skip062, label %drop.call063

drop.skip062:                                     ; preds = %drop.call063, %cleanup61
  %drop.flag67 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag67, label %drop.skip165, label %drop.call166

drop.call063:                                     ; preds = %cleanup61
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip062

drop.skip165:                                     ; preds = %drop.call166, %drop.skip062
  %drop.flag70 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag70, label %drop.skip268, label %drop.call269

drop.call166:                                     ; preds = %drop.skip062
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip165

drop.skip268:                                     ; preds = %drop.call269, %drop.skip165
  %drop.flag73 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag73, label %drop.skip371, label %drop.call372

drop.call269:                                     ; preds = %drop.skip165
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip268

drop.skip371:                                     ; preds = %drop.call372, %drop.skip268
  %drop.flag74 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag74, label %drop.skip4, label %drop.call4

drop.call372:                                     ; preds = %drop.skip268
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip371

drop.skip4:                                       ; preds = %drop.call4, %drop.skip371
  %drop.flag75 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag75, label %drop.skip5, label %drop.call5

drop.call4:                                       ; preds = %drop.skip371
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip4

drop.skip5:                                       ; preds = %drop.call5, %drop.skip4
  %drop.flag76 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag76, label %drop.skip6, label %drop.call6

drop.call5:                                       ; preds = %drop.skip4
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip5

drop.skip6:                                       ; preds = %drop.call6, %drop.skip5
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call6:                                       ; preds = %drop.skip5
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip6

sc.rhs93:                                         ; preds = %if.merge60
  %call95 = call i32 @std_core_str_core__Str.len(ptr %s1)
  %eq96 = icmp eq i32 %call95, 0
  br label %sc.merge94

sc.merge94:                                       ; preds = %sc.rhs93, %if.merge60
  %sc97 = phi i1 [ %eq92, %if.merge60 ], [ %eq96, %sc.rhs93 ]
  br i1 %sc97, label %sc.merge99, label %sc.rhs98

sc.rhs98:                                         ; preds = %sc.merge94
  %call100 = call i32 @std_core_str_core__Str.len(ptr %s2)
  %eq101 = icmp eq i32 %call100, 0
  br label %sc.merge99

sc.merge99:                                       ; preds = %sc.rhs98, %sc.merge94
  %sc102 = phi i1 [ %sc97, %sc.merge94 ], [ %eq101, %sc.rhs98 ]
  br i1 %sc102, label %if.then103, label %if.merge104

if.then103:                                       ; preds = %sc.merge99
  %10 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.130, i32 12, ptr @.ls.strlit.129)
  %11 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.131)
  br label %cleanup105

if.merge104:                                      ; preds = %sc.merge99
  %call131 = call i32 @std_core_str_core__Str.len(ptr %fb)
  %eq132 = icmp eq i32 %call131, 0
  br i1 %eq132, label %if.then133, label %if.merge134

cleanup105:                                       ; preds = %if.then103
  %drop.flag108 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag108, label %drop.skip0106, label %drop.call0107

drop.skip0106:                                    ; preds = %drop.call0107, %cleanup105
  %drop.flag111 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag111, label %drop.skip1109, label %drop.call1110

drop.call0107:                                    ; preds = %cleanup105
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip0106

drop.skip1109:                                    ; preds = %drop.call1110, %drop.skip0106
  %drop.flag114 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag114, label %drop.skip2112, label %drop.call2113

drop.call1110:                                    ; preds = %drop.skip0106
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip1109

drop.skip2112:                                    ; preds = %drop.call2113, %drop.skip1109
  %drop.flag117 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag117, label %drop.skip3115, label %drop.call3116

drop.call2113:                                    ; preds = %drop.skip1109
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip2112

drop.skip3115:                                    ; preds = %drop.call3116, %drop.skip2112
  %drop.flag120 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag120, label %drop.skip4118, label %drop.call4119

drop.call3116:                                    ; preds = %drop.skip2112
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip3115

drop.skip4118:                                    ; preds = %drop.call4119, %drop.skip3115
  %drop.flag123 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag123, label %drop.skip5121, label %drop.call5122

drop.call4119:                                    ; preds = %drop.skip3115
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip4118

drop.skip5121:                                    ; preds = %drop.call5122, %drop.skip4118
  %drop.flag126 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag126, label %drop.skip6124, label %drop.call6125

drop.call5122:                                    ; preds = %drop.skip4118
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip5121

drop.skip6124:                                    ; preds = %drop.call6125, %drop.skip5121
  %drop.flag127 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag127, label %drop.skip7, label %drop.call7

drop.call6125:                                    ; preds = %drop.skip5121
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip6124

drop.skip7:                                       ; preds = %drop.call7, %drop.skip6124
  %drop.flag128 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag128, label %drop.skip8, label %drop.call8

drop.call7:                                       ; preds = %drop.skip6124
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip7

drop.skip8:                                       ; preds = %drop.call8, %drop.skip7
  %drop.flag129 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag129, label %drop.skip9, label %drop.call9

drop.call8:                                       ; preds = %drop.skip7
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip8

drop.skip9:                                       ; preds = %drop.call9, %drop.skip8
  %drop.flag130 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag130, label %drop.skip10, label %drop.call10

drop.call9:                                       ; preds = %drop.skip8
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip9

drop.skip10:                                      ; preds = %drop.call10, %drop.skip9
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call10:                                      ; preds = %drop.skip9
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip10

if.then133:                                       ; preds = %if.merge104
  %12 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.133, i32 23, ptr @.ls.strlit.132)
  %13 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.134)
  br label %cleanup135

if.merge134:                                      ; preds = %if.merge104
  call void @llvm.lifetime.start.p0(i64 16, ptr %t_ok)
  store i1 false, ptr %var.moved169, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t_ok, align 8
  %call170 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @try_in_arm(i32 0, i32 2)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call170, ptr %match.subj, align 8
  %disc.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !1
  %payload.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 1
  switch i8 %disc, label %match.default [
    i8 0, label %match.case
    i8 1, label %match.case175
  ]

cleanup135:                                       ; preds = %if.then133
  %drop.flag138 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag138, label %drop.skip0136, label %drop.call0137

drop.skip0136:                                    ; preds = %drop.call0137, %cleanup135
  %drop.flag141 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag141, label %drop.skip1139, label %drop.call1140

drop.call0137:                                    ; preds = %cleanup135
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip0136

drop.skip1139:                                    ; preds = %drop.call1140, %drop.skip0136
  %drop.flag144 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag144, label %drop.skip2142, label %drop.call2143

drop.call1140:                                    ; preds = %drop.skip0136
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip1139

drop.skip2142:                                    ; preds = %drop.call2143, %drop.skip1139
  %drop.flag147 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag147, label %drop.skip3145, label %drop.call3146

drop.call2143:                                    ; preds = %drop.skip1139
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip2142

drop.skip3145:                                    ; preds = %drop.call3146, %drop.skip2142
  %drop.flag150 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag150, label %drop.skip4148, label %drop.call4149

drop.call3146:                                    ; preds = %drop.skip2142
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip3145

drop.skip4148:                                    ; preds = %drop.call4149, %drop.skip3145
  %drop.flag153 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag153, label %drop.skip5151, label %drop.call5152

drop.call4149:                                    ; preds = %drop.skip3145
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip4148

drop.skip5151:                                    ; preds = %drop.call5152, %drop.skip4148
  %drop.flag156 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag156, label %drop.skip6154, label %drop.call6155

drop.call5152:                                    ; preds = %drop.skip4148
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip5151

drop.skip6154:                                    ; preds = %drop.call6155, %drop.skip5151
  %drop.flag159 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag159, label %drop.skip7157, label %drop.call7158

drop.call6155:                                    ; preds = %drop.skip5151
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip6154

drop.skip7157:                                    ; preds = %drop.call7158, %drop.skip6154
  %drop.flag162 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag162, label %drop.skip8160, label %drop.call8161

drop.call7158:                                    ; preds = %drop.skip6154
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip7157

drop.skip8160:                                    ; preds = %drop.call8161, %drop.skip7157
  %drop.flag165 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag165, label %drop.skip9163, label %drop.call9164

drop.call8161:                                    ; preds = %drop.skip7157
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip8160

drop.skip9163:                                    ; preds = %drop.call9164, %drop.skip8160
  %drop.flag168 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag168, label %drop.skip10166, label %drop.call10167

drop.call9164:                                    ; preds = %drop.skip8160
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip9163

drop.skip10166:                                   ; preds = %drop.call10167, %drop.skip9163
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call10167:                                   ; preds = %drop.skip9163
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip10166

match.end:                                        ; preds = %drop.cont, %match.case
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj)
  %dead.tag.p = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj, i32 0, i32 0
  store i8 2, ptr %dead.tag.p, align 1
  %match.val = load %std_core_str_core__Str, ptr %match.res, align 8
  store %std_core_str_core__Str %match.val, ptr %t_ok, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %t_err)
  store i1 false, ptr %var.moved184, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t_err, align 8
  %call185 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @try_in_arm(i32 0, i32 1)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call185, ptr %match.subj188, align 8
  %disc.p189 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj188, i32 0, i32 0
  %disc190 = load i8, ptr %disc.p189, align 1, !range !1
  %payload.p191 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj188, i32 0, i32 1
  switch i8 %disc190, label %match.default192 [
    i8 0, label %match.case193
    i8 1, label %match.case217
  ]

match.default:                                    ; preds = %if.merge134
  unreachable

match.case:                                       ; preds = %if.merge134
  %binder.p = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %s = load %std_core_str_core__Str, ptr %binder.p, align 8
  store %std_core_str_core__Str %s, ptr %uc.self171, align 8
  %uc.r172 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self171)
  store %std_core_str_core__Str %uc.r172, ptr %s173, align 8
  store i1 false, ptr %binder.moved, align 1
  %s174 = load %std_core_str_core__Str, ptr %s173, align 8
  store %std_core_str_core__Str %s174, ptr %match.res, align 8
  br label %match.end

match.case175:                                    ; preds = %if.merge134
  %binder.p176 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p, i32 0, i32 0
  %e = load %std_core_str_core__Str, ptr %binder.p176, align 8
  store %std_core_str_core__Str %e, ptr %uc.self177, align 8
  %uc.r178 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self177)
  store %std_core_str_core__Str %uc.r178, ptr %e179, align 8
  store i1 false, ptr %binder.moved180, align 1
  %call181 = call i32 @std_core_str_core__Str.len(ptr %e179)
  %fstr.n = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp, i64 256, ptr @fstr.fmt.135, i32 %call181)
  %fstr.cap = add i32 %fstr.n, 1
  %fstr.cap64 = zext i32 %fstr.cap to i64
  %p = call ptr @malloc(i64 %fstr.cap64)
  %fstr.fits = icmp ult i32 %fstr.n, 256
  br i1 %fstr.fits, label %fstr.fits182, label %fstr.big

fstr.fits182:                                     ; preds = %match.case175
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p, ptr align 1 %fstr.tmp, i64 %fstr.cap64, i1 false)
  br label %fstr.done

fstr.big:                                         ; preds = %match.case175
  %14 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p, i64 %fstr.cap64, ptr @fstr.fmt.135, i32 %call181)
  br label %fstr.done

fstr.done:                                        ; preds = %fstr.big, %fstr.fits182
  %Str.d = insertvalue %std_core_str_core__Str undef, ptr %p, 0
  %Str.l = insertvalue %std_core_str_core__Str %Str.d, i32 %fstr.n, 1
  %Str.c = insertvalue %std_core_str_core__Str %Str.l, i32 %fstr.cap, 2
  store %std_core_str_core__Str %Str.c, ptr %match.res, align 8
  %drop.flag183 = load i1, ptr %binder.moved180, align 1
  br i1 %drop.flag183, label %drop.skip, label %drop.call

drop.call:                                        ; preds = %fstr.done
  call void @std_core_str_core__Str.__drop(ptr %e179)
  br label %drop.cont

drop.skip:                                        ; preds = %fstr.done
  br label %drop.cont

drop.cont:                                        ; preds = %drop.call, %drop.skip
  br label %match.end

match.end186:                                     ; preds = %match.case217, %drop.cont215
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj188)
  %dead.tag.p225 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj188, i32 0, i32 0
  store i8 2, ptr %dead.tag.p225, align 1
  %match.val226 = load %std_core_str_core__Str, ptr %match.res187, align 8
  store %std_core_str_core__Str %match.val226, ptr %t_err, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %t_arm)
  store i1 false, ptr %var.moved227, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %t_arm, align 8
  %call228 = call %"Result(std_core_str_core__Str,std_core_str_core__Str)" @try_in_arm(i32 1, i32 2)
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %call228, ptr %match.subj231, align 8
  %disc.p232 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj231, i32 0, i32 0
  %disc233 = load i8, ptr %disc.p232, align 1, !range !1
  %payload.p234 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj231, i32 0, i32 1
  switch i8 %disc233, label %match.default235 [
    i8 0, label %match.case236
    i8 1, label %match.case260
  ]

match.default192:                                 ; preds = %match.end
  unreachable

match.case193:                                    ; preds = %match.end
  %binder.p194 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p191, i32 0, i32 0
  %s195 = load %std_core_str_core__Str, ptr %binder.p194, align 8
  store %std_core_str_core__Str %s195, ptr %uc.self196, align 8
  %uc.r197 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self196)
  store %std_core_str_core__Str %uc.r197, ptr %s198, align 8
  store i1 false, ptr %binder.moved199, align 1
  %call200 = call i32 @std_core_str_core__Str.len(ptr %s198)
  %fstr.n202 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp201, i64 256, ptr @fstr.fmt.136, i32 %call200)
  %fstr.cap203 = add i32 %fstr.n202, 1
  %fstr.cap64204 = zext i32 %fstr.cap203 to i64
  %p205 = call ptr @malloc(i64 %fstr.cap64204)
  %fstr.fits206 = icmp ult i32 %fstr.n202, 256
  br i1 %fstr.fits206, label %fstr.fits207, label %fstr.big208

fstr.fits207:                                     ; preds = %match.case193
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p205, ptr align 1 %fstr.tmp201, i64 %fstr.cap64204, i1 false)
  br label %fstr.done209

fstr.big208:                                      ; preds = %match.case193
  %15 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p205, i64 %fstr.cap64204, ptr @fstr.fmt.136, i32 %call200)
  br label %fstr.done209

fstr.done209:                                     ; preds = %fstr.big208, %fstr.fits207
  %Str.d210 = insertvalue %std_core_str_core__Str undef, ptr %p205, 0
  %Str.l211 = insertvalue %std_core_str_core__Str %Str.d210, i32 %fstr.n202, 1
  %Str.c212 = insertvalue %std_core_str_core__Str %Str.l211, i32 %fstr.cap203, 2
  store %std_core_str_core__Str %Str.c212, ptr %match.res187, align 8
  %drop.flag216 = load i1, ptr %binder.moved199, align 1
  br i1 %drop.flag216, label %drop.skip214, label %drop.call213

drop.call213:                                     ; preds = %fstr.done209
  call void @std_core_str_core__Str.__drop(ptr %s198)
  br label %drop.cont215

drop.skip214:                                     ; preds = %fstr.done209
  br label %drop.cont215

drop.cont215:                                     ; preds = %drop.call213, %drop.skip214
  br label %match.end186

match.case217:                                    ; preds = %match.end
  %binder.p218 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p191, i32 0, i32 0
  %e219 = load %std_core_str_core__Str, ptr %binder.p218, align 8
  store %std_core_str_core__Str %e219, ptr %uc.self220, align 8
  %uc.r221 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self220)
  store %std_core_str_core__Str %uc.r221, ptr %e222, align 8
  store i1 false, ptr %binder.moved223, align 1
  %e224 = load %std_core_str_core__Str, ptr %e222, align 8
  store %std_core_str_core__Str %e224, ptr %match.res187, align 8
  br label %match.end186

match.end229:                                     ; preds = %match.case260, %drop.cont258
  call void @"Result(std_core_str_core__Str,std_core_str_core__Str).__drop"(ptr %match.subj231)
  %dead.tag.p268 = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %match.subj231, i32 0, i32 0
  store i8 2, ptr %dead.tag.p268, align 1
  %match.val269 = load %std_core_str_core__Str, ptr %match.res230, align 8
  store %std_core_str_core__Str %match.val269, ptr %t_arm, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.138, i32 4, i32 0 }, ptr %struct.borrow.tmp270, align 8
  %call271 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %t_ok, ptr %struct.borrow.tmp270)
  %not = xor i1 %call271, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp270)
  br i1 %not, label %if.then272, label %if.merge273

match.default235:                                 ; preds = %match.end186
  unreachable

match.case236:                                    ; preds = %match.end186
  %binder.p237 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p234, i32 0, i32 0
  %s238 = load %std_core_str_core__Str, ptr %binder.p237, align 8
  store %std_core_str_core__Str %s238, ptr %uc.self239, align 8
  %uc.r240 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self239)
  store %std_core_str_core__Str %uc.r240, ptr %s241, align 8
  store i1 false, ptr %binder.moved242, align 1
  %call243 = call i32 @std_core_str_core__Str.len(ptr %s241)
  %fstr.n245 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %fstr.tmp244, i64 256, ptr @fstr.fmt.137, i32 %call243)
  %fstr.cap246 = add i32 %fstr.n245, 1
  %fstr.cap64247 = zext i32 %fstr.cap246 to i64
  %p248 = call ptr @malloc(i64 %fstr.cap64247)
  %fstr.fits249 = icmp ult i32 %fstr.n245, 256
  br i1 %fstr.fits249, label %fstr.fits250, label %fstr.big251

fstr.fits250:                                     ; preds = %match.case236
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %p248, ptr align 1 %fstr.tmp244, i64 %fstr.cap64247, i1 false)
  br label %fstr.done252

fstr.big251:                                      ; preds = %match.case236
  %16 = call i32 (ptr, i64, ptr, ...) @__ls_fstr_format(ptr %p248, i64 %fstr.cap64247, ptr @fstr.fmt.137, i32 %call243)
  br label %fstr.done252

fstr.done252:                                     ; preds = %fstr.big251, %fstr.fits250
  %Str.d253 = insertvalue %std_core_str_core__Str undef, ptr %p248, 0
  %Str.l254 = insertvalue %std_core_str_core__Str %Str.d253, i32 %fstr.n245, 1
  %Str.c255 = insertvalue %std_core_str_core__Str %Str.l254, i32 %fstr.cap246, 2
  store %std_core_str_core__Str %Str.c255, ptr %match.res230, align 8
  %drop.flag259 = load i1, ptr %binder.moved242, align 1
  br i1 %drop.flag259, label %drop.skip257, label %drop.call256

drop.call256:                                     ; preds = %fstr.done252
  call void @std_core_str_core__Str.__drop(ptr %s241)
  br label %drop.cont258

drop.skip257:                                     ; preds = %fstr.done252
  br label %drop.cont258

drop.cont258:                                     ; preds = %drop.call256, %drop.skip257
  br label %match.end229

match.case260:                                    ; preds = %match.end186
  %binder.p261 = getelementptr inbounds { %std_core_str_core__Str }, ptr %payload.p234, i32 0, i32 0
  %e262 = load %std_core_str_core__Str, ptr %binder.p261, align 8
  store %std_core_str_core__Str %e262, ptr %uc.self263, align 8
  %uc.r264 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self263)
  store %std_core_str_core__Str %uc.r264, ptr %e265, align 8
  store i1 false, ptr %binder.moved266, align 1
  %e267 = load %std_core_str_core__Str, ptr %e265, align 8
  store %std_core_str_core__Str %e267, ptr %match.res230, align 8
  br label %match.end229

if.then272:                                       ; preds = %match.end229
  %17 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.140, i32 19, ptr @.ls.strlit.139)
  %18 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.141)
  br label %cleanup274

if.merge273:                                      ; preds = %match.end229
  store %std_core_str_core__Str { ptr @.ls.strlit.142, i32 3, i32 0 }, ptr %struct.borrow.tmp311, align 8
  %call312 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %t_err, ptr %struct.borrow.tmp311)
  %not313 = xor i1 %call312, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp311)
  br i1 %not313, label %if.then314, label %if.merge315

cleanup274:                                       ; preds = %if.then272
  %drop.flag277 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag277, label %drop.skip0275, label %drop.call0276

drop.skip0275:                                    ; preds = %drop.call0276, %cleanup274
  %drop.flag280 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag280, label %drop.skip1278, label %drop.call1279

drop.call0276:                                    ; preds = %cleanup274
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip0275

drop.skip1278:                                    ; preds = %drop.call1279, %drop.skip0275
  %drop.flag283 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag283, label %drop.skip2281, label %drop.call2282

drop.call1279:                                    ; preds = %drop.skip0275
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip1278

drop.skip2281:                                    ; preds = %drop.call2282, %drop.skip1278
  %drop.flag286 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag286, label %drop.skip3284, label %drop.call3285

drop.call2282:                                    ; preds = %drop.skip1278
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip2281

drop.skip3284:                                    ; preds = %drop.call3285, %drop.skip2281
  %drop.flag289 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag289, label %drop.skip4287, label %drop.call4288

drop.call3285:                                    ; preds = %drop.skip2281
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip3284

drop.skip4287:                                    ; preds = %drop.call4288, %drop.skip3284
  %drop.flag292 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag292, label %drop.skip5290, label %drop.call5291

drop.call4288:                                    ; preds = %drop.skip3284
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip4287

drop.skip5290:                                    ; preds = %drop.call5291, %drop.skip4287
  %drop.flag295 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag295, label %drop.skip6293, label %drop.call6294

drop.call5291:                                    ; preds = %drop.skip4287
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip5290

drop.skip6293:                                    ; preds = %drop.call6294, %drop.skip5290
  %drop.flag298 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag298, label %drop.skip7296, label %drop.call7297

drop.call6294:                                    ; preds = %drop.skip5290
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip6293

drop.skip7296:                                    ; preds = %drop.call7297, %drop.skip6293
  %drop.flag301 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag301, label %drop.skip8299, label %drop.call8300

drop.call7297:                                    ; preds = %drop.skip6293
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip7296

drop.skip8299:                                    ; preds = %drop.call8300, %drop.skip7296
  %drop.flag304 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag304, label %drop.skip9302, label %drop.call9303

drop.call8300:                                    ; preds = %drop.skip7296
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip8299

drop.skip9302:                                    ; preds = %drop.call9303, %drop.skip8299
  %drop.flag307 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag307, label %drop.skip10305, label %drop.call10306

drop.call9303:                                    ; preds = %drop.skip8299
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip9302

drop.skip10305:                                   ; preds = %drop.call10306, %drop.skip9302
  %drop.flag308 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag308, label %drop.skip11, label %drop.call11

drop.call10306:                                   ; preds = %drop.skip9302
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip10305

drop.skip11:                                      ; preds = %drop.call11, %drop.skip10305
  %drop.flag309 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag309, label %drop.skip12, label %drop.call12

drop.call11:                                      ; preds = %drop.skip10305
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip11

drop.skip12:                                      ; preds = %drop.call12, %drop.skip11
  %drop.flag310 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag310, label %drop.skip13, label %drop.call13

drop.call12:                                      ; preds = %drop.skip11
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip12

drop.skip13:                                      ; preds = %drop.call13, %drop.skip12
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call13:                                      ; preds = %drop.skip12
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip13

if.then314:                                       ; preds = %if.merge273
  %19 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.144, i32 20, ptr @.ls.strlit.143)
  %20 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.145)
  br label %cleanup316

if.merge315:                                      ; preds = %if.merge273
  store %std_core_str_core__Str { ptr @.ls.strlit.146, i32 3, i32 0 }, ptr %struct.borrow.tmp359, align 8
  %call360 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %t_arm, ptr %struct.borrow.tmp359)
  %not361 = xor i1 %call360, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp359)
  br i1 %not361, label %if.then362, label %if.merge363

cleanup316:                                       ; preds = %if.then314
  %drop.flag319 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag319, label %drop.skip0317, label %drop.call0318

drop.skip0317:                                    ; preds = %drop.call0318, %cleanup316
  %drop.flag322 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag322, label %drop.skip1320, label %drop.call1321

drop.call0318:                                    ; preds = %cleanup316
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip0317

drop.skip1320:                                    ; preds = %drop.call1321, %drop.skip0317
  %drop.flag325 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag325, label %drop.skip2323, label %drop.call2324

drop.call1321:                                    ; preds = %drop.skip0317
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip1320

drop.skip2323:                                    ; preds = %drop.call2324, %drop.skip1320
  %drop.flag328 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag328, label %drop.skip3326, label %drop.call3327

drop.call2324:                                    ; preds = %drop.skip1320
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip2323

drop.skip3326:                                    ; preds = %drop.call3327, %drop.skip2323
  %drop.flag331 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag331, label %drop.skip4329, label %drop.call4330

drop.call3327:                                    ; preds = %drop.skip2323
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip3326

drop.skip4329:                                    ; preds = %drop.call4330, %drop.skip3326
  %drop.flag334 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag334, label %drop.skip5332, label %drop.call5333

drop.call4330:                                    ; preds = %drop.skip3326
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip4329

drop.skip5332:                                    ; preds = %drop.call5333, %drop.skip4329
  %drop.flag337 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag337, label %drop.skip6335, label %drop.call6336

drop.call5333:                                    ; preds = %drop.skip4329
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip5332

drop.skip6335:                                    ; preds = %drop.call6336, %drop.skip5332
  %drop.flag340 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag340, label %drop.skip7338, label %drop.call7339

drop.call6336:                                    ; preds = %drop.skip5332
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip6335

drop.skip7338:                                    ; preds = %drop.call7339, %drop.skip6335
  %drop.flag343 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag343, label %drop.skip8341, label %drop.call8342

drop.call7339:                                    ; preds = %drop.skip6335
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip7338

drop.skip8341:                                    ; preds = %drop.call8342, %drop.skip7338
  %drop.flag346 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag346, label %drop.skip9344, label %drop.call9345

drop.call8342:                                    ; preds = %drop.skip7338
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip8341

drop.skip9344:                                    ; preds = %drop.call9345, %drop.skip8341
  %drop.flag349 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag349, label %drop.skip10347, label %drop.call10348

drop.call9345:                                    ; preds = %drop.skip8341
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip9344

drop.skip10347:                                   ; preds = %drop.call10348, %drop.skip9344
  %drop.flag352 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag352, label %drop.skip11350, label %drop.call11351

drop.call10348:                                   ; preds = %drop.skip9344
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip10347

drop.skip11350:                                   ; preds = %drop.call11351, %drop.skip10347
  %drop.flag355 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag355, label %drop.skip12353, label %drop.call12354

drop.call11351:                                   ; preds = %drop.skip10347
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip11350

drop.skip12353:                                   ; preds = %drop.call12354, %drop.skip11350
  %drop.flag358 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag358, label %drop.skip13356, label %drop.call13357

drop.call12354:                                   ; preds = %drop.skip11350
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip12353

drop.skip13356:                                   ; preds = %drop.call13357, %drop.skip12353
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call13357:                                   ; preds = %drop.skip12353
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip13356

if.then362:                                       ; preds = %if.merge315
  %21 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.148, i32 24, ptr @.ls.strlit.147)
  %22 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.149)
  br label %cleanup364

if.merge363:                                      ; preds = %if.merge315
  call void @llvm.lifetime.start.p0(i64 16, ptr %bf0)
  store i1 false, ptr %var.moved407, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %bf0, align 8
  %call408 = call %std_core_str_core__Str @binder_field_subject(i32 0)
  store %std_core_str_core__Str %call408, ptr %bf0, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %bf1)
  store i1 false, ptr %var.moved409, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %bf1, align 8
  %call410 = call %std_core_str_core__Str @binder_field_subject(i32 1)
  store %std_core_str_core__Str %call410, ptr %bf1, align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %bfn)
  store i1 false, ptr %var.moved411, align 1
  store %std_core_str_core__Str zeroinitializer, ptr %bfn, align 8
  %call412 = call %std_core_str_core__Str @binder_field_subject(i32 -1)
  store %std_core_str_core__Str %call412, ptr %bfn, align 8
  store %std_core_str_core__Str { ptr @.ls.strlit.150, i32 4, i32 0 }, ptr %struct.borrow.tmp413, align 8
  %call414 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %bf0, ptr %struct.borrow.tmp413)
  %not415 = xor i1 %call414, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp413)
  br i1 %not415, label %if.then416, label %if.merge417

cleanup364:                                       ; preds = %if.then362
  %drop.flag367 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag367, label %drop.skip0365, label %drop.call0366

drop.skip0365:                                    ; preds = %drop.call0366, %cleanup364
  %drop.flag370 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag370, label %drop.skip1368, label %drop.call1369

drop.call0366:                                    ; preds = %cleanup364
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip0365

drop.skip1368:                                    ; preds = %drop.call1369, %drop.skip0365
  %drop.flag373 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag373, label %drop.skip2371, label %drop.call2372

drop.call1369:                                    ; preds = %drop.skip0365
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip1368

drop.skip2371:                                    ; preds = %drop.call2372, %drop.skip1368
  %drop.flag376 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag376, label %drop.skip3374, label %drop.call3375

drop.call2372:                                    ; preds = %drop.skip1368
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip2371

drop.skip3374:                                    ; preds = %drop.call3375, %drop.skip2371
  %drop.flag379 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag379, label %drop.skip4377, label %drop.call4378

drop.call3375:                                    ; preds = %drop.skip2371
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip3374

drop.skip4377:                                    ; preds = %drop.call4378, %drop.skip3374
  %drop.flag382 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag382, label %drop.skip5380, label %drop.call5381

drop.call4378:                                    ; preds = %drop.skip3374
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip4377

drop.skip5380:                                    ; preds = %drop.call5381, %drop.skip4377
  %drop.flag385 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag385, label %drop.skip6383, label %drop.call6384

drop.call5381:                                    ; preds = %drop.skip4377
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip5380

drop.skip6383:                                    ; preds = %drop.call6384, %drop.skip5380
  %drop.flag388 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag388, label %drop.skip7386, label %drop.call7387

drop.call6384:                                    ; preds = %drop.skip5380
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip6383

drop.skip7386:                                    ; preds = %drop.call7387, %drop.skip6383
  %drop.flag391 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag391, label %drop.skip8389, label %drop.call8390

drop.call7387:                                    ; preds = %drop.skip6383
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip7386

drop.skip8389:                                    ; preds = %drop.call8390, %drop.skip7386
  %drop.flag394 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag394, label %drop.skip9392, label %drop.call9393

drop.call8390:                                    ; preds = %drop.skip7386
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip8389

drop.skip9392:                                    ; preds = %drop.call9393, %drop.skip8389
  %drop.flag397 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag397, label %drop.skip10395, label %drop.call10396

drop.call9393:                                    ; preds = %drop.skip8389
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip9392

drop.skip10395:                                   ; preds = %drop.call10396, %drop.skip9392
  %drop.flag400 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag400, label %drop.skip11398, label %drop.call11399

drop.call10396:                                   ; preds = %drop.skip9392
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip10395

drop.skip11398:                                   ; preds = %drop.call11399, %drop.skip10395
  %drop.flag403 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag403, label %drop.skip12401, label %drop.call12402

drop.call11399:                                   ; preds = %drop.skip10395
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip11398

drop.skip12401:                                   ; preds = %drop.call12402, %drop.skip11398
  %drop.flag406 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag406, label %drop.skip13404, label %drop.call13405

drop.call12402:                                   ; preds = %drop.skip11398
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip12401

drop.skip13404:                                   ; preds = %drop.call13405, %drop.skip12401
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call13405:                                   ; preds = %drop.skip12401
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip13404

if.then416:                                       ; preds = %if.merge363
  %23 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.152, i32 21, ptr @.ls.strlit.151)
  %24 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.153)
  br label %cleanup418

if.merge417:                                      ; preds = %if.merge363
  store %std_core_str_core__Str { ptr @.ls.strlit.154, i32 3, i32 0 }, ptr %struct.borrow.tmp464, align 8
  %call465 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %bf1, ptr %struct.borrow.tmp464)
  %not466 = xor i1 %call465, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp464)
  br i1 %not466, label %if.then467, label %if.merge468

cleanup418:                                       ; preds = %if.then416
  %drop.flag421 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag421, label %drop.skip0419, label %drop.call0420

drop.skip0419:                                    ; preds = %drop.call0420, %cleanup418
  %drop.flag424 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag424, label %drop.skip1422, label %drop.call1423

drop.call0420:                                    ; preds = %cleanup418
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0419

drop.skip1422:                                    ; preds = %drop.call1423, %drop.skip0419
  %drop.flag427 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag427, label %drop.skip2425, label %drop.call2426

drop.call1423:                                    ; preds = %drop.skip0419
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1422

drop.skip2425:                                    ; preds = %drop.call2426, %drop.skip1422
  %drop.flag430 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag430, label %drop.skip3428, label %drop.call3429

drop.call2426:                                    ; preds = %drop.skip1422
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2425

drop.skip3428:                                    ; preds = %drop.call3429, %drop.skip2425
  %drop.flag433 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag433, label %drop.skip4431, label %drop.call4432

drop.call3429:                                    ; preds = %drop.skip2425
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3428

drop.skip4431:                                    ; preds = %drop.call4432, %drop.skip3428
  %drop.flag436 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag436, label %drop.skip5434, label %drop.call5435

drop.call4432:                                    ; preds = %drop.skip3428
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4431

drop.skip5434:                                    ; preds = %drop.call5435, %drop.skip4431
  %drop.flag439 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag439, label %drop.skip6437, label %drop.call6438

drop.call5435:                                    ; preds = %drop.skip4431
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5434

drop.skip6437:                                    ; preds = %drop.call6438, %drop.skip5434
  %drop.flag442 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag442, label %drop.skip7440, label %drop.call7441

drop.call6438:                                    ; preds = %drop.skip5434
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6437

drop.skip7440:                                    ; preds = %drop.call7441, %drop.skip6437
  %drop.flag445 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag445, label %drop.skip8443, label %drop.call8444

drop.call7441:                                    ; preds = %drop.skip6437
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7440

drop.skip8443:                                    ; preds = %drop.call8444, %drop.skip7440
  %drop.flag448 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag448, label %drop.skip9446, label %drop.call9447

drop.call8444:                                    ; preds = %drop.skip7440
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8443

drop.skip9446:                                    ; preds = %drop.call9447, %drop.skip8443
  %drop.flag451 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag451, label %drop.skip10449, label %drop.call10450

drop.call9447:                                    ; preds = %drop.skip8443
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9446

drop.skip10449:                                   ; preds = %drop.call10450, %drop.skip9446
  %drop.flag454 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag454, label %drop.skip11452, label %drop.call11453

drop.call10450:                                   ; preds = %drop.skip9446
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10449

drop.skip11452:                                   ; preds = %drop.call11453, %drop.skip10449
  %drop.flag457 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag457, label %drop.skip12455, label %drop.call12456

drop.call11453:                                   ; preds = %drop.skip10449
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11452

drop.skip12455:                                   ; preds = %drop.call12456, %drop.skip11452
  %drop.flag460 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag460, label %drop.skip13458, label %drop.call13459

drop.call12456:                                   ; preds = %drop.skip11452
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12455

drop.skip13458:                                   ; preds = %drop.call13459, %drop.skip12455
  %drop.flag461 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag461, label %drop.skip14, label %drop.call14

drop.call13459:                                   ; preds = %drop.skip12455
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13458

drop.skip14:                                      ; preds = %drop.call14, %drop.skip13458
  %drop.flag462 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag462, label %drop.skip15, label %drop.call15

drop.call14:                                      ; preds = %drop.skip13458
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14

drop.skip15:                                      ; preds = %drop.call15, %drop.skip14
  %drop.flag463 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag463, label %drop.skip16, label %drop.call16

drop.call15:                                      ; preds = %drop.skip14
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15

drop.skip16:                                      ; preds = %drop.call16, %drop.skip15
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16:                                      ; preds = %drop.skip15
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16

if.then467:                                       ; preds = %if.merge417
  %25 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.156, i32 22, ptr @.ls.strlit.155)
  %26 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.157)
  br label %cleanup469

if.merge468:                                      ; preds = %if.merge417
  store %std_core_str_core__Str { ptr @.ls.strlit.158, i32 4, i32 0 }, ptr %struct.borrow.tmp521, align 8
  %call522 = call i1 @"std_core_str_core__Str.starts_with?"(ptr %bfn, ptr %struct.borrow.tmp521)
  %not523 = xor i1 %call522, true
  call void @std_core_str_core__Str.__drop(ptr %struct.borrow.tmp521)
  br i1 %not523, label %if.then524, label %if.merge525

cleanup469:                                       ; preds = %if.then467
  %drop.flag472 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag472, label %drop.skip0470, label %drop.call0471

drop.skip0470:                                    ; preds = %drop.call0471, %cleanup469
  %drop.flag475 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag475, label %drop.skip1473, label %drop.call1474

drop.call0471:                                    ; preds = %cleanup469
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0470

drop.skip1473:                                    ; preds = %drop.call1474, %drop.skip0470
  %drop.flag478 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag478, label %drop.skip2476, label %drop.call2477

drop.call1474:                                    ; preds = %drop.skip0470
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1473

drop.skip2476:                                    ; preds = %drop.call2477, %drop.skip1473
  %drop.flag481 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag481, label %drop.skip3479, label %drop.call3480

drop.call2477:                                    ; preds = %drop.skip1473
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2476

drop.skip3479:                                    ; preds = %drop.call3480, %drop.skip2476
  %drop.flag484 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag484, label %drop.skip4482, label %drop.call4483

drop.call3480:                                    ; preds = %drop.skip2476
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3479

drop.skip4482:                                    ; preds = %drop.call4483, %drop.skip3479
  %drop.flag487 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag487, label %drop.skip5485, label %drop.call5486

drop.call4483:                                    ; preds = %drop.skip3479
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4482

drop.skip5485:                                    ; preds = %drop.call5486, %drop.skip4482
  %drop.flag490 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag490, label %drop.skip6488, label %drop.call6489

drop.call5486:                                    ; preds = %drop.skip4482
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5485

drop.skip6488:                                    ; preds = %drop.call6489, %drop.skip5485
  %drop.flag493 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag493, label %drop.skip7491, label %drop.call7492

drop.call6489:                                    ; preds = %drop.skip5485
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6488

drop.skip7491:                                    ; preds = %drop.call7492, %drop.skip6488
  %drop.flag496 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag496, label %drop.skip8494, label %drop.call8495

drop.call7492:                                    ; preds = %drop.skip6488
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7491

drop.skip8494:                                    ; preds = %drop.call8495, %drop.skip7491
  %drop.flag499 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag499, label %drop.skip9497, label %drop.call9498

drop.call8495:                                    ; preds = %drop.skip7491
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8494

drop.skip9497:                                    ; preds = %drop.call9498, %drop.skip8494
  %drop.flag502 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag502, label %drop.skip10500, label %drop.call10501

drop.call9498:                                    ; preds = %drop.skip8494
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9497

drop.skip10500:                                   ; preds = %drop.call10501, %drop.skip9497
  %drop.flag505 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag505, label %drop.skip11503, label %drop.call11504

drop.call10501:                                   ; preds = %drop.skip9497
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10500

drop.skip11503:                                   ; preds = %drop.call11504, %drop.skip10500
  %drop.flag508 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag508, label %drop.skip12506, label %drop.call12507

drop.call11504:                                   ; preds = %drop.skip10500
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11503

drop.skip12506:                                   ; preds = %drop.call12507, %drop.skip11503
  %drop.flag511 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag511, label %drop.skip13509, label %drop.call13510

drop.call12507:                                   ; preds = %drop.skip11503
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12506

drop.skip13509:                                   ; preds = %drop.call13510, %drop.skip12506
  %drop.flag514 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag514, label %drop.skip14512, label %drop.call14513

drop.call13510:                                   ; preds = %drop.skip12506
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13509

drop.skip14512:                                   ; preds = %drop.call14513, %drop.skip13509
  %drop.flag517 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag517, label %drop.skip15515, label %drop.call15516

drop.call14513:                                   ; preds = %drop.skip13509
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14512

drop.skip15515:                                   ; preds = %drop.call15516, %drop.skip14512
  %drop.flag520 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag520, label %drop.skip16518, label %drop.call16519

drop.call15516:                                   ; preds = %drop.skip14512
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15515

drop.skip16518:                                   ; preds = %drop.call16519, %drop.skip15515
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16519:                                   ; preds = %drop.skip15515
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16518

if.then524:                                       ; preds = %if.merge468
  %27 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.160, i32 23, ptr @.ls.strlit.159)
  %28 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.161)
  br label %cleanup526

if.merge525:                                      ; preds = %if.merge468
  %call578 = call i32 @binder_move(i32 0)
  store i32 %call578, ptr %bm0, align 4
  %call579 = call i32 @binder_move(i32 1)
  store i32 %call579, ptr %bm1, align 4
  %bm0580 = load i32, ptr %bm0, align 4
  %ne = icmp ne i32 %bm0580, 101
  br i1 %ne, label %if.then581, label %if.merge582

cleanup526:                                       ; preds = %if.then524
  %drop.flag529 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag529, label %drop.skip0527, label %drop.call0528

drop.skip0527:                                    ; preds = %drop.call0528, %cleanup526
  %drop.flag532 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag532, label %drop.skip1530, label %drop.call1531

drop.call0528:                                    ; preds = %cleanup526
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0527

drop.skip1530:                                    ; preds = %drop.call1531, %drop.skip0527
  %drop.flag535 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag535, label %drop.skip2533, label %drop.call2534

drop.call1531:                                    ; preds = %drop.skip0527
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1530

drop.skip2533:                                    ; preds = %drop.call2534, %drop.skip1530
  %drop.flag538 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag538, label %drop.skip3536, label %drop.call3537

drop.call2534:                                    ; preds = %drop.skip1530
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2533

drop.skip3536:                                    ; preds = %drop.call3537, %drop.skip2533
  %drop.flag541 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag541, label %drop.skip4539, label %drop.call4540

drop.call3537:                                    ; preds = %drop.skip2533
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3536

drop.skip4539:                                    ; preds = %drop.call4540, %drop.skip3536
  %drop.flag544 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag544, label %drop.skip5542, label %drop.call5543

drop.call4540:                                    ; preds = %drop.skip3536
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4539

drop.skip5542:                                    ; preds = %drop.call5543, %drop.skip4539
  %drop.flag547 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag547, label %drop.skip6545, label %drop.call6546

drop.call5543:                                    ; preds = %drop.skip4539
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5542

drop.skip6545:                                    ; preds = %drop.call6546, %drop.skip5542
  %drop.flag550 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag550, label %drop.skip7548, label %drop.call7549

drop.call6546:                                    ; preds = %drop.skip5542
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6545

drop.skip7548:                                    ; preds = %drop.call7549, %drop.skip6545
  %drop.flag553 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag553, label %drop.skip8551, label %drop.call8552

drop.call7549:                                    ; preds = %drop.skip6545
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7548

drop.skip8551:                                    ; preds = %drop.call8552, %drop.skip7548
  %drop.flag556 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag556, label %drop.skip9554, label %drop.call9555

drop.call8552:                                    ; preds = %drop.skip7548
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8551

drop.skip9554:                                    ; preds = %drop.call9555, %drop.skip8551
  %drop.flag559 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag559, label %drop.skip10557, label %drop.call10558

drop.call9555:                                    ; preds = %drop.skip8551
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9554

drop.skip10557:                                   ; preds = %drop.call10558, %drop.skip9554
  %drop.flag562 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag562, label %drop.skip11560, label %drop.call11561

drop.call10558:                                   ; preds = %drop.skip9554
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10557

drop.skip11560:                                   ; preds = %drop.call11561, %drop.skip10557
  %drop.flag565 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag565, label %drop.skip12563, label %drop.call12564

drop.call11561:                                   ; preds = %drop.skip10557
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11560

drop.skip12563:                                   ; preds = %drop.call12564, %drop.skip11560
  %drop.flag568 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag568, label %drop.skip13566, label %drop.call13567

drop.call12564:                                   ; preds = %drop.skip11560
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12563

drop.skip13566:                                   ; preds = %drop.call13567, %drop.skip12563
  %drop.flag571 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag571, label %drop.skip14569, label %drop.call14570

drop.call13567:                                   ; preds = %drop.skip12563
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13566

drop.skip14569:                                   ; preds = %drop.call14570, %drop.skip13566
  %drop.flag574 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag574, label %drop.skip15572, label %drop.call15573

drop.call14570:                                   ; preds = %drop.skip13566
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14569

drop.skip15572:                                   ; preds = %drop.call15573, %drop.skip14569
  %drop.flag577 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag577, label %drop.skip16575, label %drop.call16576

drop.call15573:                                   ; preds = %drop.skip14569
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15572

drop.skip16575:                                   ; preds = %drop.call16576, %drop.skip15572
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16576:                                   ; preds = %drop.skip15572
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16575

if.then581:                                       ; preds = %if.merge525
  %29 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.163, i32 20, ptr @.ls.strlit.162)
  %bm0583 = load i32, ptr %bm0, align 4
  %30 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.164, i32 %bm0583)
  br label %cleanup584

if.merge582:                                      ; preds = %if.merge525
  %bm1636 = load i32, ptr %bm1, align 4
  %slt637 = icmp slt i32 %bm1636, 4
  br i1 %slt637, label %if.then638, label %if.merge639

cleanup584:                                       ; preds = %if.then581
  %drop.flag587 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag587, label %drop.skip0585, label %drop.call0586

drop.skip0585:                                    ; preds = %drop.call0586, %cleanup584
  %drop.flag590 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag590, label %drop.skip1588, label %drop.call1589

drop.call0586:                                    ; preds = %cleanup584
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0585

drop.skip1588:                                    ; preds = %drop.call1589, %drop.skip0585
  %drop.flag593 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag593, label %drop.skip2591, label %drop.call2592

drop.call1589:                                    ; preds = %drop.skip0585
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1588

drop.skip2591:                                    ; preds = %drop.call2592, %drop.skip1588
  %drop.flag596 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag596, label %drop.skip3594, label %drop.call3595

drop.call2592:                                    ; preds = %drop.skip1588
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2591

drop.skip3594:                                    ; preds = %drop.call3595, %drop.skip2591
  %drop.flag599 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag599, label %drop.skip4597, label %drop.call4598

drop.call3595:                                    ; preds = %drop.skip2591
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3594

drop.skip4597:                                    ; preds = %drop.call4598, %drop.skip3594
  %drop.flag602 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag602, label %drop.skip5600, label %drop.call5601

drop.call4598:                                    ; preds = %drop.skip3594
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4597

drop.skip5600:                                    ; preds = %drop.call5601, %drop.skip4597
  %drop.flag605 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag605, label %drop.skip6603, label %drop.call6604

drop.call5601:                                    ; preds = %drop.skip4597
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5600

drop.skip6603:                                    ; preds = %drop.call6604, %drop.skip5600
  %drop.flag608 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag608, label %drop.skip7606, label %drop.call7607

drop.call6604:                                    ; preds = %drop.skip5600
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6603

drop.skip7606:                                    ; preds = %drop.call7607, %drop.skip6603
  %drop.flag611 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag611, label %drop.skip8609, label %drop.call8610

drop.call7607:                                    ; preds = %drop.skip6603
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7606

drop.skip8609:                                    ; preds = %drop.call8610, %drop.skip7606
  %drop.flag614 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag614, label %drop.skip9612, label %drop.call9613

drop.call8610:                                    ; preds = %drop.skip7606
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8609

drop.skip9612:                                    ; preds = %drop.call9613, %drop.skip8609
  %drop.flag617 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag617, label %drop.skip10615, label %drop.call10616

drop.call9613:                                    ; preds = %drop.skip8609
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9612

drop.skip10615:                                   ; preds = %drop.call10616, %drop.skip9612
  %drop.flag620 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag620, label %drop.skip11618, label %drop.call11619

drop.call10616:                                   ; preds = %drop.skip9612
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10615

drop.skip11618:                                   ; preds = %drop.call11619, %drop.skip10615
  %drop.flag623 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag623, label %drop.skip12621, label %drop.call12622

drop.call11619:                                   ; preds = %drop.skip10615
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11618

drop.skip12621:                                   ; preds = %drop.call12622, %drop.skip11618
  %drop.flag626 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag626, label %drop.skip13624, label %drop.call13625

drop.call12622:                                   ; preds = %drop.skip11618
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12621

drop.skip13624:                                   ; preds = %drop.call13625, %drop.skip12621
  %drop.flag629 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag629, label %drop.skip14627, label %drop.call14628

drop.call13625:                                   ; preds = %drop.skip12621
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13624

drop.skip14627:                                   ; preds = %drop.call14628, %drop.skip13624
  %drop.flag632 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag632, label %drop.skip15630, label %drop.call15631

drop.call14628:                                   ; preds = %drop.skip13624
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14627

drop.skip15630:                                   ; preds = %drop.call15631, %drop.skip14627
  %drop.flag635 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag635, label %drop.skip16633, label %drop.call16634

drop.call15631:                                   ; preds = %drop.skip14627
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15630

drop.skip16633:                                   ; preds = %drop.call16634, %drop.skip15630
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16634:                                   ; preds = %drop.skip15630
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16633

if.then638:                                       ; preds = %if.merge582
  %31 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.166, i32 21, ptr @.ls.strlit.165)
  %bm1640 = load i32, ptr %bm1, align 4
  %32 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.167, i32 %bm1640)
  br label %cleanup641

if.merge639:                                      ; preds = %if.merge582
  %call693 = call i32 @deep_blocks()
  store i32 %call693, ptr %db, align 4
  %db694 = load i32, ptr %db, align 4
  %ne695 = icmp ne i32 %db694, 15
  br i1 %ne695, label %if.then696, label %if.merge697

cleanup641:                                       ; preds = %if.then638
  %drop.flag644 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag644, label %drop.skip0642, label %drop.call0643

drop.skip0642:                                    ; preds = %drop.call0643, %cleanup641
  %drop.flag647 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag647, label %drop.skip1645, label %drop.call1646

drop.call0643:                                    ; preds = %cleanup641
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0642

drop.skip1645:                                    ; preds = %drop.call1646, %drop.skip0642
  %drop.flag650 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag650, label %drop.skip2648, label %drop.call2649

drop.call1646:                                    ; preds = %drop.skip0642
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1645

drop.skip2648:                                    ; preds = %drop.call2649, %drop.skip1645
  %drop.flag653 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag653, label %drop.skip3651, label %drop.call3652

drop.call2649:                                    ; preds = %drop.skip1645
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2648

drop.skip3651:                                    ; preds = %drop.call3652, %drop.skip2648
  %drop.flag656 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag656, label %drop.skip4654, label %drop.call4655

drop.call3652:                                    ; preds = %drop.skip2648
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3651

drop.skip4654:                                    ; preds = %drop.call4655, %drop.skip3651
  %drop.flag659 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag659, label %drop.skip5657, label %drop.call5658

drop.call4655:                                    ; preds = %drop.skip3651
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4654

drop.skip5657:                                    ; preds = %drop.call5658, %drop.skip4654
  %drop.flag662 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag662, label %drop.skip6660, label %drop.call6661

drop.call5658:                                    ; preds = %drop.skip4654
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5657

drop.skip6660:                                    ; preds = %drop.call6661, %drop.skip5657
  %drop.flag665 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag665, label %drop.skip7663, label %drop.call7664

drop.call6661:                                    ; preds = %drop.skip5657
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6660

drop.skip7663:                                    ; preds = %drop.call7664, %drop.skip6660
  %drop.flag668 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag668, label %drop.skip8666, label %drop.call8667

drop.call7664:                                    ; preds = %drop.skip6660
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7663

drop.skip8666:                                    ; preds = %drop.call8667, %drop.skip7663
  %drop.flag671 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag671, label %drop.skip9669, label %drop.call9670

drop.call8667:                                    ; preds = %drop.skip7663
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8666

drop.skip9669:                                    ; preds = %drop.call9670, %drop.skip8666
  %drop.flag674 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag674, label %drop.skip10672, label %drop.call10673

drop.call9670:                                    ; preds = %drop.skip8666
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9669

drop.skip10672:                                   ; preds = %drop.call10673, %drop.skip9669
  %drop.flag677 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag677, label %drop.skip11675, label %drop.call11676

drop.call10673:                                   ; preds = %drop.skip9669
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10672

drop.skip11675:                                   ; preds = %drop.call11676, %drop.skip10672
  %drop.flag680 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag680, label %drop.skip12678, label %drop.call12679

drop.call11676:                                   ; preds = %drop.skip10672
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11675

drop.skip12678:                                   ; preds = %drop.call12679, %drop.skip11675
  %drop.flag683 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag683, label %drop.skip13681, label %drop.call13682

drop.call12679:                                   ; preds = %drop.skip11675
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12678

drop.skip13681:                                   ; preds = %drop.call13682, %drop.skip12678
  %drop.flag686 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag686, label %drop.skip14684, label %drop.call14685

drop.call13682:                                   ; preds = %drop.skip12678
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13681

drop.skip14684:                                   ; preds = %drop.call14685, %drop.skip13681
  %drop.flag689 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag689, label %drop.skip15687, label %drop.call15688

drop.call14685:                                   ; preds = %drop.skip13681
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14684

drop.skip15687:                                   ; preds = %drop.call15688, %drop.skip14684
  %drop.flag692 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag692, label %drop.skip16690, label %drop.call16691

drop.call15688:                                   ; preds = %drop.skip14684
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15687

drop.skip16690:                                   ; preds = %drop.call16691, %drop.skip15687
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16691:                                   ; preds = %drop.skip15687
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16690

if.then696:                                       ; preds = %if.merge639
  %33 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.169, i32 17, ptr @.ls.strlit.168)
  %db698 = load i32, ptr %db, align 4
  %34 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.170, i32 %db698)
  br label %cleanup699

if.merge697:                                      ; preds = %if.merge639
  %35 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.172, i32 16, ptr @.ls.strlit.171)
  %36 = call i32 (ptr, ...) @__ls_printf(ptr @.ls.fmt.173)
  br label %cleanup751

cleanup699:                                       ; preds = %if.then696
  %drop.flag702 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag702, label %drop.skip0700, label %drop.call0701

drop.skip0700:                                    ; preds = %drop.call0701, %cleanup699
  %drop.flag705 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag705, label %drop.skip1703, label %drop.call1704

drop.call0701:                                    ; preds = %cleanup699
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0700

drop.skip1703:                                    ; preds = %drop.call1704, %drop.skip0700
  %drop.flag708 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag708, label %drop.skip2706, label %drop.call2707

drop.call1704:                                    ; preds = %drop.skip0700
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1703

drop.skip2706:                                    ; preds = %drop.call2707, %drop.skip1703
  %drop.flag711 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag711, label %drop.skip3709, label %drop.call3710

drop.call2707:                                    ; preds = %drop.skip1703
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2706

drop.skip3709:                                    ; preds = %drop.call3710, %drop.skip2706
  %drop.flag714 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag714, label %drop.skip4712, label %drop.call4713

drop.call3710:                                    ; preds = %drop.skip2706
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3709

drop.skip4712:                                    ; preds = %drop.call4713, %drop.skip3709
  %drop.flag717 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag717, label %drop.skip5715, label %drop.call5716

drop.call4713:                                    ; preds = %drop.skip3709
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4712

drop.skip5715:                                    ; preds = %drop.call5716, %drop.skip4712
  %drop.flag720 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag720, label %drop.skip6718, label %drop.call6719

drop.call5716:                                    ; preds = %drop.skip4712
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5715

drop.skip6718:                                    ; preds = %drop.call6719, %drop.skip5715
  %drop.flag723 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag723, label %drop.skip7721, label %drop.call7722

drop.call6719:                                    ; preds = %drop.skip5715
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6718

drop.skip7721:                                    ; preds = %drop.call7722, %drop.skip6718
  %drop.flag726 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag726, label %drop.skip8724, label %drop.call8725

drop.call7722:                                    ; preds = %drop.skip6718
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7721

drop.skip8724:                                    ; preds = %drop.call8725, %drop.skip7721
  %drop.flag729 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag729, label %drop.skip9727, label %drop.call9728

drop.call8725:                                    ; preds = %drop.skip7721
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8724

drop.skip9727:                                    ; preds = %drop.call9728, %drop.skip8724
  %drop.flag732 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag732, label %drop.skip10730, label %drop.call10731

drop.call9728:                                    ; preds = %drop.skip8724
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9727

drop.skip10730:                                   ; preds = %drop.call10731, %drop.skip9727
  %drop.flag735 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag735, label %drop.skip11733, label %drop.call11734

drop.call10731:                                   ; preds = %drop.skip9727
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10730

drop.skip11733:                                   ; preds = %drop.call11734, %drop.skip10730
  %drop.flag738 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag738, label %drop.skip12736, label %drop.call12737

drop.call11734:                                   ; preds = %drop.skip10730
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11733

drop.skip12736:                                   ; preds = %drop.call12737, %drop.skip11733
  %drop.flag741 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag741, label %drop.skip13739, label %drop.call13740

drop.call12737:                                   ; preds = %drop.skip11733
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12736

drop.skip13739:                                   ; preds = %drop.call13740, %drop.skip12736
  %drop.flag744 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag744, label %drop.skip14742, label %drop.call14743

drop.call13740:                                   ; preds = %drop.skip12736
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13739

drop.skip14742:                                   ; preds = %drop.call14743, %drop.skip13739
  %drop.flag747 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag747, label %drop.skip15745, label %drop.call15746

drop.call14743:                                   ; preds = %drop.skip13739
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14742

drop.skip15745:                                   ; preds = %drop.call15746, %drop.skip14742
  %drop.flag750 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag750, label %drop.skip16748, label %drop.call16749

drop.call15746:                                   ; preds = %drop.skip14742
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15745

drop.skip16748:                                   ; preds = %drop.call16749, %drop.skip15745
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 1

drop.call16749:                                   ; preds = %drop.skip15745
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16748

cleanup751:                                       ; preds = %if.merge697
  %drop.flag754 = load i1, ptr %var.moved411, align 1
  br i1 %drop.flag754, label %drop.skip0752, label %drop.call0753

drop.skip0752:                                    ; preds = %drop.call0753, %cleanup751
  %drop.flag757 = load i1, ptr %var.moved409, align 1
  br i1 %drop.flag757, label %drop.skip1755, label %drop.call1756

drop.call0753:                                    ; preds = %cleanup751
  call void @std_core_str_core__Str.__drop(ptr %bfn)
  br label %drop.skip0752

drop.skip1755:                                    ; preds = %drop.call1756, %drop.skip0752
  %drop.flag760 = load i1, ptr %var.moved407, align 1
  br i1 %drop.flag760, label %drop.skip2758, label %drop.call2759

drop.call1756:                                    ; preds = %drop.skip0752
  call void @std_core_str_core__Str.__drop(ptr %bf1)
  br label %drop.skip1755

drop.skip2758:                                    ; preds = %drop.call2759, %drop.skip1755
  %drop.flag763 = load i1, ptr %var.moved227, align 1
  br i1 %drop.flag763, label %drop.skip3761, label %drop.call3762

drop.call2759:                                    ; preds = %drop.skip1755
  call void @std_core_str_core__Str.__drop(ptr %bf0)
  br label %drop.skip2758

drop.skip3761:                                    ; preds = %drop.call3762, %drop.skip2758
  %drop.flag766 = load i1, ptr %var.moved184, align 1
  br i1 %drop.flag766, label %drop.skip4764, label %drop.call4765

drop.call3762:                                    ; preds = %drop.skip2758
  call void @std_core_str_core__Str.__drop(ptr %t_arm)
  br label %drop.skip3761

drop.skip4764:                                    ; preds = %drop.call4765, %drop.skip3761
  %drop.flag769 = load i1, ptr %var.moved169, align 1
  br i1 %drop.flag769, label %drop.skip5767, label %drop.call5768

drop.call4765:                                    ; preds = %drop.skip3761
  call void @std_core_str_core__Str.__drop(ptr %t_err)
  br label %drop.skip4764

drop.skip5767:                                    ; preds = %drop.call5768, %drop.skip4764
  %drop.flag772 = load i1, ptr %var.moved86, align 1
  br i1 %drop.flag772, label %drop.skip6770, label %drop.call6771

drop.call5768:                                    ; preds = %drop.skip4764
  call void @std_core_str_core__Str.__drop(ptr %t_ok)
  br label %drop.skip5767

drop.skip6770:                                    ; preds = %drop.call6771, %drop.skip5767
  %drop.flag775 = load i1, ptr %var.moved81, align 1
  br i1 %drop.flag775, label %drop.skip7773, label %drop.call7774

drop.call6771:                                    ; preds = %drop.skip5767
  call void @std_core_str_core__Str.__drop(ptr %s2)
  br label %drop.skip6770

drop.skip7773:                                    ; preds = %drop.call7774, %drop.skip6770
  %drop.flag778 = load i1, ptr %var.moved78, align 1
  br i1 %drop.flag778, label %drop.skip8776, label %drop.call8777

drop.call7774:                                    ; preds = %drop.skip6770
  call void @std_core_str_core__Str.__drop(ptr %s1)
  br label %drop.skip7773

drop.skip8776:                                    ; preds = %drop.call8777, %drop.skip7773
  %drop.flag781 = load i1, ptr %var.moved77, align 1
  br i1 %drop.flag781, label %drop.skip9779, label %drop.call9780

drop.call8777:                                    ; preds = %drop.skip7773
  call void @std_core_str_core__Str.__drop(ptr %s0)
  br label %drop.skip8776

drop.skip9779:                                    ; preds = %drop.call9780, %drop.skip8776
  %drop.flag784 = load i1, ptr %var.moved45, align 1
  br i1 %drop.flag784, label %drop.skip10782, label %drop.call10783

drop.call9780:                                    ; preds = %drop.skip8776
  call void @std_core_str_core__Str.__drop(ptr %fb)
  br label %drop.skip9779

drop.skip10782:                                   ; preds = %drop.call10783, %drop.skip9779
  %drop.flag787 = load i1, ptr %var.moved43, align 1
  br i1 %drop.flag787, label %drop.skip11785, label %drop.call11786

drop.call10783:                                   ; preds = %drop.skip9779
  call void @std_core_str_core__Str.__drop(ptr %p2)
  br label %drop.skip10782

drop.skip11785:                                   ; preds = %drop.call11786, %drop.skip10782
  %drop.flag790 = load i1, ptr %var.moved41, align 1
  br i1 %drop.flag790, label %drop.skip12788, label %drop.call12789

drop.call11786:                                   ; preds = %drop.skip10782
  call void @std_core_str_core__Str.__drop(ptr %p1)
  br label %drop.skip11785

drop.skip12788:                                   ; preds = %drop.call12789, %drop.skip11785
  %drop.flag793 = load i1, ptr %var.moved21, align 1
  br i1 %drop.flag793, label %drop.skip13791, label %drop.call13792

drop.call12789:                                   ; preds = %drop.skip11785
  call void @std_core_str_core__Str.__drop(ptr %p0)
  br label %drop.skip12788

drop.skip13791:                                   ; preds = %drop.call13792, %drop.skip12788
  %drop.flag796 = load i1, ptr %var.moved19, align 1
  br i1 %drop.flag796, label %drop.skip14794, label %drop.call14795

drop.call13792:                                   ; preds = %drop.skip12788
  call void @std_core_str_core__Str.__drop(ptr %e1)
  br label %drop.skip13791

drop.skip14794:                                   ; preds = %drop.call14795, %drop.skip13791
  %drop.flag799 = load i1, ptr %var.moved1, align 1
  br i1 %drop.flag799, label %drop.skip15797, label %drop.call15798

drop.call14795:                                   ; preds = %drop.skip13791
  call void @std_core_str_core__Str.__drop(ptr %e0)
  br label %drop.skip14794

drop.skip15797:                                   ; preds = %drop.call15798, %drop.skip14794
  %drop.flag802 = load i1, ptr %var.moved, align 1
  br i1 %drop.flag802, label %drop.skip16800, label %drop.call16801

drop.call15798:                                   ; preds = %drop.skip14794
  call void @std_core_str_core__Str.__drop(ptr %n1)
  br label %drop.skip15797

drop.skip16800:                                   ; preds = %drop.call16801, %drop.skip15797
  call void @llvm.lifetime.end.p0(i64 16, ptr %bfn)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %bf0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_arm)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_err)
  call void @llvm.lifetime.end.p0(i64 16, ptr %t_ok)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %s0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %fb)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p2)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %p0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %e0)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n1)
  call void @llvm.lifetime.end.p0(i64 16, ptr %n0)
  call void @__ls_flush_out()
  ret i32 0

drop.call16801:                                   ; preds = %drop.skip15797
  call void @std_core_str_core__Str.__drop(ptr %n0)
  br label %drop.skip16800
}

define void @__ls_global_stmts() {
entry:
  ret void
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

define i32 @"Vec(std_core_str_core__Str).len"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %field = getelementptr inbounds %"Vec(std_core_str_core__Str)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  ret i32 %len
}

define void @"Vec(Block() -> int).reserve"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %n = alloca i32, align 4
  %need = alloca i32, align 4
  store i32 %1, ptr %need, align 4
  %need1 = load i32, ptr %need, align 4
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field, align 4
  %sle = icmp sle i32 %need1, %cap
  br i1 %sle, label %if.then, label %if.merge

if.then:                                          ; preds = %entry
  ret void

if.merge:                                         ; preds = %entry
  %field2 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 2
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
  %field11 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field11, align 8
  %n12 = load i32, ptr %n, align 4
  %widen.sext = sext i32 %n12 to i64
  %mul13 = mul nsw i64 %widen.sext, ptrtoint (ptr getelementptr ({ ptr, ptr }, ptr null, i32 1) to i64)
  %2 = call ptr @realloc(ptr %data, i64 %mul13)
  %field.ptr = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  store ptr %2, ptr %field.ptr, align 8
  %n14 = load i32, ptr %n, align 4
  %field.ptr15 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 2
  store i32 %n14, ptr %field.ptr15, align 4
  ret void
}

define void @"Vec(Block() -> int).push"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, { ptr, ptr } %1) {
entry:
  %x = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %1, ptr %x, align 8
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %add = add nsw i32 %len, 1
  call void @"Vec(Block() -> int).reserve"(ptr %0, i32 %add)
  %x1 = load { ptr, ptr }, ptr %x, align 8
  %field2 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %field3 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len4 = load i32, ptr %field3, align 4
  %pis.idx = sext i32 %len4 to i64
  %pis.ep = getelementptr { ptr, ptr }, ptr %data, i64 %pis.idx
  store { ptr, ptr } %x1, ptr %pis.ep, align 8
  %field5 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len6 = load i32, ptr %field5, align 4
  %add7 = add nsw i32 %len6, 1
  %field.ptr = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  store i32 %add7, ptr %field.ptr, align 4
  ret void
}

define void @"Vec(Block() -> int).__from_list"(ptr noalias nocapture nonnull align 8 dereferenceable(16) %0, { ptr, ptr } %1) {
entry:
  %x = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %1, ptr %x, align 8
  %x1 = load { ptr, ptr }, ptr %x, align 8
  call void @"Vec(Block() -> int).push"(ptr %0, { ptr, ptr } %x1)
  ret void
}

define %"Vec(Block() -> int)" @"Vec(Block() -> int).copy"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  %sl.tmp = alloca %"Vec(Block() -> int)", align 8
  %var.moved = alloca i1, align 1
  %out = alloca %"Vec(Block() -> int)", align 8
  call void @llvm.lifetime.start.p0(i64 16, ptr %out)
  store i1 false, ptr %var.moved, align 1
  store %"Vec(Block() -> int)" zeroinitializer, ptr %out, align 8
  store %"Vec(Block() -> int)" zeroinitializer, ptr %sl.tmp, align 8
  %sl.val = load %"Vec(Block() -> int)", ptr %sl.tmp, align 8
  store %"Vec(Block() -> int)" %sl.val, ptr %out, align 8
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  call void @"Vec(Block() -> int).reserve"(ptr %out, i32 %len)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field2 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len3 = load i32, ptr %field2, align 4
  %slt = icmp slt i32 %i1, %len3
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field4 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field4, align 8
  %i5 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i5 to i64
  %ptr.elem.ptr = getelementptr { ptr, ptr }, ptr %data, i64 %lp.idx
  %dup.src = load { ptr, ptr }, ptr %ptr.elem.ptr, align 8
  %bc.fn = extractvalue { ptr, ptr } %dup.src, 0
  %bc.env = extractvalue { ptr, ptr } %dup.src, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

for.update:                                       ; preds = %bc.cont
  %i6 = load i32, ptr %i, align 4
  %add = add nsw i32 %i6, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %out7 = load %"Vec(Block() -> int)", ptr %out, align 8
  ret %"Vec(Block() -> int)" %out7

bc.clone:                                         ; preds = %for.body
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %for.body
  %bc.envphi = phi ptr [ null, %for.body ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  call void @"Vec(Block() -> int).push"(ptr %out, { ptr, ptr } %bc.renv)
  br label %for.update
}

define %"Vec(Block() -> int)" @"Vec(Block() -> int).__clone"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0) {
entry:
  %call = call %"Vec(Block() -> int)" @"Vec(Block() -> int).copy"(ptr %0)
  ret %"Vec(Block() -> int)" %call
}

define void @"Vec(Block() -> int).__drop"(ptr nocapture nonnull align 8 dereferenceable(16) %0) {
entry:
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.update, %entry
  %i1 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %slt = icmp slt i32 %i1, %len
  br i1 %slt, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %field2 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field2, align 8
  %i3 = load i32, ptr %i, align 4
  %lp.idx = sext i32 %i3 to i64
  %ptr.elem.ptr = getelementptr { ptr, ptr }, ptr %data, i64 %lp.idx
  %blk.old.load = load { ptr, ptr }, ptr %ptr.elem.ptr, align 8
  %blk.old.env = extractvalue { ptr, ptr } %blk.old.load, 1
  %rel.env.nn = icmp ne ptr %blk.old.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

for.update:                                       ; preds = %rel.cont
  %i4 = load i32, ptr %i, align 4
  %add = add nsw i32 %i4, 1
  store i32 %add, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %field5 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 2
  %cap = load i32, ptr %field5, align 4
  %sgt = icmp sgt i32 %cap, 0
  br i1 %sgt, label %if.then, label %if.merge

rel.dec:                                          ; preds = %for.body
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %blk.old.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %blk.old.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %blk.old.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %blk.old.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %for.body
  br label %for.update

if.then:                                          ; preds = %for.end
  %field6 = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data7 = load ptr, ptr %field6, align 8
  call void @free(ptr %data7)
  br label %if.merge

if.merge:                                         ; preds = %if.then, %for.end
  ret void
}

define { ptr, ptr } @"Vec(Block() -> int).get!"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %tmp = alloca { ptr, ptr }, align 8
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  call void @llvm.lifetime.start.p0(i64 16, ptr %tmp)
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 0
  %data = load ptr, ptr %field, align 8
  %i1 = load i32, ptr %i, align 4
  %pi.idx = sext i32 %i1 to i64
  %ptr.idx = getelementptr { ptr, ptr }, ptr %data, i64 %pi.idx
  %ptr.elem = load { ptr, ptr }, ptr %ptr.idx, align 8
  store { ptr, ptr } %ptr.elem, ptr %tmp, align 8
  %tmp2 = load { ptr, ptr }, ptr %tmp, align 8
  ret { ptr, ptr } %tmp2
}

define void @"Option(Block() -> int).__drop"(ptr %self) {
entry:
  %disc.p = getelementptr inbounds %"Option(Block() -> int)", ptr %self, i32 0, i32 0
  %disc = load i8, ptr %disc.p, align 1, !range !0
  %payload.p = getelementptr inbounds %"Option(Block() -> int)", ptr %self, i32 0, i32 1
  switch i8 %disc, label %drop.end [
    i8 1, label %drop.case
  ]

drop.end:                                         ; preds = %rel.cont, %entry
  ret void

drop.case:                                        ; preds = %entry
  %drop.field = getelementptr inbounds { { ptr, ptr } }, ptr %payload.p, i32 0, i32 0
  %blk.old.load = load { ptr, ptr }, ptr %drop.field, align 8
  %blk.old.env = extractvalue { ptr, ptr } %blk.old.load, 1
  %rel.env.nn = icmp ne ptr %blk.old.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %drop.case
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %blk.old.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %blk.old.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %blk.old.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %blk.old.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %drop.case
  br label %drop.end
}

define %"Option(Block() -> int)" @"Vec(Block() -> int).get"(ptr nocapture nonnull readonly align 8 dereferenceable(16) %0, i32 %1) {
entry:
  %enum.ctor3 = alloca %"Option(Block() -> int)", align 8
  %enum.ctor = alloca %"Option(Block() -> int)", align 8
  %i = alloca i32, align 4
  store i32 %1, ptr %i, align 4
  %i1 = load i32, ptr %i, align 4
  %slt = icmp slt i32 %i1, 0
  br i1 %slt, label %sc.merge, label %sc.rhs

sc.rhs:                                           ; preds = %entry
  %i2 = load i32, ptr %i, align 4
  %field = getelementptr inbounds %"Vec(Block() -> int)", ptr %0, i32 0, i32 1
  %len = load i32, ptr %field, align 4
  %sge = icmp sge i32 %i2, %len
  br label %sc.merge

sc.merge:                                         ; preds = %sc.rhs, %entry
  %sc = phi i1 [ %slt, %entry ], [ %sge, %sc.rhs ]
  br i1 %sc, label %if.then, label %if.merge

if.then:                                          ; preds = %sc.merge
  %2 = call ptr @memset(ptr %enum.ctor, i32 0, i64 24)
  %disc.p = getelementptr inbounds %"Option(Block() -> int)", ptr %enum.ctor, i32 0, i32 0
  store i8 0, ptr %disc.p, align 1
  %enum.val = load %"Option(Block() -> int)", ptr %enum.ctor, align 8
  ret %"Option(Block() -> int)" %enum.val

if.merge:                                         ; preds = %sc.merge
  %3 = call ptr @memset(ptr %enum.ctor3, i32 0, i64 24)
  %disc.p4 = getelementptr inbounds %"Option(Block() -> int)", ptr %enum.ctor3, i32 0, i32 0
  store i8 1, ptr %disc.p4, align 1
  %payload.p = getelementptr inbounds %"Option(Block() -> int)", ptr %enum.ctor3, i32 0, i32 1
  %i5 = load i32, ptr %i, align 4
  %call = call { ptr, ptr } @"Vec(Block() -> int).get!"(ptr %0, i32 %i5)
  %field.p = getelementptr inbounds { { ptr, ptr } }, ptr %payload.p, i32 0, i32 0
  %bc.fn = extractvalue { ptr, ptr } %call, 0
  %bc.env = extractvalue { ptr, ptr } %call, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %if.merge
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %if.merge
  %bc.envphi = phi ptr [ null, %if.merge ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %field.p, align 8
  %enum.val6 = load %"Option(Block() -> int)", ptr %enum.ctor3, align 8
  ret %"Option(Block() -> int)" %enum.val6
}

declare i32 @__ls_fstr_format(ptr %0, i64 %1, ptr %2, ...)

define %"Result(std_core_str_core__Str,std_core_str_core__Str)" @"Result(std_core_str_core__Str,std_core_str_core__Str).__clone"(ptr %self) {
entry:
  %uc.self4 = alloca %std_core_str_core__Str, align 8
  %uc.self = alloca %std_core_str_core__Str, align 8
  %ec.tmp = alloca %"Result(std_core_str_core__Str,std_core_str_core__Str)", align 8
  %ec.orig = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %self, align 8
  store %"Result(std_core_str_core__Str,std_core_str_core__Str)" %ec.orig, ptr %ec.tmp, align 8
  %ec.discp = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %ec.tmp, i32 0, i32 0
  %ec.disc = load i8, ptr %ec.discp, align 1, !range !0
  %ec.payp = getelementptr inbounds %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %ec.tmp, i32 0, i32 1
  switch i8 %ec.disc, label %ec.end [
    i8 0, label %ec.case
    i8 1, label %ec.case1
  ]

ec.end:                                           ; preds = %ec.case1, %ec.case, %entry
  %ec.r = load %"Result(std_core_str_core__Str,std_core_str_core__Str)", ptr %ec.tmp, align 8
  ret %"Result(std_core_str_core__Str,std_core_str_core__Str)" %ec.r

ec.case:                                          ; preds = %entry
  %ec.fp = getelementptr inbounds { %std_core_str_core__Str }, ptr %ec.payp, i32 0, i32 0
  %ec.oldsv = load %std_core_str_core__Str, ptr %ec.fp, align 8
  store %std_core_str_core__Str %ec.oldsv, ptr %uc.self, align 8
  %uc.r = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self)
  store %std_core_str_core__Str %uc.r, ptr %ec.fp, align 8
  br label %ec.end

ec.case1:                                         ; preds = %entry
  %ec.fp2 = getelementptr inbounds { %std_core_str_core__Str }, ptr %ec.payp, i32 0, i32 0
  %ec.oldsv3 = load %std_core_str_core__Str, ptr %ec.fp2, align 8
  store %std_core_str_core__Str %ec.oldsv3, ptr %uc.self4, align 8
  %uc.r5 = call %std_core_str_core__Str @std_core_str_core__Str.__clone(ptr %uc.self4)
  store %std_core_str_core__Str %uc.r5, ptr %ec.fp2, align 8
  br label %ec.end
}

define i32 @__closure_0(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %0, i32 0, i32 3
  %cap.fromenv = load i32, ptr %cap.gep, align 4
  %base = alloca i32, align 4
  store i32 %cap.fromenv, ptr %base, align 4
  %base1 = load i32, ptr %base, align 4
  ret i32 %base1
}

define ptr @__env_clone_0(ptr %0) {
entry:
  %p = call ptr @malloc(i64 32)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, i32 }, ptr %p, i32 0, i32 3
  %cl.sv = load i32, ptr %cl.sslot, align 4
  store i32 %cl.sv, ptr %cl.dslot, align 4
  ret ptr %p
}

define i32 @__closure_1(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.fromenv = load { ptr, ptr }, ptr %cap.gep, align 8
  %b1 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %cap.fromenv, ptr %b1, align 8
  %b11 = load { ptr, ptr }, ptr %b1, align 8
  %blk.fn = extractvalue { ptr, ptr } %b11, 0
  %blk.env = extractvalue { ptr, ptr } %b11, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env)
  %add = add nsw i32 %blk.call, 1
  ret i32 %add
}

define void @__env_drop_1(ptr %0) {
entry:
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.blk = load { ptr, ptr }, ptr %cap.slot, align 8
  %cap.blk.env = extractvalue { ptr, ptr } %cap.blk, 1
  %rel.env.nn = icmp ne ptr %cap.blk.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %entry
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %cap.blk.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %cap.blk.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %cap.blk.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %cap.blk.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %entry
  ret void
}

define ptr @__env_clone_1(ptr %0) {
entry:
  %p = call ptr @malloc(i64 40)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 3
  %cl.sv.blk = load { ptr, ptr }, ptr %cl.sslot, align 8
  %bc.fn = extractvalue { ptr, ptr } %cl.sv.blk, 0
  %bc.env = extractvalue { ptr, ptr } %cl.sv.blk, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cl.dslot, align 8
  ret ptr %p
}

define i32 @__closure_2(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.fromenv = load { ptr, ptr }, ptr %cap.gep, align 8
  %b2 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %cap.fromenv, ptr %b2, align 8
  %b21 = load { ptr, ptr }, ptr %b2, align 8
  %blk.fn = extractvalue { ptr, ptr } %b21, 0
  %blk.env = extractvalue { ptr, ptr } %b21, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env)
  %add = add nsw i32 %blk.call, 1
  ret i32 %add
}

define void @__env_drop_2(ptr %0) {
entry:
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.blk = load { ptr, ptr }, ptr %cap.slot, align 8
  %cap.blk.env = extractvalue { ptr, ptr } %cap.blk, 1
  %rel.env.nn = icmp ne ptr %cap.blk.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %entry
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %cap.blk.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %cap.blk.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %cap.blk.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %cap.blk.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %entry
  ret void
}

define ptr @__env_clone_2(ptr %0) {
entry:
  %p = call ptr @malloc(i64 40)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 3
  %cl.sv.blk = load { ptr, ptr }, ptr %cl.sslot, align 8
  %bc.fn = extractvalue { ptr, ptr } %cl.sv.blk, 0
  %bc.env = extractvalue { ptr, ptr } %cl.sv.blk, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cl.dslot, align 8
  ret ptr %p
}

define i32 @__closure_3(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.fromenv = load { ptr, ptr }, ptr %cap.gep, align 8
  %b3 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %cap.fromenv, ptr %b3, align 8
  %b31 = load { ptr, ptr }, ptr %b3, align 8
  %blk.fn = extractvalue { ptr, ptr } %b31, 0
  %blk.env = extractvalue { ptr, ptr } %b31, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env)
  %add = add nsw i32 %blk.call, 1
  ret i32 %add
}

define void @__env_drop_3(ptr %0) {
entry:
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.blk = load { ptr, ptr }, ptr %cap.slot, align 8
  %cap.blk.env = extractvalue { ptr, ptr } %cap.blk, 1
  %rel.env.nn = icmp ne ptr %cap.blk.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %entry
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %cap.blk.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %cap.blk.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %cap.blk.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %cap.blk.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %entry
  ret void
}

define ptr @__env_clone_3(ptr %0) {
entry:
  %p = call ptr @malloc(i64 40)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 3
  %cl.sv.blk = load { ptr, ptr }, ptr %cl.sslot, align 8
  %bc.fn = extractvalue { ptr, ptr } %cl.sv.blk, 0
  %bc.env = extractvalue { ptr, ptr } %cl.sv.blk, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cl.dslot, align 8
  ret ptr %p
}

define i32 @__closure_4(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.fromenv = load { ptr, ptr }, ptr %cap.gep, align 8
  %b4 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %cap.fromenv, ptr %b4, align 8
  %b41 = load { ptr, ptr }, ptr %b4, align 8
  %blk.fn = extractvalue { ptr, ptr } %b41, 0
  %blk.env = extractvalue { ptr, ptr } %b41, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env)
  %add = add nsw i32 %blk.call, 1
  ret i32 %add
}

define void @__env_drop_4(ptr %0) {
entry:
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.blk = load { ptr, ptr }, ptr %cap.slot, align 8
  %cap.blk.env = extractvalue { ptr, ptr } %cap.blk, 1
  %rel.env.nn = icmp ne ptr %cap.blk.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %entry
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %cap.blk.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %cap.blk.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %cap.blk.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %cap.blk.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %entry
  ret void
}

define ptr @__env_clone_4(ptr %0) {
entry:
  %p = call ptr @malloc(i64 40)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 3
  %cl.sv.blk = load { ptr, ptr }, ptr %cl.sslot, align 8
  %bc.fn = extractvalue { ptr, ptr } %cl.sv.blk, 0
  %bc.env = extractvalue { ptr, ptr } %cl.sv.blk, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cl.dslot, align 8
  ret ptr %p
}

define i32 @__closure_5(ptr %0) {
entry:
  %cap.gep = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.fromenv = load { ptr, ptr }, ptr %cap.gep, align 8
  %b5 = alloca { ptr, ptr }, align 8
  store { ptr, ptr } %cap.fromenv, ptr %b5, align 8
  %b51 = load { ptr, ptr }, ptr %b5, align 8
  %blk.fn = extractvalue { ptr, ptr } %b51, 0
  %blk.env = extractvalue { ptr, ptr } %b51, 1
  %blk.call = call i32 %blk.fn(ptr %blk.env)
  %add = add nsw i32 %blk.call, 1
  ret i32 %add
}

define void @__env_drop_5(ptr %0) {
entry:
  %cap.slot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cap.blk = load { ptr, ptr }, ptr %cap.slot, align 8
  %cap.blk.env = extractvalue { ptr, ptr } %cap.blk, 1
  %rel.env.nn = icmp ne ptr %cap.blk.env, null
  br i1 %rel.env.nn, label %rel.dec, label %rel.cont

rel.dec:                                          ; preds = %entry
  %rel.rcslot = getelementptr inbounds { ptr, ptr, i64 }, ptr %cap.blk.env, i32 0, i32 2
  %rel.rc = load i64, ptr %rel.rcslot, align 8
  %rel.rc1 = sub i64 %rel.rc, 1
  store i64 %rel.rc1, ptr %rel.rcslot, align 8
  %rel.zero = icmp eq i64 %rel.rc1, 0
  br i1 %rel.zero, label %rel.dropchk, label %rel.cont

rel.dropchk:                                      ; preds = %rel.dec
  %rel.drop = load ptr, ptr %cap.blk.env, align 8
  %rel.has_drop = icmp ne ptr %rel.drop, null
  br i1 %rel.has_drop, label %rel.dropcall, label %rel.dofree

rel.dropcall:                                     ; preds = %rel.dropchk
  call void %rel.drop(ptr %cap.blk.env)
  br label %rel.dofree

rel.dofree:                                       ; preds = %rel.dropcall, %rel.dropchk
  call void @free(ptr %cap.blk.env)
  br label %rel.cont

rel.cont:                                         ; preds = %rel.dofree, %rel.dec, %entry
  ret void
}

define ptr @__env_clone_5(ptr %0) {
entry:
  %p = call ptr @malloc(i64 40)
  %cl.shdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 0
  %cl.dhdr = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 0
  %cl.hdr = load ptr, ptr %cl.shdr, align 8
  store ptr %cl.hdr, ptr %cl.dhdr, align 8
  %cl.shdr1 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 1
  %cl.dhdr2 = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 1
  %cl.hdr3 = load ptr, ptr %cl.shdr1, align 8
  store ptr %cl.hdr3, ptr %cl.dhdr2, align 8
  %cl.rcslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 2
  store i64 1, ptr %cl.rcslot, align 8
  %cl.sslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %0, i32 0, i32 3
  %cl.dslot = getelementptr inbounds { ptr, ptr, i64, { ptr, ptr } }, ptr %p, i32 0, i32 3
  %cl.sv.blk = load { ptr, ptr }, ptr %cl.sslot, align 8
  %bc.fn = extractvalue { ptr, ptr } %cl.sv.blk, 0
  %bc.env = extractvalue { ptr, ptr } %cl.sv.blk, 1
  %bc.isnull = icmp eq ptr %bc.env, null
  br i1 %bc.isnull, label %bc.cont, label %bc.clone

bc.clone:                                         ; preds = %entry
  %bc.cfslot = getelementptr inbounds ptr, ptr %bc.env, i64 1
  %bc.cf = load ptr, ptr %bc.cfslot, align 8
  %bc.newenv = call ptr %bc.cf(ptr %bc.env)
  br label %bc.cont

bc.cont:                                          ; preds = %bc.clone, %entry
  %bc.envphi = phi ptr [ null, %entry ], [ %bc.newenv, %bc.clone ]
  %bc.rfn = insertvalue { ptr, ptr } undef, ptr %bc.fn, 0
  %bc.renv = insertvalue { ptr, ptr } %bc.rfn, ptr %bc.envphi, 1
  store { ptr, ptr } %bc.renv, ptr %cl.dslot, align 8
  ret ptr %p
}

declare void @__ls_set_args(i32 %0, ptr %1)

declare void @__ls_flush_out()

attributes #0 = { cold noreturn }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!0 = !{i8 0, i8 3}
!1 = !{i8 0, i8 2}
