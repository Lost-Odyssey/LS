# LS — Linux Deployment Guide (Ubuntu 24.04 x86_64)

This document covers everything needed to build and run LS on Linux.  
Primary target: **Ubuntu 24.04 LTS (Noble Numbat), x86_64**.  
Secondary targets: Ubuntu 22.04, Debian 12, and other glibc-based distros should work with minor path adjustments.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [LLVM 18 — Online Installation (apt)](#2-llvm-18--online-installation-apt)
3. [LLVM 18 — Offline Installation](#3-llvm-18--offline-installation)
   - [Method A: Pre-built tarball (recommended)](#method-a-pre-built-tarball-recommended)
   - [Method B: Offline .deb packages](#method-b-offline-deb-packages)
   - [Method C: Build from source](#method-c-build-from-source)
4. [Build LS](#4-build-ls)
5. [Run Tests](#5-run-tests)
6. [Platform Differences vs Windows](#6-platform-differences-vs-windows)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. Prerequisites

The following packages must be available on the target machine **before** installing LLVM.  
On a fresh Ubuntu 24.04 installation most of them are already present; install any that are missing.

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \   # gcc, make, libc-dev
    cmake \             # >= 3.20 required
    ninja-build \       # recommended generator (faster than make)
    git \               # to clone the repo
    zlib1g-dev \        # zlib (LLVM uses it for bitcode compression)
    libzstd-dev         # zstd (optional but recommended for LLVM 18)
```

Verify cmake version:

```bash
cmake --version   # must print 3.20 or higher
```

If the system cmake is too old (< 3.20), install a newer one from [cmake.org/download](https://cmake.org/download/) or via snap:

```bash
sudo snap install cmake --classic
```

---

## 2. LLVM 18 — Online Installation (apt)

Use this method when the target machine has unrestricted internet access.

```bash
# Step 1: Add the official LLVM apt repository for Ubuntu 24.04 (noble)
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc

echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-18 main" \
    | sudo tee /etc/apt/sources.list.d/llvm-18.list

sudo apt-get update

# Step 2: Install LLVM 18 development headers + clang
sudo apt-get install -y llvm-18-dev clang-18

# Step 3: Verify the cmake config file is present
ls /usr/lib/llvm-18/lib/cmake/llvm/LLVMConfig.cmake
```

**LLVM_DIR** to pass to cmake:

```
/usr/lib/llvm-18/lib/cmake/llvm
```

---

## 3. LLVM 18 — Offline Installation

Choose the method that best matches your situation.

| Method | Download size | Install time | Best when |
|--------|--------------|--------------|-----------|
| A — pre-built tarball | ~900 MB | ~5 min | Recommended for most cases |
| B — offline .deb | ~800 MB | ~10 min | You have another Ubuntu 24.04 with internet |
| C — build from source | ~130 MB (src) | 1–3 hours | No pre-built binary available at all |

---

### Method A: Pre-built tarball (recommended)

#### Step 1 — Download on a machine with internet access

```bash
# Official LLVM 18.1.8 pre-built binary for Linux x86_64
# (Compiled against Ubuntu 22.04; fully compatible with Ubuntu 24.04)
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz

# Optional: verify checksum (compare against the SHA256 listed on the GitHub release page)
sha256sum clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz
```

#### Step 2 — Transfer to the offline machine

```bash
# Via USB drive
cp clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz /media/usb/

# Via scp (from any machine that can reach the target)
scp clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz user@offline-host:~/

# Via rsync
rsync -avz clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz user@offline-host:~/
```

#### Step 3 — Install on the offline machine

```bash
# Extract into /opt/llvm-18  (--strip-components=1 removes the top-level directory)
sudo mkdir -p /opt/llvm-18
sudo tar -xJf clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-22.04.tar.xz \
    -C /opt/llvm-18 \
    --strip-components=1

# Verify key files
ls /opt/llvm-18/lib/cmake/llvm/LLVMConfig.cmake   # cmake config
ls /opt/llvm-18/lib/libLLVMCore.a                 # static library
ls /opt/llvm-18/bin/llvm-config                   # config tool

# Optional: add clang/llvm tools to PATH
echo 'export PATH=/opt/llvm-18/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

**LLVM_DIR** to pass to cmake:

```
/opt/llvm-18/lib/cmake/llvm
```

---

### Method B: Offline .deb packages

Use this method when you have a second Ubuntu 24.04 machine with internet access that can prepare the packages.

#### Step 1 — On the internet-connected Ubuntu 24.04 machine, download all .deb files

```bash
# Add the LLVM apt source (same as online method)
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-18 main" \
    | sudo tee /etc/apt/sources.list.d/llvm-18.list
sudo apt-get update

# Create a staging directory and download every .deb (with recursive deps)
mkdir ~/llvm18-debs && cd ~/llvm18-debs

TARGET_PKGS="llvm-18-dev clang-18 libllvm18 llvm-18-runtime llvm-18"

for pkg in $TARGET_PKGS; do
    apt-get download $(apt-cache depends --recurse \
        --no-recommends --no-suggests --no-conflicts \
        --no-breaks --no-replaces --no-enhances \
        "$pkg" 2>/dev/null \
        | grep "^\w" | sort -u) 2>/dev/null || true
done
apt-get download $TARGET_PKGS

# Bundle into a single archive
cd ~ && tar -czf llvm18-debs.tar.gz llvm18-debs/
```

#### Step 2 — Transfer and install on the offline machine

```bash
# Transfer
scp llvm18-debs.tar.gz user@offline-host:~/

# On the offline machine:
tar -xzf llvm18-debs.tar.gz
cd llvm18-debs

# Install (dpkg does not resolve ordering, so run twice or use the fix-broken step)
sudo dpkg -i *.deb || true
# Fix any dependency ordering issues using only already-downloaded packages
sudo dpkg -i *.deb

# Verify
llvm-config-18 --version   # should print 18.1.x
```

**LLVM_DIR** to pass to cmake:

```
/usr/lib/llvm-18/lib/cmake/llvm
```

---

### Method C: Build from source

Use this as a last resort when no pre-built binary is available.

> **Resource requirements:** ~2 hours on an 8-core machine · 25 GB disk space · 8 GB RAM recommended

#### Step 1 — Download source archive on a machine with internet access

```bash
# Source-only archive — much smaller than the pre-built tarball (~130 MB)
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/llvm-project-18.1.8.src.tar.xz
```

#### Step 2 — Transfer to the offline machine (same as Method A step 2)

#### Step 3 — Configure and build on the offline machine

The cmake flags below build **only the components that LS needs**, cutting build time by roughly 70 % compared to a full LLVM build.

```bash
tar -xJf llvm-project-18.1.8.src.tar.xz
cd llvm-project-18.1.8.src

cmake -B build-llvm -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/llvm-18 \
    -DLLVM_TARGETS_TO_BUILD="X86" \        # only x86; skip ARM, RISC-V, etc.
    -DLLVM_ENABLE_PROJECTS="" \            # no clang, no lld — LS only needs the LLVM C API
    -DLLVM_BUILD_TOOLS=OFF \              # skip llvm-objdump, llvm-nm, etc.
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_ENABLE_ZLIB=ON \
    -DLLVM_ENABLE_ZSTD=ON \
    llvm                                   # point at the llvm/ sub-directory

# Build using all available CPU cores
cmake --build build-llvm -j$(nproc)

# Install to /opt/llvm-18
sudo cmake --install build-llvm
```

**LLVM_DIR** to pass to cmake:

```
/opt/llvm-18/lib/cmake/llvm
```

---

## 4. Build LS

Replace `<LLVM_DIR>` with the value from whichever installation method you used.

```bash
# Clone the repository
git clone https://github.com/<your-repo>/LS.git
cd LS

# --- Release build (recommended) ---
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR=<LLVM_DIR>

cmake --build build

# The compiler binary is at:
#   build/ls

# --- Debug build (AddressSanitizer enabled) ---
cmake -B build_dbg -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLLVM_DIR=<LLVM_DIR>

cmake --build build_dbg

# --- With codegen memory tracing (developer only) ---
cmake -B build_cg -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_DIR=<LLVM_DIR> \
    -DLS_CG_DEBUG=ON

cmake --build build_cg
```

### Make ls accessible system-wide (optional)

```bash
sudo ln -sf "$(pwd)/build/ls" /usr/local/bin/ls-lang

# Test
ls-lang run tests/samples/math_basic_test.ls
```

> **Note:** do not shadow the system `ls` command — use a different symlink name such as `ls-lang`.

---

## 5. Run Tests

```bash
cd build
ctest --output-on-failure

# Run a single test by name
ctest -R test_phase_f7_stress --output-on-failure

# Run with verbose output
ctest -V
```

### Copy stdlib next to the binary (required for io/math import tests)

The `import io` / `import math` module system looks for `stdlib/` relative to the `ls` binary.  
After building, copy the stdlib directory into the build output:

```bash
cp -r stdlib build/
```

### JIT quick smoke test

```bash
build/ls run tests/samples/math_basic_test.ls
build/ls run tests/samples/closure_f7_stress_test.ls
```

### AOT compile smoke test

```bash
build/ls compile tests/samples/math_basic_test.ls -o /tmp/math_test
/tmp/math_test
```

### Memcheck smoke test

```bash
build/ls run --memcheck tests/samples/memcheck_phase_a.ls
# Expected last line: [memcheck] SUMMARY: 0 leak(s) (0 bytes), 0 double-free, 0 invalid free
```

---

## 6. Platform Differences vs Windows

### Test suite

| Test | Windows | Linux | Notes |
|------|---------|-------|-------|
| `test_scanner` | ✅ | ✅ | |
| `test_parser` | ✅ | ✅ | |
| `test_types` | ✅ | ✅ | |
| `test_codegen` | ✅ | ✅ | |
| `test_jit` | ✅ | ✅ | |
| `test_ffi` | ✅ | ✅ | uses `dlopen`/`dlsym` on Linux |
| `test_module` | ✅ | ✅ | |
| `test_memory` | ✅ | ✅ | |
| `test_memcheck_aot` | ✅ | ✅ | AOT links `libls_memcheck.a` via `-L -l` |
| `test_extern_struct` | ✅ | ✅ | Phase E.1: basic extern struct declarations |
| `test_extern_struct_byval` | ✅ | ⏭ skipped | Windows x64 ABI-specific; System V AMD64 ABI differs |
| `test_e3_glue` … `test_phase_f7_stress` | ✅ | ✅ | All closure/io/math/condcomp tests |

### Code-level platform handling (already implemented)

| Area | Windows | Linux |
|------|---------|-------|
| Dynamic library loading | `LoadLibrary` / `GetProcAddress` / `FreeLibrary` | `dlopen` / `dlsym` / `dlclose` |
| Codegen FFI IR | emits `LoadLibraryA` + `GetProcAddress` calls | emits `dlopen` + `dlsym` calls |
| Executable location | `GetModuleFileNameA` | `readlink("/proc/self/exe")` |
| Default output name | `output.exe` / `output.obj` | `output` / `output.o` |
| AOT link command | `clang.exe … -llegacy_stdio_definitions` | `cc … -lm` |
| `--memcheck` archive | `ls_memcheck.lib` (absolute path) | `-L<dir> -lls_memcheck` |
| errno function name | `_errno` | `__errno_location` |
| Path separator | `\\` (also accepts `/`) | `/` |
| FFI shared library suffix | `.dll` (auto-appended) | `.so` (auto-appended) |
| Conditional compilation | `#if WINDOWS` active | `#if LINUX` active |

### FFI on Linux

LS's `load("libname")` automatically tries `libname.so` if the name has no extension.  
The `.so` file must be on the dynamic linker search path (`LD_LIBRARY_PATH` or `/etc/ld.so.conf`).

```ls
// Windows:  loads foo.dll
// Linux:    loads foo.so
lib foo = load("foo")
```

---

## 7. Troubleshooting

### `Could NOT find LLVM`

```
CMake Error: Could NOT find LLVM (missing: LLVM_DIR)
```

Make sure you pass `-DLLVM_DIR=<path>` and that `<path>/LLVMConfig.cmake` exists:

```bash
ls <LLVM_DIR>/LLVMConfig.cmake
```

### Linker error: undefined reference to `LLVMxxx`

The `LS_UNIX_EXTRA_LIBS` variable in `CMakeLists.txt` automatically pulls in `LLVM_SYSTEM_LIBS` (exported by the LLVM cmake config), `dl`, `m`, and `Threads::Threads`.  
If you still see missing symbols, print what LLVM reports:

```bash
/opt/llvm-18/bin/llvm-config --system-libs --link-static
```

Add any missing libraries to `target_link_libraries` in `CMakeLists.txt`.

### `error while loading shared libraries: libtinfo.so.5`

LLVM tools in the pre-built tarball may require `libtinfo.so.5`; Ubuntu 24.04 ships `.so.6`.  
LS itself is statically linked and is unaffected, but `llvm-config`, `clang`, etc. may fail:

```bash
sudo ln -sf /lib/x86_64-linux-gnu/libtinfo.so.6 \
            /lib/x86_64-linux-gnu/libtinfo.so.5
```

### `zlib not found` during cmake configure

```bash
sudo apt-get install -y zlib1g-dev   # online
# OR disable zlib in the LLVM source build:
cmake ... -DLLVM_ENABLE_ZLIB=OFF
```

### AOT-compiled binary links but crashes at runtime

The AOT path calls `cc` to link the `.o` file.  
Verify that `cc` resolves to a working compiler:

```bash
cc --version
```

If you installed clang from the tarball and prefer to use it as the linker driver:

```bash
sudo ln -sf /opt/llvm-18/bin/clang /usr/local/bin/cc
```

### JIT test failures on `test_ffi`

The FFI test loads a shared library.  
Ensure the test shared library (`.so`) was compiled for Linux; `.dll` files will not load.  
Check `LD_LIBRARY_PATH` or use an absolute path in the LS source file.

### `import io` / `import math` fails with "module not found"

Copy the stdlib directory next to the `ls` binary:

```bash
cp -r stdlib build/
```

The module resolver looks for `stdlib/<module>.ls` relative to the `ls` executable location.
