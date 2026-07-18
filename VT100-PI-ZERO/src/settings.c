// See settings.h. Small key=value parser; no external config library.
#include "settings.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

settings_t g_settings;

static char cfg_path[512];

#define STR_(x) #x
#define STR(x) STR_(x)   // stringify a numeric macro, e.g. STR(SERIAL_BAUD) -> "9600"

// The default file written on first run. Every option is present and commented
// so the file itself documents what can be changed. SERIAL_DEV is already a
// string literal (concatenates cleanly); baud is stringified from config.h.
static const char *DEFAULT_FILE =
    "# VT100-PI-ZERO settings. Edit, then restart the program\n"
    "# (sudo systemctl restart vt100-pi, or rerun the binary).\n"
    "# Syntax: key = value   (# starts a comment)\n"
    "\n"
    "# ---- Serial host link ----\n"
    "serial_dev = " SERIAL_DEV "\n"
    "baud       = " STR(SERIAL_BAUD) "   # 300 1200 2400 4800 9600 19200 38400 57600 115200\n"
    "\n"
    "# ---- Display ----\n"
    "theme      = amber      # color amber green white blue red yellow\n"
    "cursor     = block      # block | underline\n"
    "local_echo = off        # on | off  (a real terminal leaves this off; the host echoes)\n"
    "\n"
    "# ---- Font ----\n"
    "# empty = bundled DejaVu Sans Mono (or a system DejaVu); or an absolute .ttf path.\n"
    "font       =\n";

// ---- small helpers ---------------------------------------------------------
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) --e;
    *e = '\0';
    return s;
}

static int parse_bool(const char *v, int fallback) {
    if (!strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcmp(v, "1")) return 1;
    if (!strcasecmp(v, "off") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcmp(v, "0")) return 0;
    return fallback;
}

static int parse_theme(const char *v, int fallback) {
    static const struct { const char *name; int val; } t[] = {
        {"color", THEME_COLOR}, {"amber", THEME_AMBER}, {"green", THEME_GREEN},
        {"white", THEME_WHITE}, {"blue", THEME_BLUE}, {"red", THEME_RED},
        {"yellow", THEME_YELLOW},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; ++i)
        if (!strcasecmp(v, t[i].name)) return t[i].val;
    return fallback;
}

static int parse_cursor(const char *v, int fallback) {
    if (!strcasecmp(v, "block")) return 0;
    if (!strcasecmp(v, "underline") || !strcasecmp(v, "line")) return 1;
    return fallback;
}

// ---- defaults + file location ----------------------------------------------
static void set_defaults(void) {
    snprintf(g_settings.serial_dev, sizeof g_settings.serial_dev, "%s", SERIAL_DEV);
    g_settings.baud = SERIAL_BAUD;
    g_settings.theme = THEME_DEFAULT;
    g_settings.cursor_style = 0;
    g_settings.local_echo = LOCAL_ECHO;
    snprintf(g_settings.font_path, sizeof g_settings.font_path, "%s", FONT_TTF_PATH);
}

static void resolve_path(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(cfg_path, sizeof cfg_path, "%s/vt100-pi/vt100.conf", xdg);
    else if (home && home[0])
        snprintf(cfg_path, sizeof cfg_path, "%s/.config/vt100-pi/vt100.conf", home);
    else
        snprintf(cfg_path, sizeof cfg_path, "/etc/vt100-pi.conf");
}

// Create parent dirs of cfg_path (mkdir -p of the containing directory).
static void ensure_dir(void) {
    char dir[512];
    snprintf(dir, sizeof dir, "%s", cfg_path);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';
    for (char *p = dir + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
    }
    mkdir(dir, 0755);
}

static void write_default(void) {
    ensure_dir();
    FILE *f = fopen(cfg_path, "w");
    if (!f) { fprintf(stderr, "settings: cannot create %s: %s\n", cfg_path, strerror(errno)); return; }
    fputs(DEFAULT_FILE, f);
    fclose(f);
    fprintf(stderr, "settings: wrote default %s\n", cfg_path);
}

// ---- parse -----------------------------------------------------------------
static void apply(const char *key, const char *val) {
    if (!strcasecmp(key, "serial_dev"))       snprintf(g_settings.serial_dev, sizeof g_settings.serial_dev, "%s", val);
    else if (!strcasecmp(key, "baud"))        g_settings.baud = atoi(val);
    else if (!strcasecmp(key, "theme"))       g_settings.theme = parse_theme(val, g_settings.theme);
    else if (!strcasecmp(key, "cursor"))      g_settings.cursor_style = parse_cursor(val, g_settings.cursor_style);
    else if (!strcasecmp(key, "local_echo"))  g_settings.local_echo = parse_bool(val, g_settings.local_echo);
    else if (!strcasecmp(key, "font"))        snprintf(g_settings.font_path, sizeof g_settings.font_path, "%s", val);
    else fprintf(stderr, "settings: ignoring unknown key '%s'\n", key);
}

static void parse_file(FILE *f) {
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';                 // strip comment
        char *eq = strchr(line, '=');
        if (!eq) continue;                      // not a key=value line
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);
        if (key[0]) apply(key, val);
    }
}

void settings_load(void) {
    set_defaults();
    resolve_path();

    FILE *f = fopen(cfg_path, "r");
    if (!f) {
        write_default();                        // first run: materialize the template
        f = fopen(cfg_path, "r");
    }
    if (f) {
        parse_file(f);
        fclose(f);
        fprintf(stderr, "settings: loaded %s\n", cfg_path);
    } else {
        fprintf(stderr, "settings: using built-in defaults (no config file)\n");
    }
}

const char *settings_path(void) { return cfg_path; }

const char *settings_theme_name(int theme) {
    switch (theme) {
        case THEME_COLOR:  return "color";
        case THEME_GREEN:  return "green";
        case THEME_WHITE:  return "white";
        case THEME_BLUE:   return "blue";
        case THEME_RED:    return "red";
        case THEME_YELLOW: return "yellow";
        case THEME_AMBER:  default: return "amber";
    }
}

void settings_save(void) {
    ensure_dir();
    FILE *f = fopen(cfg_path, "w");
    if (!f) { fprintf(stderr, "settings: cannot write %s: %s\n", cfg_path, strerror(errno)); return; }
    fprintf(f,
        "# VT100-PI-ZERO settings. Edit, then restart the program\n"
        "# (sudo systemctl restart vt100-pi, or rerun the binary),\n"
        "# or use the on-screen Setup menu (Ctrl+F3).\n"
        "# Syntax: key = value   (# starts a comment)\n"
        "\n"
        "# ---- Serial host link ----\n"
        "serial_dev = %s\n"
        "baud       = %d   # 300 1200 2400 4800 9600 19200 38400 57600 115200\n"
        "\n"
        "# ---- Display ----\n"
        "theme      = %s      # color amber green white blue red yellow\n"
        "cursor     = %s      # block | underline\n"
        "local_echo = %s        # on | off\n"
        "\n"
        "# ---- Font ----\n"
        "# empty = bundled DejaVu Sans Mono (or a system DejaVu); or an absolute .ttf path.\n"
        "font       = %s\n",
        g_settings.serial_dev, g_settings.baud, settings_theme_name(g_settings.theme),
        g_settings.cursor_style ? "underline" : "block",
        g_settings.local_echo ? "on" : "off", g_settings.font_path);
    fclose(f);
    fprintf(stderr, "settings: saved %s\n", cfg_path);
}
