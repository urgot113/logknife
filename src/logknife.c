#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#endif

// -------------------------
// Minimal regex (K&R / Pike style)
// Supports: ^ $ . *
// Reference: The Practice of Programming (classic)
// -------------------------

static int matchhere(const char *re, const char *text);

static int matchstar(int c, const char *re, const char *text) {
  const char *t = text;
  do {
    if (matchhere(re, t)) return 1;
  } while (*t != '\0' && (*t++ == c || c == '.'));
  return 0;
}

static int matchhere(const char *re, const char *text) {
  if (re[0] == '\0') return 1;
  if (re[0] == '$' && re[1] == '\0') return *text == '\0';
  if (re[1] == '*') return matchstar(re[0], re + 2, text);
  if (*text != '\0' && (re[0] == '.' || re[0] == *text))
    return matchhere(re + 1, text + 1);
  return 0;
}

static int matchre(const char *re, const char *text) {
  if (re[0] == '^') return matchhere(re + 1, text);
  do {
    if (matchhere(re, text)) return 1;
  } while (*text++ != '\0');
  return 0;
}

// -------------------------
// ANSI color helpers
// -------------------------

static void enable_ansi_if_windows(void) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE) return;

  DWORD mode = 0;
  if (!GetConsoleMode(h, &mode)) return;
  mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(h, mode);
#endif
}

static void print_highlighted(const char *line, const char **words, size_t word_count) {
  // Very simple highlighter: splits by matches and wraps keywords.
  // Not regex; exact substring match.
  // Colors: red for ERROR, yellow for WARN, cyan for others.

  if (word_count == 0) {
    fputs(line, stdout);
    return;
  }

  const char *p = line;
  while (*p) {
    size_t best_i = (size_t)-1;
    const char *best_pos = NULL;

    for (size_t i = 0; i < word_count; i++) {
      const char *pos = strstr(p, words[i]);
      if (!pos) continue;
      if (!best_pos || pos < best_pos) {
        best_pos = pos;
        best_i = i;
      }
    }

    if (!best_pos) {
      fputs(p, stdout);
      return;
    }

    fwrite(p, 1, (size_t)(best_pos - p), stdout);

    const char *w = words[best_i];
    const char *color = "\x1b[36m"; // cyan
    if (strcasecmp(w, "ERROR") == 0) color = "\x1b[31m";
    else if (strcasecmp(w, "WARN") == 0 || strcasecmp(w, "WARNING") == 0) color = "\x1b[33m";

    fputs(color, stdout);
    fputs(w, stdout);
    fputs("\x1b[0m", stdout);

    p = best_pos + strlen(w);
  }
}

// Windows doesn't have strcasecmp in MSVC by default.
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// -------------------------
// CLI options
// -------------------------

typedef struct {
  const char **include;
  size_t include_count;
  const char **exclude;
  size_t exclude_count;
  const char **highlight;
  size_t highlight_count;
  const char *path;
  int interval_ms;
} opts_t;

static void usage(FILE *out) {
  fprintf(out,
    "logknife (v0.1)\n"
    "\n"
    "Usage:\n"
    "  logknife follow <file> [--include PATTERN] [--exclude PATTERN] [--highlight WORD]\n"
    "\n"
    "Options:\n"
    "  --include <pattern>    regex-subset filter (repeatable)\n"
    "  --exclude <pattern>    regex-subset negative filter (repeatable)\n"
    "  --highlight <word>     highlight exact words (repeatable)\n"
    "  --interval <ms>        polling interval (default: 200)\n"
  );
}

static void push_str(const char ***arr, size_t *count, const char *s) {
  *arr = (const char**)realloc((void*)*arr, sizeof(char*) * (*count + 1));
  if (!*arr) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  (*arr)[*count] = s;
  (*count)++;
}

static int parse_args(int argc, char **argv, opts_t *o) {
  memset(o, 0, sizeof(*o));
  o->interval_ms = 200;

  if (argc < 3) return 0;
  if (strcmp(argv[1], "follow") != 0) return 0;

  o->path = argv[2];

  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "--include") == 0 && i + 1 < argc) {
      push_str(&o->include, &o->include_count, argv[++i]);
    } else if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
      push_str(&o->exclude, &o->exclude_count, argv[++i]);
    } else if (strcmp(argv[i], "--highlight") == 0 && i + 1 < argc) {
      push_str(&o->highlight, &o->highlight_count, argv[++i]);
    } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
      o->interval_ms = atoi(argv[++i]);
      if (o->interval_ms < 10) o->interval_ms = 10;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      return 0;
    } else {
      fprintf(stderr, "Unknown arg: %s\n", argv[i]);
      return 0;
    }
  }

  return 1;
}

// -------------------------
// follow implementation
// -------------------------

static void sleep_ms(int ms) {
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

static int64_t file_size(FILE *fp) {
#ifdef _WIN32
  int fd = _fileno(fp);
  if (fd < 0) return -1;
  __int64 cur = _telli64(fd);
  __int64 end = _lseeki64(fd, 0, SEEK_END);
  _lseeki64(fd, cur, SEEK_SET);
  return (int64_t)end;
#else
  struct stat st;
  if (fstat(fileno(fp), &st) != 0) return -1;
  return (int64_t)st.st_size;
#endif
}

static bool should_print(const opts_t *o, const char *line) {
  // Trim trailing newline for matching.
  size_t n = strlen(line);
  char *tmp = (char*)malloc(n + 1);
  if (!tmp) return true;
  memcpy(tmp, line, n + 1);
  while (n > 0 && (tmp[n-1] == '\n' || tmp[n-1] == '\r')) tmp[--n] = '\0';

  // include: if any include exists, at least one must match
  if (o->include_count > 0) {
    bool ok = false;
    for (size_t i = 0; i < o->include_count; i++) {
      if (matchre(o->include[i], tmp)) { ok = true; break; }
    }
    if (!ok) { free(tmp); return false; }
  }

  // exclude: if any matches, drop
  for (size_t i = 0; i < o->exclude_count; i++) {
    if (matchre(o->exclude[i], tmp)) { free(tmp); return false; }
  }

  free(tmp);
  return true;
}

static int cmd_follow(const opts_t *o) {
  FILE *fp = fopen(o->path, "rb");
  if (!fp) {
    fprintf(stderr, "Failed to open %s: %s\n", o->path, strerror(errno));
    return 1;
  }

  // Start at end of file
  fseek(fp, 0, SEEK_END);
  int64_t last_size = file_size(fp);

  char buf[8192];

  for (;;) {
    int c = fgetc(fp);
    if (c == EOF) {
      clearerr(fp);

      // Handle truncation / rotation: if file shrank, seek to start.
      int64_t sz = file_size(fp);
      if (sz >= 0 && last_size >= 0 && sz < last_size) {
        fseek(fp, 0, SEEK_SET);
      }
      last_size = sz;

      sleep_ms(o->interval_ms);
      continue;
    }

    ungetc(c, fp);
    if (!fgets(buf, (int)sizeof(buf), fp)) {
      continue;
    }

    if (!should_print(o, buf)) continue;

    print_highlighted(buf, o->highlight, o->highlight_count);
    fflush(stdout);
  }

  // unreachable
  // fclose(fp);
  // return 0;
}

int main(int argc, char **argv) {
  enable_ansi_if_windows();

  opts_t o;
  if (!parse_args(argc, argv, &o)) {
    usage(stderr);
    return 2;
  }

  return cmd_follow(&o);
}
