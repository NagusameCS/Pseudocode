# Building Pseudocode on Windows

This guide explains how to compile the Pseudocode VM natively on Windows.

## Prerequisites

### Option 1: MSYS2 (Recommended)

1. **Download and install MSYS2** from https://www.msys2.org/

2. **Open MSYS2 MINGW64** terminal (not the regular MSYS2 terminal)

3. **Install required packages:**
   ```bash
   pacman -Syu
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pcre2 make
   ```

### Option 2: Visual Studio (Alternative)

If you prefer Visual Studio, you'll need:
- Visual Studio 2019 or later with C++ workload
- vcpkg for managing dependencies

---

## Building with MSYS2

### Step 1: Clone the Repository

```bash
git clone https://github.com/NagusameCS/Pseudocode.git
cd Pseudocode/cvm
```

### Step 2: Build (Simple - Dynamic Linking)

This creates a binary that requires MSYS2 DLLs:

```bash
make release
```

The binary will be at `./pseudo.exe`

### Step 3: Build (Portable - Static Linking)

For a standalone executable with no DLL dependencies:

```bash
# Build with static linking
gcc -O3 -flto -fomit-frame-pointer -DNDEBUG -DPCRE2_STATIC \
    -Wall -Wextra -std=gnu11 \
    -o pseudo.exe \
    main.c lexer.c compiler.c vm.c memory.c import.c \
    jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c \
    -Wl,-Bstatic -lpcre2-8 -Wl,-Bdynamic -lm -static-libgcc
```

**Note:** This requires the static pcre2 library. If you get linker errors, you may need to build pcre2 from source (see below).

---

## Building PCRE2 from Source (for fully static builds)

If the static pcre2 library isn't available, build it yourself:

### Step 1: Download PCRE2

```bash
curl -L https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz | tar xz
cd pcre2-10.44
```

### Step 2: Install CMake

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-make
```

### Step 3: Build PCRE2 as Static Library

```bash
cmake -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DPCRE2_BUILD_PCRE2_8=ON \
    -DPCRE2_BUILD_PCRE2_16=OFF \
    -DPCRE2_BUILD_PCRE2_32=OFF \
    -DPCRE2_SUPPORT_JIT=ON \
    -DPCRE2_BUILD_PCRE2GREP=OFF \
    -DPCRE2_BUILD_TESTS=OFF \
    .

mingw32-make -j4
```

### Step 4: Build Pseudocode with Custom PCRE2

```bash
cd ../cvm

gcc -O3 -flto -fomit-frame-pointer -DNDEBUG -DPCRE2_STATIC \
    -Wall -Wextra -std=gnu11 \
    -I../pcre2-10.44/src \
    -o pseudo.exe \
    main.c lexer.c compiler.c vm.c memory.c import.c \
    jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c \
    ../pcre2-10.44/libpcre2-8.a -lm -static-libgcc
```

---

## Verifying the Build

### Check DLL Dependencies

```bash
# In MSYS2
objdump -p pseudo.exe | grep "DLL Name"
```

A fully static build should show only Windows system DLLs:
```
DLL Name: KERNEL32.dll
DLL Name: msvcrt.dll
```

If you see `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`, or `libpcre2-8.dll`, the build isn't fully static.

### Test the Binary

```bash
./pseudo.exe ../examples/hello.pseudo
./pseudo.exe ../examples/fibonacci.pseudo
```

---

## Troubleshooting

### Error: "cannot execute binary file"
- Make sure you're running from MSYS2 MINGW64, not regular Command Prompt

### Error: 3221225781 (0xC0000135)
- This means a DLL is missing
- Either rebuild with static linking, or copy these DLLs to the same folder as pseudo.exe:
  - `libgcc_s_seh-1.dll` (from `C:\msys64\mingw64\bin\`)
  - `libwinpthread-1.dll` (from `C:\msys64\mingw64\bin\`)
  - `libpcre2-8.dll` (from `C:\msys64\mingw64\bin\`)

### Linker errors about pcre2
- Install pcre2: `pacman -S mingw-w64-x86_64-pcre2`
- Or build from source (see above)

### "PCRE2_STATIC" undefined
- Add `-DPCRE2_STATIC` to your compiler flags

---

## Quick Copy-Paste Commands

### Full static build (single command):

```bash
# Run from cvm/ directory in MSYS2 MINGW64
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pcre2 && \
gcc -O3 -DNDEBUG -DPCRE2_STATIC -std=gnu11 \
    -o pseudo.exe \
    main.c lexer.c compiler.c vm.c memory.c import.c \
    jit_trace.c trace_recorder.c trace_regalloc.c trace_codegen.c tensor.c \
    -lpcre2-8 -lm -static-libgcc && \
./pseudo.exe ../examples/hello.pseudo
```

### Bundle DLLs approach (if static doesn't work):

```bash
# After building with: make release
cp /mingw64/bin/libgcc_s_seh-1.dll .
cp /mingw64/bin/libwinpthread-1.dll .
cp /mingw64/bin/libpcre2-8.dll .
```

Then distribute `pseudo.exe` along with these 3 DLL files.
