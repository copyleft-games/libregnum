# Cross-Compilation Guide

Libregnum supports cross-compilation for Windows using MinGW-w64.

## Prerequisites

### Fedora

```bash
sudo dnf install \
    mingw64-gcc \
    mingw64-glib2 \
    mingw64-pkg-config \
    wine  # For testing
```

### Ubuntu/Debian

```bash
sudo apt install \
    gcc-mingw-w64-x86-64 \
    mingw-w64-x86-64-dev
```

Note: Ubuntu may require additional setup for MinGW GLib.

## Building for Windows

### Basic Windows Build

```bash
make WINDOWS=1
```

This automatically:
- Sets `CROSS=x86_64-w64-mingw32`
- Uses MinGW toolchain (`x86_64-w64-mingw32-gcc`, etc.)
- Links Windows libraries (`-lopengl32 -lgdi32 -lwinmm -lshell32`)
- Produces `.dll` and `.exe` files
- Disables GIR generation (not supported for cross-compilation)

### Windows + Steam

```bash
make WINDOWS=1 STEAM=1
```

This links against `steam_api64.dll` from the Steamworks SDK.

### Debug Windows Build

```bash
make WINDOWS=1 DEBUG=1
```

## Output Files

| Platform | Static Library | Shared Library | Executables |
|----------|---------------|----------------|-------------|
| Linux | `libregnum.a` | `libregnum.so` | `example` |
| Windows | `libregnum.a` | `regnum.dll` | `example.exe` |

Windows builds also produce an import library: `libregnum.dll.a`

## Platform Detection

The build system creates a `.platform-marker` file to track the target platform. When switching between platforms, an automatic clean is performed:

```bash
make               # Builds for Linux
make WINDOWS=1     # Detects platform change, cleans, then builds for Windows
make               # Detects platform change, cleans, then builds for Linux
```

## Manual Cross-Compilation

For other cross-compilers:

```bash
make CROSS=aarch64-linux-gnu   # ARM64 Linux
make CROSS=arm-linux-gnueabihf # ARM32 Linux (Raspberry Pi)
```

The `CROSS` variable sets the toolchain prefix for:
- `CC` → `$(CROSS)-gcc`
- `AR` → `$(CROSS)-ar`
- `RANLIB` → `$(CROSS)-ranlib`
- `PKG_CONFIG` → `$(CROSS)-pkg-config`

## Testing Windows Builds

### Using Wine

```bash
# Run a test executable
wine ./build/release/tests/test-registry.exe

# Run with library path
WINEPATH="./build/release/lib;./deps/graylib/build/lib;./deps/yaml-glib/build" \
    wine ./build/release/examples/hello.exe
```

### Using a Windows VM

Copy the following to your Windows machine:
- `build/release/lib/regnum.dll`
- `deps/graylib/build/lib/graylib.dll`
- `deps/yaml-glib/build/yaml-glib.dll`
- `deps/steamworks_sdk/sdk/redistributable_bin/win64/steam_api64.dll` (if STEAM=1)
- Your executable

## Distributing Windows Builds

### Required DLLs

When distributing your game, include:

1. **Libregnum DLLs**:
   - `regnum.dll`
   - `graylib.dll`
   - `yaml-glib.dll`

2. **GLib Runtime** (from MinGW or MSYS2):
   - `libglib-2.0-0.dll`
   - `libgobject-2.0-0.dll`
   - `libgio-2.0-0.dll`
   - `libintl-8.dll`
   - `libffi-8.dll`
   - `libiconv-2.dll`
   - `libpcre2-8-0.dll`
   - `zlib1.dll`

3. **Steam** (if STEAM=1):
   - `steam_api64.dll`

4. **Raylib dependencies** (usually statically linked, but check):
   - OpenGL32.dll (system)
   - Other system DLLs

### Finding MinGW DLLs

On Fedora, MinGW DLLs are in:
```bash
/usr/x86_64-w64-mingw32/sys-root/mingw/bin/
```

### Creating a Distribution Package

```bash
#!/bin/bash
# Package a Windows distribution

DIST_DIR="dist/windows"
mkdir -p "$DIST_DIR"

# Copy game executable
cp build/release/bin/mygame.exe "$DIST_DIR/"

# Copy library DLLs
cp build/release/lib/regnum.dll "$DIST_DIR/"
cp deps/graylib/build/lib/graylib.dll "$DIST_DIR/"
cp deps/yaml-glib/build/yaml-glib.dll "$DIST_DIR/"

# Copy MinGW runtime DLLs
MINGW_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"
cp "$MINGW_BIN/libglib-2.0-0.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libgobject-2.0-0.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libgio-2.0-0.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libintl-8.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libffi-8.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libiconv-2.dll" "$DIST_DIR/"
cp "$MINGW_BIN/libpcre2-8-0.dll" "$DIST_DIR/"
cp "$MINGW_BIN/zlib1.dll" "$DIST_DIR/"

# Copy Steam DLL (if applicable)
if [ "$STEAM" = "1" ]; then
    cp deps/steamworks_sdk/sdk/redistributable_bin/win64/steam_api64.dll "$DIST_DIR/"
fi

# Copy game data
cp -r data "$DIST_DIR/"

echo "Distribution created in $DIST_DIR"
```

## Troubleshooting

### Missing pkg-config for MinGW

Ensure MinGW pkg-config is installed and configured:

```bash
# Check if MinGW pkg-config works
x86_64-w64-mingw32-pkg-config --cflags glib-2.0
```

If not working, you may need to set:

```bash
export PKG_CONFIG_PATH="/usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="/usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig"
```

### GLib Not Found

Install MinGW GLib:

```bash
# Fedora
sudo dnf install mingw64-glib2

# Or build from source for other distros
```

### Steam API Not Found

Ensure the Steamworks SDK submodule is initialized:

```bash
git submodule update --init --recursive
```

### Wine Crashes

Windows builds may need the Visual C++ runtime. Install `winetricks`:

```bash
winetricks vcrun2019
```

## Continuous Integration

Example GitHub Actions workflow:

```yaml
name: Build

on: [push, pull_request]

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y gcc make pkgconf libglib2.0-dev
      - name: Build
        run: make

  build-windows:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y gcc-mingw-w64-x86-64
      - name: Build
        run: make WINDOWS=1
```

## Build Flags Reference

| Flag | Default | Description |
|------|---------|-------------|
| `WINDOWS` | 0 | Enable Windows cross-compilation |
| `CROSS` | (empty) | Cross-compiler prefix |
| `STEAM` | 0 | Enable Steam SDK integration |
| `DEBUG` | 0 | Debug build with symbols |
| `BUILD_GIR` | 1 | Generate GIR files (disabled for cross) |
