# logknife

A tiny **C** CLI for day-to-day log work: follow a file (tail -f), filter lines, and highlight keywords.

## Features (v0.1)

- `follow` like `tail -f`
- `--include <pattern>` (repeatable)
- `--exclude <pattern>` (repeatable)
- `--highlight <word>` (repeatable)
- `--tail <n>`: print last N lines, then follow
- `--since <dur>`: approximate a tail window (e.g., `10m`, `2h`) using `--rate` (default `1 line/sec`)
- `--json`: colorize JSON-ish lines (strings/keys/numbers/bools) with **zero deps**
- `--json-key <key>`: emphasize a specific JSON key (repeatable)

## Build

### With CMake

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Use

Follow a log:

```bash
./build/logknife follow ./app.log --include "^ERROR" --exclude "health" --highlight ERROR --highlight WARN
```

Tail last 200 lines then follow:

```bash
./build/logknife follow ./app.log --tail 200
```

Approximate “last 10 minutes” as 600 lines (1 line/sec default):

```bash
./build/logknife follow ./app.log --since 10m --rate 1
```

JSON mode:

```bash
./build/logknife follow ./app.log --json --json-key requestId --json-key userId
```

## Regex support

### Default (built-in, dependency-free)

Patterns support:

- `^` match start of line
- `$` match end of line
- `.` any character
- `*` repetition (K&R style)

Examples:

- `^ERROR` → lines starting with ERROR
- `timeout$` → lines ending with timeout
- `warn.*retry` → warn … retry

### Optional: full regex with PCRE2

If you have PCRE2 installed:

```bash
cmake -S . -B build -DLOGKNIFE_USE_PCRE2=ON
cmake --build build
```

## Roadmap

- True time-based `--since` by parsing timestamps (ISO8601, etc.)
- Faster matching / better highlighting

## License

MIT
