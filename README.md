# logknife

A tiny **C** CLI for day-to-day log work: follow a file (tail -f), filter lines, and highlight keywords.

> v0.1 is intentionally dependency-free and ships with a small built-in regex *subset* (`^`, `$`, `.`, `*`).

## Features

- `follow` like `tail -f`
- `--include <pattern>` (repeatable)
- `--exclude <pattern>` (repeatable)
- `--highlight <word>` (repeatable)
- ANSI color output (auto-enabled on Windows)

## Quick start

### Build (CMake)

```bash
cmake -S . -B build
cmake --build build -j
```

### Use

```bash
./build/logknife follow ./app.log --include "ERROR" --exclude "health" --highlight "ERROR" --highlight "WARN"
```

## Regex subset

Patterns support:

- `^` match start of line
- `$` match end of line
- `.` any character
- `*` repetition (K&R style)

Examples:

- `^ERROR` → lines starting with ERROR
- `timeout$` → lines ending with timeout
- `warn.*retry` → warn … retry

## Roadmap

- Full regex (PCRE2 optional)
- JSON line mode (pretty + key highlighting)
- `--since` for recent window reading

## License

MIT
