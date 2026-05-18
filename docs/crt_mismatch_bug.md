# Bug Report: Static/Dynamic CRT Mismatch in JIT Mode (Windows)

## Summary

JIT tests `test_e4_io_basic` and `test_e4_io_seek` crash with
`STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) on Windows. Root cause: static CRT
(`/MT`) and dynamic CRT (`ucrtbase.dll`) `FILE*` objects are not interoperable.

## Symptoms

- JIT: crash (exit code 9, no user output).
- AOT: works perfectly.
- Crash occurs at the point where JIT-compiled code calls `ls_os_ftell64` or
  `ls_os_fseek64` (registered via `LLVMOrcAbsoluteSymbols` in `jit.c`).

## Root Cause

`ls.exe` was compiled with `CMAKE_MSVC_RUNTIME_LIBRARY = "MultiThreaded"`
(`/MT`, **static CRT**). This embeds a private copy of the C runtime into
`ls.exe`, including `fopen`, `_ftelli64`, `_fseeki64`, and all CRT internal
structures (notably the `FILE` struct layout and the CRT heap).

In JIT mode, LLJIT uses `LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess`
to resolve symbols from loaded DLLs. On Windows 10+, `ucrtbase.dll` (the
**dynamic CRT**) is always loaded. When the JIT module declares
`extern fn fopen(...)`, LLJIT resolves it to `ucrtbase.dll`'s `fopen` — the
dynamic CRT.

The `FILE*` returned by `ucrtbase.dll`'s `fopen` is then passed to
`ls_os_ftell64` (in `ls.exe`, registered as an AbsoluteSymbol). Inside this
function, `_ftelli64` is the **static CRT**'s version. The static CRT's
`_ftelli64` treats the `FILE*` as its own internal `FILE` struct, but the
memory layout and heap differ between CRT instances. This corrupts the GS
stack cookie, triggering `_invoke_watson` → `STATUS_STACK_BUFFER_OVERRUN`.

```
JIT code                 ls.exe (static CRT)       ucrtbase.dll (dynamic CRT)
────────                 ───────────────────        ──────────────────────────
fopen("x","rb") ──────────────────────────────────→ ucrtbase!fopen
  ↓ returns FILE* from dynamic CRT heap
  ↓
ls_os_ftell64(fp) ──→ _ftelli64(fp)  [static CRT]
                       ↓
                  Reads FILE* as static CRT struct
                  ↓ layout mismatch → GS cookie corrupt → CRASH
```

### Why other AbsoluteSymbol functions work fine

- `ls_os_perf_now()`: calls Win32 `QueryPerformanceCounter`, no CRT dependency.
- `ls_os_path_exists()`: calls Win32 `GetFileAttributesA`, no CRT dependency.
- `ls_os_pid()`: calls CRT `_getpid()` which has no cross-CRT state issues.

The bug is specific to functions that accept a `FILE*` created by one CRT
instance and pass it to another CRT instance's file operations.

## Fix

Changed `CMAKE_MSVC_RUNTIME_LIBRARY` from `"MultiThreaded..."` (`/MT`) to
`"MultiThreaded...DLL"` (`/MD`). This makes `ls.exe` use the same
`ucrtbase.dll` as the JIT-resolved symbols. No CRT mismatch possible.

LLVM 18 static libraries were built with `/MT`, so the linker emits warning
`LNK4098` (LIBCMT conflicts with MSVCRT). This is benign: LLVM's internal CRT
usage (allocations, I/O) is self-contained and does not share `FILE*` or heap
objects with LS runtime code. All 40 tests pass.

### Files changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | `/MT` → `/MD` |
| `src/main.c` | AOT linker command adds `-lmsvcrt -lucrt` for dynamic CRT imports |
| `std/io.ls` | `c.strlen(content)` → `content.length` (3 locations, separate bug) |

### Why not other approaches?

| Approach | Problem |
|----------|---------|
| Dynamic CRT detection in `os_win32.c` | Can't distinguish JIT vs AOT FILE* at runtime; `ucrtbase.dll` is always loaded on Win10+ |
| Register `fopen`/`fclose` as AbsoluteSymbols | Fixes FILE* but breaks `errno()` (JIT's `_errno()` reads dynamic CRT, AbsoluteSymbol `fopen` sets static CRT) |
| Direct `_ftelli64` in LS via `extern fn` | Platform-specific (`_ftelli64` is MSVC-only), requires `#if WINDOWS` in stdlib |
| Keep `/MT` + per-function dynamic dispatch | Complex, error-prone, every new CRT-touching wrapper needs the same treatment |

## Impact on other platforms

**Linux/macOS: no impact.** These platforms use dynamic libc (glibc / libSystem)
by default. There is only one CRT instance in the process, so JIT and
host-linked functions always share the same `FILE*` layout. The `/MD` vs `/MT`
distinction is Windows-specific.

## Broader implications for future stdlib development

Any `ls_os_*` function in `runtime/os_win32.c` that accepts or returns a CRT
object (`FILE*`, `errno`, locale state, etc.) is potentially affected by CRT
mismatch. With the `/MD` fix, this is no longer an issue because all code
shares the same CRT. However, if `/MT` is ever reintroduced (e.g., for
standalone deployment), the following categories need attention:

- **FILE* functions**: `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`,
  `fprintf`, `fgets`, `fflush`, etc.
- **errno**: set by CRT functions, read via `_errno()` thread-local accessor.
- **Heap**: `malloc`/`free`/`realloc` — objects allocated by one CRT cannot be
  freed by another.
- **Locale/codepage state**: `setlocale`, `_setmbcp`, etc.

Functions using only Win32 API (`CreateFile`, `GetFileAttributes`,
`QueryPerformanceCounter`, etc.) are immune — they bypass the CRT entirely.
