# diskhopper

A native storage analyzer and cleanup utility for macOS. Read-only analysis first, safe recovery-oriented cleanup second, with a hard-coded safety model as the final barrier.

```
scan      map every path and its real allocated size
report    classify storage as SAFE / REVIEW / PROTECTED
explain   why a path uses its space
clean     remove caches; REVIEW -> Trash, SAFE -> permanent only behind gates
```

## Safety model

Every path is classified on three axes, checked **again at delete time**:

- **SAFE** - deterministic, regenerable (build caches, system logs). Permanent deletion is allowed only with `--force`, and only if Time Machine has a backup destination.
- **REVIEW** - potentially removable, derive before assuming (node_modules, xcode data). Always goes to **Trash**, never permanent.
- **PROTECTED** - never offered for cleanup: user documents, home, system, secrets. 17 hard-coded protection roots support the policy; unclassified paths are protected by default.
- Project trees (`.git`, `package.json`, `Cargo.toml`, ...) are detected and protected as a single unit.

Deletion honors Trash-first semantics, refuses symlink targets and missing paths, re-canonicalizes nothing (the scanned tree is authoritative), and logs every session to an optional audit file. The cleaner never shells out - only direct filesystem/Cocoa APIs.

Nothing is ever recommended for deletion if a more-specific rule matches a protected root.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/tests/core_tests      # 41 checks
```

Requires macOS (CoreServices), CMake >= 3.20, a C++17 compiler.

## Usage

```sh
diskhopper scan ~                    # tree of sizes, JSON with --json
diskhopper report ~                  # SAFE / REVIEW / PROTECTED totals
diskhopper explain ~/Downloads       # what is this path really made of
diskhopper clean --review --yes ~    # send REVIEW caches to the Trash
diskhopper clean --safe --force --dry-run ~   # preview permanent deletes
```

`clean` requires an explicit category (`--safe` / `--review`), an explicit `--yes` to act, and gated `--force` for permanent deletion. SAFE items default to the Trash too. `--audit FILE` appends a per-path session log.

## Benchmarks

Measured on Apple Silicon, macOS 26, AppleClang 21, Release build. Allocated size uses `st_blocks * 512` (sparse-aware); hard links are counted once.

| Path | Items | Allocated | Scan time | Throughput |
| --- | --- | --- | --- | --- |
| `~/Documents` | 6,876 | - | 0.06 s | ~106k items/s |
| `~/` (home) | 864,813 | 50.2 GB | 26.78 s | ~32k items/s |

Home scan counts 864k items including 31.7k symlinks; scan speed is dominated by cold page cache and directory reads.

## Layout

```
core/     C++17 libraries (scanner, rules, classifier, cleaner, platform, safety)
cli/      diskhopper executable
macos/    (reserved for the SwiftUI app - v0.6)
rules/    (rule source data for generation)
docs/     (design notes)
tests/    core_tests - 41 checks via CTest
```

## Roadmap

- v0.1 scanner + JSON  - done
- v0.2 classifier, safety model, report/explain - done
- v0.3 cleaner with Trash-first + Time Machine gate - done
- v0.4 developer-edition rule packs
- v0.5 SwiftUI app
- v0.6 scheduled auto-clean