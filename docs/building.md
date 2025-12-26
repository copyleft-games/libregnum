# Building Libregnum

This guide explains how to build Libregnum from source.

## Prerequisites

### Fedora / RHEL / CentOS

```bash
sudo dnf install \
    gcc \
    make \
    pkgconf \
    glib2-devel \
    gobject-introspection-devel \
    libdex-devel \
    libyaml-devel \
    json-glib-devel
```

### Ubuntu / Debian

```bash
sudo apt install \
    build-essential \
    pkg-config \
    libglib2.0-dev \
    gobject-introspection \
    libgirepository1.0-dev \
    libdex-1-dev \
    libyaml-dev \
    libjson-glib-dev
```

### Arch Linux

```bash
sudo pacman -S \
    base-devel \
    glib2 \
    gobject-introspection \
    libdex \
    libyaml \
    json-glib
```

## Getting the Source

```bash
git clone https://github.com/example/libregnum.git
cd libregnum
git submodule update --init --recursive
```

## Building

### Basic Build

```bash
make
```

This will:
1. Build submodule dependencies (graylib, yaml-glib)
2. Compile the Libregnum library (static and shared)
3. Generate GObject Introspection files (.gir and .typelib)

### Debug Build

```bash
make DEBUG=1
```

Enables debug symbols (`-g3`) and disables optimization (`-O0`).

### Build Targets

| Target | Description |
|--------|-------------|
| `make` | Build library (default) |
| `make lib` | Build library only |
| `make gir` | Generate GIR files |
| `make test` | Build and run tests |
| `make examples` | Build examples |
| `make docs` | Build documentation |
| `make install` | Install to PREFIX |
| `make clean` | Clean build artifacts |
| `make distclean` | Clean everything including deps |
| `make help` | Show all targets |

## Configuration

Edit `config.mk` to customize the build:

### Installation Paths

```makefile
PREFIX      := /usr/local
EXEC_PREFIX := $(PREFIX)
LIBDIR      := $(EXEC_PREFIX)/lib
INCLUDEDIR  := $(PREFIX)/include
```

### Build Options

```makefile
BUILD_STATIC   := 1    # Build static library
BUILD_SHARED   := 1    # Build shared library
BUILD_GIR      := 1    # Generate GIR files
BUILD_TESTS    := 1    # Build test suite
BUILD_EXAMPLES := 0    # Build examples
BUILD_DOCS     := 0    # Build documentation
```

### Debug Options

```makefile
DEBUG := 0             # Set to 1 for debug build
ENABLE_TRACE := 0      # Set to 1 for trace logging
```

## Installation

### System Installation

```bash
sudo make install
```

Default installation locations:
- Libraries: `/usr/local/lib/`
- Headers: `/usr/local/include/libregnum/`
- pkg-config: `/usr/local/lib/pkgconfig/`
- GIR: `/usr/local/share/gir-1.0/`
- Typelib: `/usr/local/lib/girepository-1.0/`

### Custom Prefix

```bash
make install PREFIX=/opt/libregnum
```

### Staged Installation (for packaging)

```bash
make install DESTDIR=/path/to/staging
```

## Using Libregnum

### pkg-config

After installation:

```bash
# Compiler flags
pkg-config --cflags libregnum-1

# Linker flags
pkg-config --libs libregnum-1
```

### In a Makefile

```makefile
CFLAGS += $(shell pkg-config --cflags libregnum-1)
LDFLAGS += $(shell pkg-config --libs libregnum-1)
```

### Without Installation

Set library paths before running:

```bash
export LD_LIBRARY_PATH="/path/to/libregnum/build/release/lib:$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="/path/to/libregnum/build/release:$PKG_CONFIG_PATH"
export GI_TYPELIB_PATH="/path/to/libregnum/build/release/gir:$GI_TYPELIB_PATH"
```

## Testing

Run the test suite:

```bash
make test
```

This builds and runs all unit tests using the GLib testing framework.

To run tests manually:

```bash
cd tests
LD_LIBRARY_PATH="../build/release/lib" ./test-engine --tap
```

## Troubleshooting

### Missing Dependencies

If `make` fails with missing headers, ensure all development packages are installed.

### Submodule Issues

```bash
git submodule update --init --recursive
make deps-clean
make deps
```

### Library Not Found at Runtime

Set `LD_LIBRARY_PATH`:

```bash
export LD_LIBRARY_PATH="/path/to/libregnum/build/release/lib:$LD_LIBRARY_PATH"
```

Or run `ldconfig` after installation:

```bash
sudo ldconfig
```

### GIR Not Found

Set `GI_TYPELIB_PATH`:

```bash
export GI_TYPELIB_PATH="/path/to/libregnum/build/release/gir:$GI_TYPELIB_PATH"
```

## Cross-Compilation

Not currently supported. Libregnum targets Linux x86_64 and aarch64.

## Build System Details

Libregnum uses GNU Make with a three-file structure:

- **Makefile** - Main build orchestration
- **config.mk** - Configuration variables
- **rules.mk** - Build rules and helper functions

The build system is modeled after graylib's proven pattern.
