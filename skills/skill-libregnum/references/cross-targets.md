# Cross targets: Windows, Linux aarch64, Android (0.12.0)

The full guide is `docs/cross-compile.org` plus one page per target in `docs/platforms/`. This is what an agent needs to build, test and change them without breaking anything.

## Commands

```bash
make -j16                        # native: build/release, never a container
make -j16 WINDOWS=1              # build/release-windows-amd64 (MinGW-w64)
make -j16 ARM64=1 DEBUG=1        # build/debug-arm64 (gcc + Fedora aarch64 sysroot)
make -j16 ANDROID=1              # build/release-android-arm64 (NDK clang)
make -j16 ANDROID=1 apk APK_GAME=examples/game-chocolate-chip-clicker.c
make ANDROID=1 apk-install apk-run APK_GAME=...   # host adb + device
make ANDROID=1 test-emulator APK_GAME=...         # APK on an Android 15 emulator (podman + /dev/kvm)
make WINDOWS=1 test              # under Wine;  make ARM64=1 test: under qemu (slow: hours)
make ANDROID=1 test-apk test-shaders
make targets                     # list; make <switch> print-target | print-container
make test-build-system           # build-system tests + native no-regression gate
```

`LRG_TARGET=<os>-<arch>` is the canonical spelling of the switches. Asking for the host's own target (ARM64=1 on Asahi) is a native build.

## How it works (one paragraph)

`GNUmakefile` (read before `Makefile`) includes `build-aux/mk/target.mk`; for a cross target it runs every goal as one `make` inside a podman image built from `build-aux/containers/<target>/` (tag = hash of the recipe, `common/`, `build-aux/cross/`, base image), mounting the checkout at the same path with `--userns=keep-id`, forwarding `MAKEFLAGS` minus the jobserver. Host-only goals (`clean`, `print-target`, `targets`, `container-*`, adb goals) never dispatch; `NO_BUILD_CONTAINER=1` builds on the host. `config.mk` reads the target profile `build-aux/mk/targets/<target>.mk` (toolchain, `TARGET_PLATFORM`, forced defaults, test wrapper, skip lists). Cross builds put graylib/raylib/yaml-glib/crispy/mcp-glib under `$(BUILDDIR)/deps/`; native builds keep them in the submodules.

## A parent project reusing the cross builds

A project embedding libregnum (e.g. librewow, `deps/libregnum`) writes its own `GNUmakefile` that includes libregnum's `build-aux/mk/target.mk`, `dispatch.mk` and (when `LRG_DISPATCH`/`LRG_USE_CONTAINER` is 1) the target profile and `container.mk`; full example in `docs/cross-compile.org`, "Consuming the cross builds from another project". Knobs: `LRG_ROOT` (libregnum's dir, derived by `target.mk`), `LRG_CONTAINER_MOUNT_ROOT` (default `PROJECT_ROOT`), `LRG_CONTAINER_EXTRA_MOUNTS` (more dirs at the same path; `LRG_ROOT` is auto-added when outside), `LRG_CONTAINER_ENV` (variable *names*, passed as `-e NAME`), `LRG_EXTRA_HOST_SAFE_GOALS` (set before `dispatch.mk`). The image is libregnum's (same tag). Inside the container the parent's make runs `$(MAKE) -C deps/libregnum lib`; `LRG_IN_CONTAINER=1` and the inherited `LRG_BUILD_SUFFIX` put it in `build/<type>-<target>/`. Profiles refer to libregnum files through `$(LRG_ROOT)`, never `$(PROJECT_ROOT)`.

## Rules when changing the build

- **The native build's commands must not change.** Check with `make test-native-unchanged` (`EXCLUDE_ADDED=1` when the branch adds sources); on the host that recorded `tests/golden/native-make-n/` it also checks that stored baseline, and `make update-native-golden BASE=master` re-records it (commit both files) when a native change is intended. Guard cross-only flags with `ifeq ($(LRG_IS_CROSS),1)` or a profile variable that is empty natively, and append with `ifneq` guards (an empty `$(VAR)` in a recipe leaves a stray space and changes the command text). Don't change the text of a native recipe line, even a message.
- **pkg-config queries:** one missing module empties the whole answer. Windows has no `gio-unix-2.0`, `libdex-1` or `girepository-2.0`; Android has no libdex, libsoup or libpq. Ask for those conditionally (`LRG_GIO_UNIX_PKG`, `LRG_TEST_DEX_PKG`, `LIBPQ_PKG`).
- **A new target** = profile + image (ending with `lrg_smoke`) + cases in `tests/test-build-targets.sh` and `tests/test-build-dispatch.sh` + `docs/platforms/<target>.org` + rows in `docs/cross-compile.org`/`building.org`. Walkthrough: `docs/cross-compile.org`, "Adding a new target".
- **Tools as one word**: `CC`, `AR`, `PKG_CONFIG` are passed to the dependencies' makes; wrap flags in a script under `build-aux/cross/`.
- **Every download in an image is pinned** with a checksum (`versions.sh`).
- Shell scripts are bash, ShellCheck-clean (`make test-build-system` runs it), with `--help` examples.

## Rules when changing code

- Platform guards: `G_OS_WIN32`/`G_OS_UNIX`, `__linux__` (true on Android too) / `__ANDROID__`, `LRG_PLATFORM_{LINUX,WINDOWS,ANDROID}` (from `TARGET_PLATFORM`). POSIX file calls with a Windows equivalent: `src/lrg-portable-private.h` (`lrg_realpath()`, `S_ISLNK`).
- `gint64` in printf formats: `G_GINT64_FORMAT`, never `%ld` (long is 32-bit on Windows).
- Shaders start with `LRG_GLSL_HEADER` (`src/graphics/lrg-glsl-private.h`), stay in the GLSL 3.30 / ES 3.00 common subset (no implicit int->float, no double), arrays named `*_vs` / `*_fs`; `make ANDROID=1 test-shaders` compiles all of them both ways.
- Android: a game's `main()` calls `lrg_platform_prepare()` first (the `LRG_DEFINE_GAME_MODULE` main does it); data files are bundled with `APK_ASSETS=dir`.
- Android's clang: `-Wno-typedef-redefinition` is the one accepted gcc/clang gap (decided 2026-11-24); fix everything else in the code. graylib's Android flags carry the same.
- `SOUP=0` / `LIBPQ=0` builds keep the API and fail with documented errors (`docs/reference/error-codes.org`, "Features built out").

## Tests per target

- linux-arm64: `LRG_TEST_SKIP_PATHS` (emulation limits, with reasons) and `LRG_TEST_KNOWN_FAILURES` (real aarch64 bugs: three determinism breaks) in the profile; both `-s`'d, both listed at the end of the run.
- windows-amd64: POSIX-only suites are left out (`LRG_WINDOWS_UNPORTED_TESTS` in `tests/Makefile`); the rest run under Wine (`WINEPATH`, `WINEPREFIX` in the build dir).
- android-arm64: no unit-test runner; `test-apk` (structure, alignment, signature, manifest) and `test-shaders`; the game itself with `test-device` (adb) or `test-emulator` (pinned emulator in a container, `-gpu guest`; screenshot next to the APK).

## Traps

- `make -j all test` in one invocation races two library builds: run them separately.
- `pkill -f <pattern>` in a shell whose own command line contains the pattern kills that shell; kill by PID.
- qemu-user cannot start another aarch64 program (no binfmt): `g_test_trap_subprocess()` and spawned helpers fail there.
- Wine refuses a new prefix whose parent you do not own (the container's `/tmp`).
- The NDK headers define plain `inline` helpers for newer APIs; under gnu89 they are emitted out of line, so a file including them defines `__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` before any include (`src/core/lrg-platform.c`).
