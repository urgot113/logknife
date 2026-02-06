#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#endif

#if defined(LOGKNIFE_USE_PCRE2)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

// Windows doesn't have strcasecmp in MSVC by default.
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// -------------------------
// Built-in minimal regex (K&R / Pike style)
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

static int matchre_builtin(const char *re, const char *text) {
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

// -------------------------
// small utils
// -------------------------

static char *strdup_s(const char *s) {
  size_t n = strlen(s);
  char *p = (char *)malloc(n + 1);
  if (!p) return NULL;
  memcpy(p, s, n + 1);
  return p;
}

static void rstrip_newlines(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

// -------------------------
// highlight
// -------------------------

static void print_highlighted_plain(const char *line, const char **words, size_t word_count) {
  // Very simple highlighter: exact substring match.
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

static bool is_jsonish(const char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  return *s == '{' || *s == '[';
}

static void print_json_colorized(const char *line, const char **keys, size_t key_count) {
  // Not a full JSON parser. A lightweight lexer that:
  // - colors strings
  // - colors numbers / booleans / null
  // - if a string is a key (followed by ':'), uses key color

  const char *p = line;
  while (*p) {
    if (*p == '"') {
      // capture string
      const char *start = p;
      p++; // skip opening quote
      bool esc = false;
      while (*p) {
        if (esc) {
          esc = false;
          p++;
          continue;
        }
        if (*p == '\\') {
          esc = true;
          p++;
          continue;
        }
        if (*p == '"') {
          p++;
          break;
        }
        p++;
      }

      const char *end = p;

      // lookahead for ':'
      const char *q = end;
      while (*q && isspace((unsigned char)*q)) q++;
      bool is_key = (*q == ':');

      // extract inner text to compare keys
      const char *color = is_key ? "\x1b[35m" : "\x1b[32m"; // magenta for keys, green for strings

      if (is_key && key_count > 0) {
        size_t inner_len = (size_t)(end - start);
        char *tmp = (char *)malloc(inner_len + 1);
        if (tmp) {
          memcpy(tmp, start, inner_len);
          tmp[inner_len] = '\0';
          // tmp looks like "key" including quotes.
          // Compare without quotes.
          if (inner_len >= 2) {
            tmp[inner_len - 1] = '\0';
            const char *inner = tmp + 1;
            bool matched = false;
            for (size_t i = 0; i < key_count; i++) {
              if (strcmp(inner, keys[i]) == 0) { matched = true; break; }
            }
            if (matched) color = "\x1b[36m"; // cyan for requested keys
          }
          free(tmp);
        }
      }

      fputs(color, stdout);
      fwrite(start, 1, (size_t)(end - start), stdout);
      fputs("\x1b[0m", stdout);
      continue;
    }

    if (isdigit((unsigned char)*p) || (*p == '-' && isdigit((unsigned char)p[1]))) {
      const char *start = p;
      p++;
      while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
      fputs("\x1b[33m", stdout); // yellow
      fwrite(start, 1, (size_t)(p - start), stdout);
      fputs("\x1b[0m", stdout);
      continue;
    }

    if (strncmp(p, "true", 4) == 0 || strncmp(p, "false", 5) == 0 || strncmp(p, "null", 4) == 0) {
      const char *start = p;
      size_t len = 0;
      if (strncmp(p, "true", 4) == 0) len = 4;
      else if (strncmp(p, "false", 5) == 0) len = 5;
      else len = 4;

      fputs("\x1b[34m", stdout); // blue
      fwrite(start, 1, len, stdout);
      fputs("\x1b[0m", stdout);
      p += (int)len;
      continue;
    }

    fputc(*p, stdout);
    p++;
  }
}

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

  bool json_mode;
  const char **json_keys;
  size_t json_key_count;

  const char *path;
  int interval_ms;

  long tail_lines;      // if > 0, print last N lines before following
  long since_seconds;   // if > 0 and tail_lines==0, approximates tail_lines
  double since_rate_lps; // lines per second for since->tail conversion
} opts_t;

static void usage(FILE *out) {
  fprintf(out,
    "logknife (v0.1)\n"
    "\n"
    "Usage:\n"
    "  logknife follow <file> [options]\n"
    "\n"
    "Options:\n"
    "  --include <pattern>      filter (repeatable)\n"
    "  --exclude <pattern>      negative filter (repeatable)\n"
    "  --highlight <word>       highlight exact words (repeatable)\n"
    "  --json                   colorize JSON-ish lines\n"
    "  --json-key <key>         emphasize a JSON key (repeatable)\n"
    "  --tail <n>               print last n lines then follow\n"
    "  --since <dur>            approximate tail by duration (e.g., 10m, 2h). Uses --rate (default: 1 line/sec)\n"
    "  --rate <lines-per-sec>   used with --since (default: 1)\n"
    "  --interval <ms>          polling interval (default: 200)\n"
    "\n"
    "Regex:\n"
#if defined(LOGKNIFE_USE_PCRE2)
    "  PCRE2 enabled (full regex).\n"
#else
    "  Built-in regex subset: ^ $ . *\n"
#endif
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

static long parse_duration_seconds(const char *s) {
  // supports: 10s, 10m, 2h, 1d
  if (!s || !*s) return -1;
  char *end = NULL;
  long n = strtol(s, &end, 10);
  if (end == s) return -1;
  char unit = *end ? *end : 's';
  long mult = 1;
  if (unit == 's') mult = 1;
  else if (unit == 'm') mult = 60;
  else if (unit == 'h') mult = 3600;
  else if (unit == 'd') mult = 86400;
  else return -1;
  if (n < 0) return -1;
  return n * mult;
}

static int parse_args(int argc, char **argv, opts_t *o) {
  memset(o, 0, sizeof(*o));
  o->interval_ms = 200;
  o->since_rate_lps = 1.0;

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
    } else if (strcmp(argv[i], "--json") == 0) {
      o->json_mode = true;
    } else if (strcmp(argv[i], "--json-key") == 0 && i + 1 < argc) {
      push_str(&o->json_keys, &o->json_key_count, argv[++i]);
    } else if (strcmp(argv[i], "--tail") == 0 && i + 1 < argc) {
      o->tail_lines = strtol(argv[++i], NULL, 10);
      if (o->tail_lines < 0) o->tail_lines = 0;
    } else if (strcmp(argv[i], "--since") == 0 && i + 1 < argc) {
      o->since_seconds = parse_duration_seconds(argv[++i]);
      if (o->since_seconds < 0) {
        fprintf(stderr, "Invalid duration for --since (use 10s/10m/2h/1d)\n");
        return 0;
      }
    } else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
      o->since_rate_lps = atof(argv[++i]);
      if (o->since_rate_lps <= 0.0) o->since_rate_lps = 1.0;
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
// regex matching layer
// -------------------------

#if defined(LOGKNIFE_USE_PCRE2)

typedef struct {
  pcre2_code *code;
} re_t;

static void re_free(re_t *r) {
  if (!r) return;
  if (r->code) pcre2_code_free(r->code);
  r->code = NULL;
}

static bool re_compile(re_t *r, const char *pat) {
  int errorcode = 0;
  PCRE2_SIZE erroffset = 0;
  r->code = pcre2_compile((PCRE2_SPTR)pat, PCRE2_ZERO_TERMINATED, 0, &errorcode, &erroffset, NULL);
  return r->code != NULL;
}

static bool re_match(const re_t *r, const char *text) {
  if (!r || !r->code) return false;
  pcre2_match_data *md = pcre2_match_data_create_from_pattern(r->code, NULL);
  if (!md) return false;
  int rc = pcre2_match(r->code, (PCRE2_SPTR)text, strlen(text), 0, 0, md, NULL);
  pcre2_match_data_free(md);
  return rc >= 0;
}

#else

typedef struct {
  const char *pat;
} re_t;

static void re_free(re_t *r) {
  (void)r;
}

static bool re_compile(re_t *r, const char *pat) {
  r->pat = pat;
  return true;
}

static bool re_match(const re_t *r, const char *text) {
  return matchre_builtin(r->pat, text) != 0;
}

#endif

// -------------------------
// follow implementation
// -------------------------

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

static bool should_print(const opts_t *o, const re_t *includes, const re_t *excludes, const char *line) {
  char *tmp = strdup_s(line);
  if (!tmp) return true;
  rstrip_newlines(tmp);

  if (o->include_count > 0) {
    bool ok = false;
    for (size_t i = 0; i < o->include_count; i++) {
      if (re_match(&includes[i], tmp)) { ok = true; break; }
    }
    if (!ok) { free(tmp); return false; }
  }

  for (size_t i = 0; i < o->exclude_count; i++) {
    if (re_match(&excludes[i], tmp)) { free(tmp); return false; }
  }

  free(tmp);
  return true;
}

static int print_line(const opts_t *o, const char *line) {
  if (o->json_mode && is_jsonish(line)) {
    print_json_colorized(line, o->json_keys, o->json_key_count);
  } else {
    print_highlighted_plain(line, o->highlight, o->highlight_count);
  }
  return 0;
}

static int tail_last_lines(FILE *fp, long n, const opts_t *o, const re_t *includes, const re_t *excludes) {
  if (n <= 0) return 0;

  // Read backwards in blocks and count newlines.
  const size_t block = 4096;
  int64_t end = file_size(fp);
  if (end < 0) return 0;

  int64_t pos = end;
  long found = 0;
  char *buf = (char *)malloc(block);
  if (!buf) return 0;

  while (pos > 0 && found <= n) {
    size_t to_read = block;
    if (pos < (int64_t)block) to_read = (size_t)pos;
    pos -= (int64_t)to_read;
    fseek(fp, (long)pos, SEEK_SET);
    size_t got = fread(buf, 1, to_read, fp);
    for (size_t i = got; i > 0; i--) {
      if (buf[i - 1] == '\n') {
        found++;
        if (found > n) {
          pos += (int64_t)i;
          goto done;
        }
      }
    }
  }

  pos = 0;

 done:
  free(buf);
  fseek(fp, (long)pos, SEEK_SET);

  // Now print from pos to end.
  char line[8192];
  while (fgets(line, (int)sizeof(line), fp)) {
    if (!should_print(o, includes, excludes, line)) continue;
    print_line(o, line);
  }

  return 0;
}

static int cmd_follow(const opts_t *o) {
  FILE *fp = fopen(o->path, "rb");
  if (!fp) {
    fprintf(stderr, "Failed to open %s: %s\n", o->path, strerror(errno));
    return 1;
  }

  // compile patterns
  re_t *includes = NULL;
  re_t *excludes = NULL;

  if (o->include_count) {
    includes = (re_t *)calloc(o->include_count, sizeof(re_t));
    if (!includes) return 1;
    for (size_t i = 0; i < o->include_count; i++) {
      if (!re_compile(&includes[i], o->include[i])) {
        fprintf(stderr, "Failed to compile include pattern: %s\n", o->include[i]);
        return 1;
      }
    }
  }

  if (o->exclude_count) {
    excludes = (re_t *)calloc(o->exclude_count, sizeof(re_t));
    if (!excludes) return 1;
    for (size_t i = 0; i < o->exclude_count; i++) {
      if (!re_compile(&excludes[i], o->exclude[i])) {
        fprintf(stderr, "Failed to compile exclude pattern: %s\n", o->exclude[i]);
        return 1;
      }
    }
  }

  // determine tail behavior
  long tail = o->tail_lines;
  if (tail <= 0 && o->since_seconds > 0) {
    tail = (long)(o->since_seconds * o->since_rate_lps);
    if (tail < 1) tail = 1;
    if (tail > 100000) tail = 100000;
  }

  if (tail > 0) {
    tail_last_lines(fp, tail, o, includes ? includes : (re_t *)0, excludes ? excludes : (re_t *)0);
  }

  // Start following from end
  fseek(fp, 0, SEEK_END);
  int64_t last_size = file_size(fp);

  char buf[8192];

  for (;;) {
    int c = fgetc(fp);
    if (c == EOF) {
      clearerr(fp);

      // truncation
      int64_t sz = file_size(fp);
      if (sz >= 0 && last_size >= 0 && sz < last_size) {
        fseek(fp, 0, SEEK_SET);
      }
      last_size = sz;

      sleep_ms(o->interval_ms);
      continue;
    }

    ungetc(c, fp);
    if (!fgets(buf, (int)sizeof(buf), fp)) continue;

    if (!should_print(o, includes ? includes : (re_t *)0, excludes ? excludes : (re_t *)0, buf)) continue;

    print_line(o, buf);
    fflush(stdout);
  }

  // unreachable
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
