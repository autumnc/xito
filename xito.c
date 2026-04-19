/*
 * editor.c - A minimalist X11 text editor with XIM support
 *
 * Features:
 *   - dmenu-style borderless window (no title, no menu, no buttons)
 *   - XIM/XIC input method support (Chinese, CJK, etc.)
 *   - Pango + Cairo rendering with Xft2 font support
 *   - UTF-8 encoding, full Unicode support
 *   - Markdown syntax highlighting (-m flag)
 *   - Current line highlighting (-z flag)
 *   - Auto line wrapping (word-char)
 *   - Multiple built-in themes (tokyonight default), custom theme support
 *
 * Usage:
 *   editor [-w width] [-h height] [-x x] [-y y] [-bg color] [-fg color]
 *         [-fn font] [-z] [-m] [-t theme]
 *
 * Keys:
 *   Ctrl+S    Save text to stdout and exit
 *   Esc       Cancel and exit
 *   Ctrl+B    Bold **...** (markdown mode)
 *   Ctrl+I    Italic *...* (markdown mode)
 *   Ctrl+U    Underline __...__ (markdown mode)
 *   Ctrl+1~6  Headings # ~ ###### (markdown mode)
 *
 * Build:
 *   gcc -Wall -O2 -std=c11 $(pkg-config --cflags x11 xft pangocairo cairo-xlib) \
 *       editor.c -o editor $(pkg-config --libs x11 xft pangocairo cairo-xlib)
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <langinfo.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pango/pangoft2.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

/* ======================== Constants ======================== */
#define PAD           6
#define MAX_PREEDIT   1024
#define TAB_SIZE      4
#define INIT_CAP      4096
#define SCROLL_MARGIN 3

/* ======================== Data Types ======================== */

typedef struct {
    const char *name;
    const char *bg;
    const char *fg;
    const char *cursor_color;
    const char *dim;
    const char *selection_bg;
    const char *heading[6];
    const char *bold_color;
    const char *italic_color;
    const char *underline_color;
    const char *strikethrough_color;
    const char *highlight_bg;
    const char *code_bg;
    const char *code_fg;
    const char *link_color;
    const char *blockquote;
} Theme;

typedef struct {
    int  width, height;
    int  pos_x, pos_y;
    char *bg;
    char *fg;
    char *font;
    int  highlight_current;   /* -z */
    int  markdown_mode;       /* -m */
    char *theme_arg;
} Config;

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
    size_t  cursor;
    size_t  sel_start;
    size_t  sel_end;
    int     has_sel;
} TextBuf;

/* ======================== Built-in Themes ======================== */

static Theme theme_tokyonight = {
    .name = "tokyonight",
    .bg = "#1a1b26",
    .fg = "#c0caf5",
    .cursor_color = "#c0caf5",
    .dim = "#3b4261",
    .selection_bg = "#283457",
    .heading = { "#7aa2f7", "#7dcfff", "#9ece6a", "#e0af68", "#bb9af7", "#f7768e" },
    .bold_color = "#ff9e64",
    .italic_color = "#bb9af7",
    .underline_color = "#73daca",
    .strikethrough_color = "#565f89",
    .highlight_bg = "#292e42",
    .code_bg = "#292e42",
    .code_fg = "#e0af68",
    .link_color = "#7aa2f7",
    .blockquote = "#565f89"
};

static Theme theme_dracula = {
    .name = "dracula",
    .bg = "#282a36",
    .fg = "#f8f8f2",
    .cursor_color = "#f8f8f2",
    .dim = "#44475a",
    .selection_bg = "#44475a",
    .heading = { "#bd93f9", "#ff79c6", "#50fa7b", "#f1fa8c", "#8be9fd", "#ffb86c" },
    .bold_color = "#ffb86c",
    .italic_color = "#bd93f9",
    .underline_color = "#8be9fd",
    .strikethrough_color = "#6272a4",
    .highlight_bg = "#44475a",
    .code_bg = "#44475a",
    .code_fg = "#f1fa8c",
    .link_color = "#8be9fd",
    .blockquote = "#6272a4"
};

static Theme theme_nord = {
    .name = "nord",
    .bg = "#2e3440",
    .fg = "#d8dee9",
    .cursor_color = "#d8dee9",
    .dim = "#4c566a",
    .selection_bg = "#434c5e",
    .heading = { "#88c0d0", "#81a1c1", "#a3be8c", "#ebcb8b", "#b48ead", "#bf616a" },
    .bold_color = "#d08770",
    .italic_color = "#b48ead",
    .underline_color = "#88c0d0",
    .strikethrough_color = "#4c566a",
    .highlight_bg = "#3b4252",
    .code_bg = "#3b4252",
    .code_fg = "#ebcb8b",
    .link_color = "#88c0d0",
    .blockquote = "#4c566a"
};

static Theme theme_gruvbox = {
    .name = "gruvbox-dark",
    .bg = "#282828",
    .fg = "#ebdbb2",
    .cursor_color = "#ebdbb2",
    .dim = "#504945",
    .selection_bg = "#504945",
    .heading = { "#458588", "#689d6a", "#98971a", "#d79921", "#b16286", "#cc241d" },
    .bold_color = "#fe8019",
    .italic_color = "#b16286",
    .underline_color = "#689d6a",
    .strikethrough_color = "#504945",
    .highlight_bg = "#3c3836",
    .code_bg = "#3c3836",
    .code_fg = "#d79921",
    .link_color = "#458588",
    .blockquote = "#665c54"
};

/* ======================== Global State ======================== */

static Display       *dpy;
static Window         win;
static int            screen;
static Visual        *visual;
static Colormap       cmap;
static int            depth;
static XIM            xim = NULL;
static XIC            xic = NULL;
static XftFont       *xft_font = NULL;
static XftDraw       *xft_draw = NULL;

static cairo_surface_t *cairo_surface = NULL;
static cairo_t        *cr = NULL;
static PangoLayout    *pango_layout = NULL;
static PangoFontDescription *font_desc = NULL;
static Theme         *active_theme = &theme_tokyonight;

static TextBuf        editor;
static Config         config;
static int            scroll_y = 0;

/* Input method state */
static int            composing = 0;
static char           preedit_buf[MAX_PREEDIT] = "";
static int            preedit_caret = 0;

/* ======================== UTF-8 Utilities ======================== */

/*
 * Find the next UTF-8 codepoint boundary from `pos` in direction `inc` (+1 or -1).
 */
static size_t utf8_next_rune(const char *text, size_t len, size_t pos, int inc)
{
    ssize_t n = (ssize_t)pos;
    if (inc > 0) {
        if (n < (ssize_t)len) {
            n++;
            while (n < (ssize_t)len && (text[n] & 0xC0) == 0x80)
                n++;
        }
    } else {
        if (n > 0) {
            n--;
            while (n > 0 && (text[n] & 0xC0) == 0x80)
                n--;
        }
    }
    return (size_t)n;
}

/*
 * Return the number of UTF-8 characters in the first `nbytes` bytes.
 */
static size_t utf8_char_count(const char *text, size_t nbytes)
{
    size_t count = 0, i = 0;
    while (i < nbytes) {
        if ((text[i] & 0xC0) != 0x80)
            count++;
        i++;
    }
    return count;
}

/*
 * Return byte length of the first `nchars` UTF-8 characters.
 */
static size_t utf8_char_to_byte(const char *text, size_t nbytes, size_t nchars)
{
    size_t bytes = 0, chars = 0;
    while (bytes < nbytes && chars < nchars) {
        if ((text[bytes] & 0xC0) != 0x80)
            chars++;
        bytes++;
    }
    return bytes;
}

/* ======================== Color Utilities ======================== */

static void parse_color(const char *hex, double *r, double *g, double *b)
{
    if (!hex || hex[0] != '#') {
        *r = *g = *b = 1.0;
        return;
    }
    unsigned int val = 0;
    if (strlen(hex) == 7) {
        sscanf(hex + 1, "%06x", &val);
    } else if (strlen(hex) == 4) {
        sscanf(hex + 1, "%01x%01x%01x", &val, &val, &val);
        val = (val & 0xF) | ((val >> 4) & 0xF0) | ((val >> 8) & 0xF00);
    }
    *r = ((val >> 16) & 0xFF) / 255.0;
    *g = ((val >> 8)  & 0xFF) / 255.0;
    *b = (val & 0xFF) / 255.0;
}

static unsigned int color_to_xpixel(const char *hex)
{
    if (!hex || hex[0] != '#') return WhitePixel(dpy, screen);
    unsigned int r, g, b;
    if (strlen(hex) == 7)
        sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b);
    else if (strlen(hex) == 4)
        sscanf(hex + 1, "%1x%1x%1x", &r, &g, &b);
    else
        return WhitePixel(dpy, screen);
    return (r << 16) | (g << 8) | b;
}

/* ======================== Text Buffer Operations ======================== */

static void textbuf_init(TextBuf *tb, const char *initial)
{
    size_t ilen = initial ? strlen(initial) : 0;
    tb->cap = INIT_CAP > ilen ? INIT_CAP : ilen + 256;
    tb->data = malloc(tb->cap);
    if (!tb->data) {
        fprintf(stderr, "editor: out of memory\n");
        exit(1);
    }
    if (ilen > 0) {
        memcpy(tb->data, initial, ilen);
    }
    tb->data[ilen] = '\0';
    tb->len = ilen;
    tb->cursor = ilen;
    tb->sel_start = 0;
    tb->sel_end = 0;
    tb->has_sel = 0;
}

static void textbuf_ensure_cap(TextBuf *tb, size_t needed)
{
    if (tb->cap >= needed) return;
    while (tb->cap < needed)
        tb->cap *= 2;
    tb->data = realloc(tb->data, tb->cap);
    if (!tb->data) {
        fprintf(stderr, "editor: out of memory\n");
        exit(1);
    }
}

static void textbuf_insert(TextBuf *tb, const char *str, size_t slen)
{
    textbuf_ensure_cap(tb, tb->len + slen + 1);

    /* If there's a selection, delete it first */
    if (tb->has_sel) {
        size_t ds = tb->sel_start < tb->sel_end ? tb->sel_start : tb->sel_end;
        size_t de = tb->sel_start < tb->sel_end ? tb->sel_end : tb->sel_start;
        memmove(tb->data + ds + slen, tb->data + de, tb->len - de);
        memcpy(tb->data + ds, str, slen);
        tb->len = tb->len - (de - ds) + slen;
        tb->data[tb->len] = '\0';
        tb->cursor = ds + slen;
        tb->has_sel = 0;
    } else {
        memmove(tb->data + tb->cursor + slen, tb->data + tb->cursor, tb->len - tb->cursor);
        memcpy(tb->data + tb->cursor, str, slen);
        tb->len += slen;
        tb->data[tb->len] = '\0';
        tb->cursor += slen;
    }
}

static void textbuf_delete_char(TextBuf *tb, int dir)
{
    /* dir: -1 = backspace (delete before cursor), +1 = delete (delete after cursor) */
    if (tb->has_sel) {
        size_t ds = tb->sel_start < tb->sel_end ? tb->sel_start : tb->sel_end;
        size_t de = tb->sel_start < tb->sel_end ? tb->sel_end : tb->sel_start;
        memmove(tb->data + ds, tb->data + de, tb->len - de);
        tb->len -= (de - ds);
        tb->data[tb->len] = '\0';
        tb->cursor = ds;
        tb->has_sel = 0;
        return;
    }

    if (dir < 0) {
        if (tb->cursor == 0) return;
        size_t prev = utf8_next_rune(tb->data, tb->len, tb->cursor, -1);
        size_t del_len = tb->cursor - prev;
        memmove(tb->data + prev, tb->data + tb->cursor, tb->len - tb->cursor);
        tb->len -= del_len;
        tb->data[tb->len] = '\0';
        tb->cursor = prev;
    } else {
        if (tb->cursor >= tb->len) return;
        size_t next = utf8_next_rune(tb->data, tb->len, tb->cursor, +1);
        size_t del_len = next - tb->cursor;
        memmove(tb->data + tb->cursor, tb->data + next, tb->len - next);
        tb->len -= del_len;
        tb->data[tb->len] = '\0';
    }
}

static void textbuf_delete_range(TextBuf *tb, size_t start, size_t end)
{
    if (start >= end || end > tb->len) return;
    memmove(tb->data + start, tb->data + end, tb->len - end);
    tb->len -= (end - start);
    tb->data[tb->len] = '\0';
    if (tb->cursor > tb->len) tb->cursor = tb->len;
    tb->has_sel = 0;
}

/* ======================== Cursor / Line Navigation ======================== */

static size_t line_start(TextBuf *tb, size_t pos)
{
    if (pos == 0) return 0;
    size_t p = pos;
    /* If pos is on a newline, back up one */
    if (p > 0 && tb->data[p - 1] == '\n') p--;
    while (p > 0 && tb->data[p - 1] != '\n') p--;
    return p;
}

static size_t line_end(TextBuf *tb, size_t pos)
{
    size_t p = pos;
    while (p < tb->len && tb->data[p] != '\n') p++;
    return p;
}

static void move_cursor_left(TextBuf *tb)
{
    if (tb->has_sel) {
        tb->cursor = tb->sel_start < tb->sel_end ? tb->sel_start : tb->sel_end;
        tb->has_sel = 0;
        return;
    }
    if (tb->cursor > 0)
        tb->cursor = utf8_next_rune(tb->data, tb->len, tb->cursor, -1);
}

static void move_cursor_right(TextBuf *tb)
{
    if (tb->has_sel) {
        tb->cursor = tb->sel_start < tb->sel_end ? tb->sel_end : tb->sel_start;
        tb->has_sel = 0;
        return;
    }
    if (tb->cursor < tb->len)
        tb->cursor = utf8_next_rune(tb->data, tb->len, tb->cursor, +1);
}

static void move_cursor_home(TextBuf *tb)
{
    tb->has_sel = 0;
    tb->cursor = line_start(tb, tb->cursor);
}

static void move_cursor_end(TextBuf *tb)
{
    tb->has_sel = 0;
    tb->cursor = line_end(tb, tb->cursor);
}

static void move_cursor_up(TextBuf *tb)
{
    tb->has_sel = 0;
    size_t ls = line_start(tb, tb->cursor);
    if (ls == 0 && tb->cursor == 0) return;

    /* Find current column (in bytes from line start) */
    size_t col = tb->cursor - ls;

    /* Find previous line start */
    if (ls == 0) return;
    size_t prev_end = ls - 1;  /* the \n character */
    size_t prev_ls = line_start(tb, prev_end);

    /* Clamp column to previous line length */
    size_t prev_line_len = prev_end - prev_ls;
    if (col > prev_line_len) col = prev_line_len;

    tb->cursor = prev_ls + col;
}

static void move_cursor_down(TextBuf *tb)
{
    tb->has_sel = 0;
    size_t le = line_end(tb, tb->cursor);
    if (le >= tb->len) return;

    size_t ls = line_start(tb, tb->cursor);
    size_t col = tb->cursor - ls;

    size_t next_ls = le + 1;
    if (next_ls >= tb->len) return;

    size_t next_end = line_end(tb, next_ls);
    size_t next_line_len = next_end - next_ls;
    if (col > next_line_len) col = next_line_len;

    tb->cursor = next_ls + col;
}

static void move_cursor_to_start(TextBuf *tb)
{
    tb->has_sel = 0;
    tb->cursor = 0;
}

static void move_cursor_to_end(TextBuf *tb)
{
    tb->has_sel = 0;
    tb->cursor = tb->len;
}

/* Word movement */
static size_t word_edge(TextBuf *tb, size_t pos, int dir)
{
    /* Skip non-alphanumeric in current direction */
    if (dir > 0) {
        while (pos < tb->len && !isalnum((unsigned char)tb->data[pos])
               && tb->data[pos] != '\n')
            pos++;
        while (pos < tb->len && isalnum((unsigned char)tb->data[pos]))
            pos++;
    } else {
        if (pos > 0) pos = utf8_next_rune(tb->data, tb->len, pos, -1);
        while (pos > 0 && !isalnum((unsigned char)tb->data[pos])
               && tb->data[pos] != '\n')
            pos = utf8_next_rune(tb->data, tb->len, pos, -1);
        while (pos > 0 && isalnum((unsigned char)tb->data[pos]))
            pos = utf8_next_rune(tb->data, tb->len, pos, -1);
        /* Move to the start of the word */
        if (pos > 0 && isalnum((unsigned char)tb->data[pos]))
            pos = utf8_next_rune(tb->data, tb->len, pos, +1);
    }
    return pos;
}

/* ======================== Selection Helpers ======================== */

static void sel_update_shift(TextBuf *tb, size_t new_pos)
{
    if (!tb->has_sel) {
        tb->sel_start = tb->cursor;
        tb->sel_end = tb->cursor;
        tb->has_sel = 1;
    }
    tb->cursor = new_pos;
    tb->sel_end = new_pos;
    /* Normalize so sel_start <= sel_end */
    if (tb->sel_start > tb->sel_end) {
        size_t tmp = tb->sel_start;
        tb->sel_start = tb->sel_end;
        tb->sel_end = tmp;
    }
    if (tb->sel_start == tb->sel_end)
        tb->has_sel = 0;
}

/* ======================== Markdown Parser ======================== */

/*
 * Parse markdown in the text and apply Pango attributes for syntax highlighting.
 * Builds a PangoAttrList that colors headings, bold, italic, underline, code, and links.
 */
static PangoAttrList *build_markdown_attrs(const char *text, Theme *theme)
{
    PangoAttrList *attrs = pango_attr_list_new();
    if (!text || !text[0]) return attrs;

    size_t text_len = strlen(text);

    /* Helper: add foreground color for a range */
    #define ADD_FG(start, end, color) do { \
        PangoColor _c; \
        if (pango_color_parse(&_c, color)) { \
            PangoAttribute *_a = pango_attr_foreground_new(_c.red, _c.green, _c.blue); \
            _a->start_index = (start); _a->end_index = (end); \
            pango_attr_list_insert(attrs, _a); \
        } \
    } while(0)

    #define ADD_BG(start, end, color) do { \
        PangoColor _c; \
        if (pango_color_parse(&_c, color)) { \
            PangoAttribute *_a = pango_attr_background_new(_c.red, _c.green, _c.blue); \
            _a->start_index = (start); _a->end_index = (end); \
            pango_attr_list_insert(attrs, _a); \
        } \
    } while(0)

    #define ADD_WEIGHT(start, end, w) do { \
        PangoAttribute *_a = pango_attr_weight_new(w); \
        _a->start_index = (start); _a->end_index = (end); \
        pango_attr_list_insert(attrs, _a); \
    } while(0)

    #define ADD_STYLE(start, end, s) do { \
        PangoAttribute *_a = pango_attr_style_new(s); \
        _a->start_index = (start); _a->end_index = (end); \
        pango_attr_list_insert(attrs, _a); \
    } while(0)

    #define ADD_ULINE(start, end, u) do { \
        PangoAttribute *_a = pango_attr_underline_new(u); \
        _a->start_index = (start); _a->end_index = (end); \
        pango_attr_list_insert(attrs, _a); \
    } while(0)

    /* Parse line by line */
    size_t pos = 0;
    while (pos < text_len) {
        size_t line_start_idx = pos;
        size_t line_end_idx = pos;
        while (line_end_idx < text_len && text[line_end_idx] != '\n')
            line_end_idx++;
        size_t line_len = line_end_idx - line_start_idx;

        /* Headings: # to ###### at line start */
        if (line_len > 0 && text[line_start_idx] == '#') {
            int level = 0;
            while (level < 6 && line_start_idx + level < text_len
                   && text[line_start_idx + level] == '#')
                level++;
            if (level > 0 && level <= 6
                && (line_start_idx + level >= text_len
                    || text[line_start_idx + level] == ' '
                    || text[line_start_idx + level] == '\t')) {
                /* Skip leading # and space */
                size_t content_start = line_start_idx + level;
                while (content_start < line_end_idx
                       && (text[content_start] == ' ' || text[content_start] == '\t'))
                    content_start++;
                if (content_start < line_end_idx) {
                    ADD_FG(content_start, line_end_idx, theme->heading[level - 1]);
                    ADD_WEIGHT(content_start, line_end_idx, PANGO_WEIGHT_BOLD);
                }
            }
        }

        /* Blockquote: > at line start */
        if (line_len > 0 && text[line_start_idx] == '>') {
            size_t bq_end = line_start_idx + 1;
            if (bq_end < line_end_idx && text[bq_end] == ' ')
                bq_end++;
            ADD_FG(line_start_idx, line_end_idx, theme->blockquote);
            ADD_STYLE(line_start_idx, line_end_idx, PANGO_STYLE_ITALIC);
        }

        /* Inline patterns within the line */
        /* We parse for **bold**, *italic*, __underline__, `code`, [link](url) */
        size_t i = line_start_idx;
        while (i < line_end_idx) {
            /* `code` - backtick inline code */
            if (text[i] == '`' && i + 1 < line_end_idx) {
                size_t close = i + 1;
                while (close < line_end_idx && text[close] != '`') close++;
                if (close < line_end_idx && close > i + 1) {
                    ADD_BG(i, close + 1, theme->code_bg);
                    ADD_FG(i, close + 1, theme->code_fg);
                    i = close + 1;
                    continue;
                }
            }

            /* **bold** */
            if (i + 1 < line_end_idx && text[i] == '*' && text[i + 1] == '*') {
                size_t close = i + 2;
                while (close + 1 < line_end_idx && !(text[close] == '*' && text[close + 1] == '*'))
                    close++;
                if (close + 1 < line_end_idx && close > i + 1) {
                    ADD_FG(i, close + 2, theme->bold_color);
                    ADD_WEIGHT(i, close + 2, PANGO_WEIGHT_BOLD);
                    i = close + 2;
                    continue;
                }
            }

            /* *italic* - but not ** */
            if (text[i] == '*' && (i + 1 >= line_end_idx || text[i + 1] != '*')
                && (i == line_start_idx || text[i - 1] != '*')) {
                size_t close = i + 1;
                while (close < line_end_idx && text[close] != '*') close++;
                if (close < line_end_idx && close > i + 1
                    && (close + 1 >= line_end_idx || text[close + 1] != '*')) {
                    ADD_FG(i, close + 1, theme->italic_color);
                    ADD_STYLE(i, close + 1, PANGO_STYLE_ITALIC);
                    i = close + 1;
                    continue;
                }
            }

            /* __underline__ */
            if (i + 1 < line_end_idx && text[i] == '_' && text[i + 1] == '_') {
                size_t close = i + 2;
                while (close + 1 < line_end_idx && !(text[close] == '_' && text[close + 1] == '_'))
                    close++;
                if (close + 1 < line_end_idx && close > i + 1) {
                    ADD_FG(i, close + 2, theme->underline_color);
                    ADD_ULINE(i, close + 2, PANGO_UNDERLINE_SINGLE);
                    i = close + 2;
                    continue;
                }
            }

            /* ~~strikethrough~~ */
            if (i + 1 < line_end_idx && text[i] == '~' && text[i + 1] == '~') {
                size_t close = i + 2;
                while (close + 1 < line_end_idx && !(text[close] == '~' && text[close + 1] == '~'))
                    close++;
                if (close + 1 < line_end_idx && close > i + 1) {
                    ADD_FG(i, close + 2, theme->strikethrough_color);
                    /* Pango strikethrough via underline attribute */
                    PangoAttribute *sa = pango_attr_strikethrough_new(TRUE);
                    sa->start_index = (guint)i;
                    sa->end_index = (guint)(close + 2);
                    pango_attr_list_insert(attrs, sa);
                    i = close + 2;
                    continue;
                }
            }

            /* ==highlight== (mark syntax, common in Obsidian/VS Code) */
            if (i + 1 < line_end_idx && text[i] == '=' && text[i + 1] == '=') {
                size_t close = i + 2;
                while (close + 1 < line_end_idx && !(text[close] == '=' && text[close + 1] == '='))
                    close++;
                if (close + 1 < line_end_idx && close > i + 1) {
                    ADD_BG(i, close + 2, theme->highlight_bg);
                    i = close + 2;
                    continue;
                }
            }

            /* [link](url) */
            if (text[i] == '[' && i + 1 < line_end_idx) {
                size_t bracket_end = i + 1;
                while (bracket_end < line_end_idx && text[bracket_end] != ']') bracket_end++;
                if (bracket_end < line_end_idx && bracket_end + 2 < line_end_idx
                    && text[bracket_end + 1] == '(') {
                    size_t paren_end = bracket_end + 2;
                    while (paren_end < line_end_idx && text[paren_end] != ')') paren_end++;
                    if (paren_end < line_end_idx) {
                        ADD_FG(i, paren_end + 1, theme->link_color);
                        ADD_ULINE(i, bracket_end + 1, PANGO_UNDERLINE_SINGLE);
                        i = paren_end + 1;
                        continue;
                    }
                }
            }

            i++;
        }

        pos = line_end_idx + 1; /* skip the \n */
    }

    #undef ADD_FG
    #undef ADD_BG
    #undef ADD_WEIGHT
    #undef ADD_STYLE
    #undef ADD_ULINE

    return attrs;
}

/* ======================== X11 Initialization ======================== */

static void x11_init(void)
{
    /* Connect to X display */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "editor: cannot open display\n");
        exit(1);
    }
    screen = DefaultScreen(dpy);
    visual = DefaultVisual(dpy, screen);
    cmap = DefaultColormap(dpy, screen);
    depth = DefaultDepth(dpy, screen);

    /* Calculate position */
    int x = config.pos_x;
    int y = config.pos_y;
    int w = config.width;
    int h = config.height;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    /* Create borderless window (dmenu-style override_redirect) */
    XSetWindowAttributes wa;
    memset(&wa, 0, sizeof(wa));
    wa.override_redirect = True;
    wa.background_pixel = color_to_xpixel(config.bg);
    wa.border_pixel = color_to_xpixel(config.bg);
    wa.colormap = cmap;
    wa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                    StructureNotifyMask | VisibilityChangeMask | FocusChangeMask;

    win = XCreateWindow(dpy, RootWindow(dpy, screen),
                        x, y, w, h, 0, depth, InputOutput, visual,
                        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
                        &wa);

    /* Set window title (even though it's hidden, useful for WM) */
    XStoreName(dpy, win, "editor");

    /* Set WM_CLASS */
    XClassHint ch = { "editor", "Editor" };
    XSetClassHint(dpy, win, &ch);

    /* Map and raise */
    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);

    /* Grab keyboard focus */
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

    XFlush(dpy);
}

/* ======================== Input Method Setup ======================== */

/*
 * Adapted from the reference inputmethod.c implementation.
 * Sets up XIM/XIC with PreeditCallbacks for CJK input support.
 * XMODIFIERS environment variable is respected by XOpenIM.
 */

static int preedit_start_cb(XIC ic, XPointer client_data, XPointer call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    composing = 1;
    preedit_buf[0] = '\0';
    preedit_caret = 0;
    return strlen(preedit_buf);  /* return max preedit length */
}

static int preedit_done_cb(XIC ic, XPointer client_data, XPointer call_data)
{
    (void)ic; (void)client_data; (void)call_data;
    composing = 0;
    preedit_buf[0] = '\0';
    preedit_caret = 0;
    return 0;
}

static int preedit_draw_cb(XIC ic, XPointer client_data, XPointer call_data)
{
    XIMPreeditDrawCallbackStruct *pdraw = (XIMPreeditDrawCallbackStruct *)call_data;
    (void)ic; (void)client_data;

    if (!pdraw) return 0;

    /* We only support multi-byte (UTF-8) encoding */
    if (pdraw->text && pdraw->text->encoding_is_wchar) {
        fprintf(stderr, "editor: warning: wchar encoding not supported, use UTF-8\n");
        return 0;
    }

    size_t beg = utf8_char_to_byte(editor.data, editor.len, pdraw->chg_first);
    size_t dellen = utf8_char_to_byte(preedit_buf + beg,
                                      MAX_PREEDIT - beg, pdraw->chg_length);
    size_t inslen = 0;
    if (pdraw->text && pdraw->text->length > 0) {
        inslen = strlen(pdraw->text->string.multi_byte);
    }
    size_t preedit_len = strlen(preedit_buf);
    size_t endlen = 0;
    if (beg + dellen < preedit_len)
        endlen = preedit_len - beg - dellen;

    if (beg + inslen >= MAX_PREEDIT)
        return 0;

    memmove(preedit_buf + beg + inslen, preedit_buf + beg + dellen, endlen + 1);
    if (pdraw->text && pdraw->text->length > 0)
        memcpy(preedit_buf + beg, pdraw->text->string.multi_byte, inslen);
    preedit_buf[beg + inslen + endlen] = '\0';

    /* Caret position: convert from char index to byte offset */
    if (pdraw->caret >= 0)
        preedit_caret = (int)utf8_char_to_byte(preedit_buf,
                                                 strlen(preedit_buf),
                                                 (size_t)pdraw->caret);
    return 0;
}

static int preedit_caret_cb(XIC ic, XPointer client_data, XPointer call_data)
{
    XIMPreeditCaretCallbackStruct *pcaret = (XIMPreeditCaretCallbackStruct *)call_data;
    (void)ic; (void)client_data;

    if (!pcaret) return 0;

    switch (pcaret->direction) {
    case XIMForwardChar:
        if (preedit_caret < (int)strlen(preedit_buf))
            preedit_caret = (int)utf8_next_rune(preedit_buf, strlen(preedit_buf),
                                                  (size_t)preedit_caret, +1);
        break;
    case XIMBackwardChar:
        if (preedit_caret > 0)
            preedit_caret = (int)utf8_next_rune(preedit_buf, strlen(preedit_buf),
                                                  (size_t)preedit_caret, -1);
        break;
    case XIMAbsolutePosition:
        preedit_caret = (int)utf8_char_to_byte(preedit_buf,
                                                 strlen(preedit_buf),
                                                 (size_t)pcaret->position);
        break;
    case XIMDontChange:
    case XIMLineStart:
        preedit_caret = 0;
        break;
    case XIMLineEnd:
        preedit_caret = (int)strlen(preedit_buf);
        break;
    default:
        break;
    }

    pcaret->position = (int)utf8_char_count(preedit_buf, (size_t)preedit_caret);
    return 0;
}

static void init_input_method(void)
{
    /*
     * XOpenIM uses the locale modifiers set by XSetLocaleModifiers("")
     * (called in main) to find the correct IM server (ibus, fcitx, etc.).
     * XMODIFIERS env var is automatically respected.
     */
    xim = XOpenIM(dpy, NULL, NULL, NULL);

    if (!xim) {
        fprintf(stderr, "editor: warning: could not open input method "
                "(XMODIFIERS=%s)\n", getenv("XMODIFIERS") ? getenv("XMODIFIERS") : "(unset)");
        return;
    }

    /* Query supported styles */
    XIMStyles *imstyles = NULL;
    if (XGetIMValues(xim, XNQueryInputStyle, &imstyles, NULL) != Success || !imstyles) {
        fprintf(stderr, "editor: warning: could not query input method styles\n");
        XCloseIM(xim);
        xim = NULL;
        return;
    }

    /*
     * Select the best XIM style.
     *
     * Modern IM frameworks (ibus, fcitx via XIM bridge) often ONLY support
     * XIMPreeditNothing — meaning the IM server manages its own preedit UI
     * (floating candidate window, composition bar, etc.) and the application
     * only receives the final committed text via Xutf8LookupString.
     *
     * Priority order:
     *   1. PreeditCallbacks + StatusNothing  (ideal: app draws preedit inline)
     *   2. PreeditCallbacks + anything else  (app draws preedit inline)
     *   3. PreeditNothing + StatusNothing    (IM manages its own preedit UI)
     *   4. PreeditNothing + anything else    (IM manages its own preedit UI)
     *   5. Any other style with preedit support
     *
     * We skip PreeditPosition/Area because we can't handle geometry callbacks.
     */
    XIMStyle best_callbacks_style = 0;   /* best style WITH PreeditCallbacks */
    XIMStyle fallback_callbacks_style = 0;
    XIMStyle best_nothing_style = 0;     /* best style WITHOUT PreeditCallbacks */
    XIMStyle fallback_nothing_style = 0;

    fprintf(stderr, "editor: IM supports %d styles:\n", imstyles->count_styles);

    for (int i = 0; i < imstyles->count_styles; i++) {
        XIMStyle s = imstyles->supported_styles[i];
        int preedit = s & 0x00FF;  /* extract preedit part (low byte) */

        fprintf(stderr, "  style[%d] = 0x%lx (preedit=%s, status=%s)\n",
                i, (unsigned long)s,
                preedit == XIMPreeditCallbacks ? "Callbacks" :
                preedit == XIMPreeditNothing   ? "Nothing"   :
                preedit == XIMPreeditPosition  ? "Position"  :
                preedit == XIMPreeditArea      ? "Area"      :
                "Unknown",
                (s & XIMStatusNothing) ? "Nothing" :
                (s & XIMStatusCallbacks) ? "Callbacks" :
                (s & XIMStatusArea) ? "Area" :
                (s & XIMStatusNone) ? "None" : "Unknown");

        /* Skip styles with PreeditPosition/Area (we can't handle these) */
        if (preedit == XIMPreeditPosition || preedit == XIMPreeditArea)
            continue;

        if (preedit == XIMPreeditCallbacks) {
            if (fallback_callbacks_style == 0)
                fallback_callbacks_style = s;
            /* Prefer StatusNothing */
            if (s & XIMStatusNothing) {
                best_callbacks_style = s;
            }
        } else if (preedit == XIMPreeditNothing) {
            if (fallback_nothing_style == 0)
                fallback_nothing_style = s;
            /* Prefer StatusNothing */
            if (s & XIMStatusNothing) {
                best_nothing_style = s;
            }
        } else {
            /* Unknown preedit type but not Position/Area — try as fallback */
            if (fallback_nothing_style == 0)
                fallback_nothing_style = s;
        }
    }

    XFree(imstyles);
    imstyles = NULL;

    /* Choose: prefer Callbacks (inline preedit) over Nothing (IM-managed preedit) */
    XIMStyle chosen_style = 0;
    int use_preedit_callbacks = 0;

    if (best_callbacks_style) {
        chosen_style = best_callbacks_style;
        use_preedit_callbacks = 1;
    } else if (fallback_callbacks_style) {
        chosen_style = fallback_callbacks_style;
        use_preedit_callbacks = 1;
    } else if (best_nothing_style) {
        chosen_style = best_nothing_style;
        use_preedit_callbacks = 0;
    } else if (fallback_nothing_style) {
        chosen_style = fallback_nothing_style;
        use_preedit_callbacks = 0;
    }

    if (chosen_style == 0) {
        fprintf(stderr, "editor: warning: no compatible XIM style found\n");
        XCloseIM(xim);
        xim = NULL;
        return;
    }

    fprintf(stderr, "editor: input method opened, style=0x%lx, preedit=%s\n",
            (unsigned long)chosen_style,
            use_preedit_callbacks ? "Callbacks (inline)" : "Nothing (IM-managed)");

    /*
     * Create the input context.
     *
     * - With PreeditCallbacks: register our preedit callback functions so the app
     *   can draw the composing text inline (the reference inputmethod.c style).
     *
     * - With PreeditNothing: do NOT pass PreeditAttributes. The IM server will
     *   manage its own candidate/composition window. Committed text arrives via
     *   Xutf8LookupString returning XBufferOverflow or XLookupChars.
     */
    if (use_preedit_callbacks) {
        XICCallback start_cb = { NULL, (XICProc)preedit_start_cb };
        XICCallback done_cb  = { NULL, (XICProc)preedit_done_cb };
        XICCallback draw_cb  = { NULL, (XICProc)preedit_draw_cb };
        XICCallback caret_cb = { NULL, (XICProc)preedit_caret_cb };

        XVaNestedList preedit_attr = XVaCreateNestedList(0,
            XNPreeditStartCallback, &start_cb,
            XNPreeditDoneCallback,  &done_cb,
            XNPreeditDrawCallback,  &draw_cb,
            XNPreeditCaretCallback, &caret_cb,
            NULL);

        xic = XCreateIC(xim,
            XNInputStyle,       chosen_style,
            XNPreeditAttributes, preedit_attr,
            XNClientWindow,     win,
            XNFocusWindow,      win,
            NULL);

        XFree(preedit_attr);
    } else {
        /* PreeditNothing: no preedit callbacks, IM manages its own UI */
        xic = XCreateIC(xim,
            XNInputStyle,   chosen_style,
            XNClientWindow, win,
            XNFocusWindow,  win,
            NULL);
    }

    if (!xic) {
        fprintf(stderr, "editor: warning: could not create input context\n");
        XCloseIM(xim);
        xim = NULL;
        return;
    }

    /* Merge IM event mask with our event mask */
    long im_event_mask;
    if (XGetICValues(xic, XNFilterEvents, &im_event_mask, NULL) == Success) {
        XSelectInput(dpy, win,
            ExposureMask | KeyPressMask | KeyReleaseMask |
            StructureNotifyMask | VisibilityChangeMask |
            FocusChangeMask | im_event_mask);
    }

    XSetICFocus(xic);
}

/* ======================== Cairo / Pango Rendering ======================== */

static void rendering_init(void)
{
    /* Create Cairo Xlib surface */
    cairo_surface = cairo_xlib_surface_create(dpy, win, visual,
                                               config.width, config.height);
    cr = cairo_create(cairo_surface);

    /* Create Pango layout */
    pango_layout = pango_cairo_create_layout(cr);

    /* Parse font description */
    font_desc = pango_font_description_from_string(config.font);
    if (!font_desc || pango_font_description_get_family(font_desc) == NULL) {
        if (font_desc) pango_font_description_free(font_desc);
        font_desc = pango_font_description_from_string("Monospace 13");
    }
    pango_layout_set_font_description(pango_layout, font_desc);

    /* Set wrap mode */
    pango_layout_set_wrap(pango_layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_spacing(pango_layout, 2 * PANGO_SCALE);
}

static void rendering_resize(int w, int h)
{
    if (cairo_surface) {
        cairo_xlib_surface_set_size(cairo_surface, w, h);
    }
}

/*
 * Get the pixel height of a single line from the Pango layout.
 */
static int get_line_height(void)
{
    PangoContext *ctx = pango_layout_get_context(pango_layout);
    PangoFontMetrics *metrics = pango_context_get_metrics(ctx, font_desc, NULL);
    if (!metrics) return 18;
    int height = pango_font_metrics_get_ascent(metrics) + pango_font_metrics_get_descent(metrics);
    pango_font_metrics_unref(metrics);
    return PANGO_PIXELS(height) + 2;
}

/*
 * Ensure the cursor is visible by adjusting scroll_y.
 */
static void ensure_cursor_visible(void)
{
    if (!pango_layout || (editor.len == 0 && editor.cursor == 0)) {
        scroll_y = 0;
        return;
    }

    /* Set text to get accurate layout info */
    pango_layout_set_text(pango_layout, editor.data, (int)editor.len);
    pango_layout_set_width(pango_layout, (config.width - 2 * PAD) * PANGO_SCALE);

    PangoRectangle strong, weak;
    pango_layout_get_cursor_pos(pango_layout, (int)editor.cursor, &strong, &weak);

    int cursor_y = strong.y / PANGO_SCALE;
    int lh = get_line_height();
    if (lh < 10) lh = 18;
    int vis_h = config.height - 2 * PAD;

    if (cursor_y < scroll_y) {
        scroll_y = cursor_y;
    } else if (cursor_y + lh > scroll_y + vis_h) {
        scroll_y = cursor_y + lh - vis_h;
    }

    /* Clamp scroll_y to non-negative */
    if (scroll_y < 0) scroll_y = 0;
}

/* ======================== Draw Function ======================== */

static void draw(void)
{
    if (!cr || !pango_layout) return;

    int w = config.width;
    int h = config.height;
    int vis_w = w - 2 * PAD;
    int vis_h = h - 2 * PAD;

    /* Clear background */
    double bg_r, bg_g, bg_b;
    parse_color(config.bg, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* Draw subtle border (1px) */
    double br, bg_c, bb;
    parse_color(active_theme->dim, &br, &bg_c, &bb);
    cairo_set_source_rgba(cr, br, bg_c, bb, 0.3);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_stroke(cr);

    /* Prepare layout */
    pango_layout_set_text(pango_layout, editor.data, (int)editor.len);
    pango_layout_set_width(pango_layout, vis_w * PANGO_SCALE);
    pango_layout_set_indent(pango_layout, 0);

    /* Build attribute list */
    PangoAttrList *attrs = pango_attr_list_new();

    if (config.markdown_mode) {
        PangoAttrList *md_attrs = build_markdown_attrs(editor.data, active_theme);
        /* Merge markdown attrs */
        PangoAttrIterator *iter = pango_attr_list_get_iterator(md_attrs);
        if (iter) {
            do {
                int s, e;
                pango_attr_iterator_range(iter, &s, &e);
                GSList *list = pango_attr_iterator_get_attrs(iter);
                for (GSList *l = list; l; l = l->next) {
                    PangoAttribute *a = (PangoAttribute *)l->data;
                    PangoAttribute *copy = pango_attribute_copy(a);
                    copy->start_index = (guint)s;
                    copy->end_index = (guint)e;
                    pango_attr_list_insert(attrs, copy);
                }
                g_slist_free(list);
            } while (pango_attr_iterator_next(iter));
            pango_attr_iterator_destroy(iter);
        }
        pango_attr_list_unref(md_attrs);
    }

    if (config.highlight_current && editor.len > 0) {
        /* Apply line highlight: dim non-current lines */
        int cursor_line_idx, cursor_line_off;
        pango_layout_index_to_line_x(pango_layout, (int)editor.cursor,
                                      0, &cursor_line_idx, &cursor_line_off);
        (void)cursor_line_off;

        int n_lines = pango_layout_get_line_count(pango_layout);
        PangoColor dim_color;
        pango_color_parse(&dim_color, active_theme->dim);

        for (int i = 0; i < n_lines; i++) {
            if (i == cursor_line_idx) continue;
            PangoLayoutLine *line = pango_layout_get_line_readonly(pango_layout, i);
            if (!line) continue;

            size_t start = line->start_index;
            size_t end;
            if (i + 1 < n_lines) {
                PangoLayoutLine *next = pango_layout_get_line_readonly(pango_layout, i + 1);
                end = next ? (size_t)next->start_index : (size_t)editor.len;
            } else {
                end = (size_t)editor.len;
            }
            if (end > start) {
                PangoAttribute *a = pango_attr_foreground_new(dim_color.red,
                                                               dim_color.green,
                                                               dim_color.blue);
                a->start_index = (guint)start;
                a->end_index = (guint)end;
                pango_attr_list_change(attrs, a);
            }
        }

        /* Draw current line highlight background */
        if (cursor_line_idx < n_lines) {
            PangoLayoutLine *cline = pango_layout_get_line_readonly(pango_layout, cursor_line_idx);
            if (cline) {
                PangoRectangle line_rect;
                pango_layout_line_get_extents(cline, NULL, &line_rect);
                int ly = line_rect.y / PANGO_SCALE - scroll_y;
                int lh = line_rect.height / PANGO_SCALE;
                if (lh < 1) lh = get_line_height();

                /* Subtle highlight for current line */
                double hr, hg, hb;
                parse_color(active_theme->selection_bg, &hr, &hg, &hb);
                cairo_set_source_rgba(cr, hr, hg, hb, 0.25);
                cairo_rectangle(cr, PAD, PAD + ly, vis_w, lh);
                cairo_fill(cr);
            }
        }
    }

    pango_layout_set_attributes(pango_layout, attrs);
    pango_attr_list_unref(attrs);

    /* Clip and translate for scrolling */
    cairo_save(cr);
    cairo_rectangle(cr, PAD, PAD, vis_w, vis_h);
    cairo_clip(cr);
    cairo_translate(cr, PAD, PAD - scroll_y);

    /* Set text color */
    double fg_r, fg_g, fg_b;
    parse_color(config.fg, &fg_r, &fg_g, &fg_b);
    cairo_set_source_rgb(cr, fg_r, fg_g, fg_b);

    /* Draw text */
    pango_cairo_show_layout(cr, pango_layout);
    cairo_restore(cr);

    /* Draw selection highlight */
    if (editor.has_sel && editor.sel_start != editor.sel_end) {
        size_t ss = editor.sel_start < editor.sel_end ? editor.sel_start : editor.sel_end;
        size_t se = editor.sel_start < editor.sel_end ? editor.sel_end : editor.sel_start;

        /* Get selection rectangles */
        /* Draw selection by iterating over lines */
        double sr, sg, sb;
        parse_color(active_theme->selection_bg, &sr, &sg, &sb);
        cairo_set_source_rgba(cr, sr, sg, sb, 0.6);

        int n_lines = pango_layout_get_line_count(pango_layout);
        for (int i = 0; i < n_lines; i++) {
            PangoLayoutLine *line = pango_layout_get_line_readonly(pango_layout, i);
            if (!line) continue;
            size_t lstart = line->start_index;
            size_t lend = (i + 1 < n_lines)
                ? (size_t)pango_layout_get_line_readonly(pango_layout, i + 1)->start_index
                : (size_t)editor.len;

            /* Check if this line overlaps with selection */
            if (lend <= ss || lstart >= se) continue;

            /* Clamp to selection range */
            size_t draw_start = (ss > lstart) ? ss : lstart;
            size_t draw_end = (se < lend) ? se : lend;

            /* Get x positions */
            int x1 = 0, x2 = 0;
            pango_layout_line_index_to_x(line, (int)draw_start, 0, &x1);
            pango_layout_line_index_to_x(line, (int)draw_end, 0, &x2);

            PangoRectangle lr;
            pango_layout_line_get_extents(line, NULL, &lr);
            int ly = lr.y / PANGO_SCALE - scroll_y;
            int lh = lr.height / PANGO_SCALE;
            if (lh < 1) lh = get_line_height();

            int rx = PAD + x1 / PANGO_SCALE;
            int rw = (x2 - x1) / PANGO_SCALE;
            if (rw < 2) rw = 2;

            if (ly + lh > PAD && ly < h - PAD)
                cairo_rectangle(cr, rx, ly, rw, lh);
        }
        cairo_fill(cr);
    }

    /* Draw cursor */
    if (!composing) {
        PangoRectangle strong_pos, weak_pos;
        pango_layout_get_cursor_pos(pango_layout, (int)editor.cursor,
                                     &strong_pos, &weak_pos);

        int cx = PAD + strong_pos.x / PANGO_SCALE;
        int cy = PAD + strong_pos.y / PANGO_SCALE - scroll_y;
        int ch = strong_pos.height / PANGO_SCALE;
        if (ch < 1) ch = get_line_height();

        if (cy + ch >= PAD && cy <= h - PAD) {
            double cr_r, cr_g, cr_b;
            parse_color(active_theme->cursor_color, &cr_r, &cr_g, &cr_b);
            cairo_set_source_rgb(cr, cr_r, cr_g, cr_b);
            cairo_rectangle(cr, cx, cy, 2, ch);
            cairo_fill(cr);
        }
    }

    /* Draw preedit text (input method composing) */
    if (composing && preedit_buf[0]) {
        PangoRectangle strong_pos, weak_pos;
        pango_layout_get_cursor_pos(pango_layout, (int)editor.cursor,
                                     &strong_pos, &weak_pos);

        int cx = PAD + strong_pos.x / PANGO_SCALE;
        int cy = PAD + strong_pos.y / PANGO_SCALE - scroll_y;

        /* Create a temporary layout for preedit text */
        PangoLayout *pl = pango_cairo_create_layout(cr);
        pango_layout_set_text(pl, preedit_buf, -1);
        pango_layout_set_font_description(pl, font_desc);

        /* Add underline to preedit text */
        PangoAttrList *pa = pango_attr_list_new();
        PangoAttribute *ua = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        ua->start_index = 0;
        ua->end_index = (guint)strlen(preedit_buf);
        pango_attr_list_insert(pa, ua);

        /* Highlight the preedit text with accent color */
        PangoColor preedit_color;
        pango_color_parse(&preedit_color, active_theme->heading[0]);
        PangoAttribute *ca = pango_attr_foreground_new(preedit_color.red,
                                                        preedit_color.green,
                                                        preedit_color.blue);
        ca->start_index = 0;
        ca->end_index = (guint)strlen(preedit_buf);
        pango_attr_list_insert(pa, ca);
        pango_layout_set_attributes(pl, pa);
        pango_attr_list_unref(pa);

        /* Draw preedit text */
        double pe_r, pe_g, pe_b;
        parse_color(active_theme->fg, &pe_r, &pe_g, &pe_b);
        cairo_set_source_rgb(cr, pe_r, pe_g, pe_b);

        cairo_save(cr);
        cairo_rectangle(cr, PAD, PAD, vis_w, vis_h);
        cairo_clip(cr);
        cairo_move_to(cr, cx, cy);
        pango_cairo_show_layout(cr, pl);
        cairo_restore(cr);

        /* Draw preedit caret */
        if (preedit_caret >= 0 && preedit_caret <= (int)strlen(preedit_buf)) {
            PangoRectangle pc_strong, pc_weak;
            pango_layout_get_cursor_pos(pl, preedit_caret, &pc_strong, &pc_weak);
            int pcx = cx + pc_strong.x / PANGO_SCALE;
            int pcy = cy + pc_strong.y / PANGO_SCALE;
            int pch = pc_strong.height / PANGO_SCALE;
            if (pch < 1) pch = get_line_height();

            double cr_r, cr_g, cr_b;
            parse_color(active_theme->cursor_color, &cr_r, &cr_g, &cr_b);
            cairo_set_source_rgb(cr, cr_r, cr_g, cr_b);
            cairo_rectangle(cr, pcx, pcy, 2, pch);
            cairo_fill(cr);
        }

        g_object_unref(pl);
    }

    /* Flush drawing */
    cairo_surface_flush(cairo_surface);
    XFlush(dpy);
}

/* ======================== Markdown Action Helpers ======================== */

/*
 * Get current line range and content.
 */
static void get_current_line(size_t *ls, size_t *le)
{
    *ls = line_start(&editor, editor.cursor);
    *le = line_end(&editor, editor.cursor);
}

/*
 * Wrap selection (or insert empty pair at cursor) with markdown markers.
 */
static void markdown_wrap(const char *prefix, const char *suffix)
{
    size_t ss, se;
    if (editor.has_sel) {
        ss = editor.sel_start < editor.sel_end ? editor.sel_start : editor.sel_end;
        se = editor.sel_start < editor.sel_end ? editor.sel_end : editor.sel_start;
    } else {
        ss = se = editor.cursor;
    }

    size_t plen = strlen(prefix);
    size_t slen = strlen(suffix);

    /* Insert suffix first (so indices stay valid) */
    textbuf_insert(&editor, suffix, slen);
    /* Now cursor is at se + slen, move back to ss */
    editor.cursor = ss;
    textbuf_insert(&editor, prefix, plen);

    /* Select the inner content */
    editor.sel_start = ss + plen;
    editor.sel_end = ss + plen + (se - ss);
    editor.has_sel = 1;
    editor.cursor = editor.sel_end;
}

/*
 * Set or replace heading level on the current line.
 */
static void markdown_heading(int level)
{
    size_t ls, le;
    get_current_line(&ls, &le);

    /* Check existing heading markers */
    int existing = 0;
    while (existing < 6 && ls + existing < editor.len && editor.data[ls + existing] == '#')
        existing++;

    /* Remove existing markers and trailing space */
    size_t content_start = ls;
    if (existing > 0) {
        content_start = ls + existing;
        if (content_start < editor.len && editor.data[content_start] == ' ')
            content_start++;
    }

    /* Remove old heading markers */
    if (content_start > ls) {
        memmove(editor.data + ls, editor.data + content_start, editor.len - content_start);
        editor.len -= (content_start - ls);
        editor.data[editor.len] = '\0';
    }

    /* Insert new heading */
    char heading[8] = {0};
    for (int i = 0; i < level; i++) heading[i] = '#';
    heading[level] = ' ';
    heading[level + 1] = '\0';

    size_t hlen = level + 1;
    textbuf_ensure_cap(&editor, editor.len + hlen + 1);
    memmove(editor.data + ls + hlen, editor.data + ls, editor.len - ls);
    memcpy(editor.data + ls, heading, hlen);
    editor.len += hlen;
    editor.data[editor.len] = '\0';

    /* Fix cursor position */
    if (editor.cursor >= ls) {
        editor.cursor += hlen;
        if (editor.has_sel) {
            if (editor.sel_start >= ls) editor.sel_start += hlen;
            if (editor.sel_end >= ls) editor.sel_end += hlen;
        }
    }
}

/* ======================== Theme Loading ======================== */

/*
 * Load a custom theme from a simple key=value config file.
 * Format: key=color (one per line), e.g. bg=#1a1b26
 */
static Theme *load_custom_theme(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "editor: cannot open theme file: %s\n", path);
        return NULL;
    }

    static Theme custom;
    /* Start with tokyonight defaults */
    memcpy(&custom, &theme_tokyonight, sizeof(Theme));
    custom.name = "custom";

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* Trim whitespace */
        while (*key == ' ' || *key == '\t') key++;
        char *kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = '\0';

        while (*val == ' ' || *val == '\t') val++;
        char *vend = val + strlen(val) - 1;
        while (vend > val && (*vend == ' ' || *vend == '\t')) *vend-- = '\0';

        if (val[0] == '\0') continue;

        /* Assign values */
        if (strcmp(key, "bg") == 0)                custom.bg = strdup(val);
        else if (strcmp(key, "fg") == 0)           custom.fg = strdup(val);
        else if (strcmp(key, "cursor") == 0)       custom.cursor_color = strdup(val);
        else if (strcmp(key, "dim") == 0)          custom.dim = strdup(val);
        else if (strcmp(key, "selection_bg") == 0) custom.selection_bg = strdup(val);
        else if (strcmp(key, "h1") == 0)           custom.heading[0] = strdup(val);
        else if (strcmp(key, "h2") == 0)           custom.heading[1] = strdup(val);
        else if (strcmp(key, "h3") == 0)           custom.heading[2] = strdup(val);
        else if (strcmp(key, "h4") == 0)           custom.heading[3] = strdup(val);
        else if (strcmp(key, "h5") == 0)           custom.heading[4] = strdup(val);
        else if (strcmp(key, "h6") == 0)           custom.heading[5] = strdup(val);
        else if (strcmp(key, "bold") == 0)         custom.bold_color = strdup(val);
        else if (strcmp(key, "italic") == 0)       custom.italic_color = strdup(val);
        else if (strcmp(key, "underline") == 0)       custom.underline_color = strdup(val);
        else if (strcmp(key, "strikethrough") == 0)   custom.strikethrough_color = strdup(val);
        else if (strcmp(key, "highlight_bg") == 0)     custom.highlight_bg = strdup(val);
        else if (strcmp(key, "code_bg") == 0)          custom.code_bg = strdup(val);
        else if (strcmp(key, "code_fg") == 0)      custom.code_fg = strdup(val);
        else if (strcmp(key, "link") == 0)         custom.link_color = strdup(val);
        else if (strcmp(key, "blockquote") == 0)   custom.blockquote = strdup(val);
        else if (strcmp(key, "name") == 0)         custom.name = strdup(val);
    }

    fclose(f);
    return &custom;
}

static Theme *resolve_theme(const char *arg)
{
    if (!arg || arg[0] == '\0')
        return &theme_tokyonight;

    /* Check built-in themes */
    if (strcasecmp(arg, "tokyonight") == 0)    return &theme_tokyonight;
    if (strcasecmp(arg, "dracula") == 0)        return &theme_dracula;
    if (strcasecmp(arg, "nord") == 0)           return &theme_nord;
    if (strcasecmp(arg, "gruvbox") == 0 ||
        strcasecmp(arg, "gruvbox-dark") == 0)   return &theme_gruvbox;

    /* Try as file path */
    Theme *t = load_custom_theme(arg);
    if (t) return t;

    fprintf(stderr, "editor: unknown theme '%s', falling back to tokyonight\n", arg);
    return &theme_tokyonight;
}

/* ======================== Argument Parsing ======================== */

static void print_usage(void)
{
    fprintf(stderr,
        "editor - minimalist X11 text editor\n"
        "\n"
        "Usage: editor [options]\n"
        "\n"
        "Options:\n"
        "  -w <width>   Window width in pixels (default: 600)\n"
        "  -h <height>  Window height in pixels (default: 400)\n"
        "  -x <x>       Window X position (default: centered)\n"
        "  -y <y>       Window Y position (default: centered)\n"
        "  -bg <color>  Background color (default: theme bg)\n"
        "  -fg <color>  Foreground color (default: theme fg)\n"
        "  -fn <font>   Font in Pango format (default: Monospace 13)\n"
        "  -z           Highlight current line, dim others\n"
        "  -m           Enable markdown syntax highlighting\n"
        "  -t <theme>   Theme name or path to theme file (default: tokyonight)\n"
        "               Built-in: tokyonight, dracula, nord, gruvbox\n"
        "\n"
        "Keys:\n"
        "  Ctrl+S       Save (output text to stdout) and exit\n"
        "  Esc          Cancel and exit\n"
        "  Ctrl+B       Bold **text** (markdown mode)\n"
        "  Ctrl+I       Italic *text* (markdown mode)\n"
        "  Ctrl+U       Underline __text__ (markdown mode)\n"
        "  Ctrl+`       Strikethrough ~~text~~ (markdown mode)\n"
        "  Ctrl+=       Highlight ==text== (markdown mode)\n"
        "  Ctrl+1~6     Headings (markdown mode)\n"
        "  Ctrl+A       Home / Select all\n"
        "  Ctrl+E       End\n"
        "  Ctrl+K       Delete to end of line\n"
        "  Ctrl+U       Delete to start of line\n"
        "  Ctrl+W       Delete previous word\n"
        "\n"
        "Theme file format (key=value per line):\n"
        "  name=MyTheme\n"
        "  bg=#1a1b26\n"
        "  fg=#c0caf5\n"
        "  cursor=#c0caf5\n"
        "  dim=#3b4261\n"
        "  selection_bg=#283457\n"
        "  h1=#7aa2f7  h2=#7dcfff  ...  h6=#f7768e\n"
        "  bold=#ff9e64  italic=#bb9af7  underline=#73daca\n"
        "  code_bg=#292e42  code_fg=#e0af68\n"
        "  link=#7aa2f7  blockquote=#565f89\n"
    );
}

static void parse_args(int argc, char **argv)
{
    /* Defaults */
    config.width = 600;
    config.height = 400;
    config.pos_x = -1;  /* centered */
    config.pos_y = -1;  /* centered */
    config.bg = NULL;
    config.fg = NULL;
    config.font = NULL;
    config.highlight_current = 0;
    config.markdown_mode = 0;
    config.theme_arg = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            config.width = atoi(argv[++i]);
            if (config.width < 100) config.width = 100;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            config.height = atoi(argv[++i]);
            if (config.height < 50) config.height = 50;
        } else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            config.pos_x = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-y") == 0 && i + 1 < argc) {
            config.pos_y = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-bg") == 0 && i + 1 < argc) {
            config.bg = argv[++i];
        } else if (strcmp(argv[i], "-fg") == 0 && i + 1 < argc) {
            config.fg = argv[++i];
        } else if (strcmp(argv[i], "-fn") == 0 && i + 1 < argc) {
            config.font = argv[++i];
        } else if (strcmp(argv[i], "-z") == 0) {
            config.highlight_current = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            config.markdown_mode = 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            config.theme_arg = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            exit(0);
        } else {
            fprintf(stderr, "editor: unknown option: %s\n", argv[i]);
            print_usage();
            exit(1);
        }
        i++;
    }

    /* Resolve theme first (for default colors) */
    active_theme = resolve_theme(config.theme_arg);

    /* Apply theme defaults for bg/fg if not specified */
    if (!config.bg) config.bg = (char *)active_theme->bg;
    if (!config.fg) config.fg = (char *)active_theme->fg;
    if (!config.font) config.font = "Monospace 13";
}

/* ======================== Read Stdin ======================== */

static char *read_stdin(void)
{
    if (isatty(STDIN_FILENO))
        return strdup("");

    size_t cap = INIT_CAP;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return strdup("");

    size_t n;
    while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len >= cap - 1) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) return strdup("");
        }
    }
    buf[len] = '\0';
    return buf;
}

/* ======================== Key Event Handling ======================== */

static void handle_keypress(XKeyEvent *ev)
{
    KeySym keysym = NoSymbol;
    char buf[64];
    int n = 0;
    Status status = XLookupNone;

    /* XFilterEvent is already called at the top of the main event loop */

    /* Lookup the key through XIC (input method) */
    if (xic) {
        n = Xutf8LookupString(xic, ev, buf, sizeof(buf) - 1, &keysym, &status);
    } else {
        n = XLookupString(ev, buf, sizeof(buf) - 1, &keysym, NULL);
        status = (n > 0) ? XLookupBoth : XLookupKeySym;
    }

    /*
     * Detect modifier state BEFORE deciding whether to treat the event as
     * text input or as a shortcut.  This is critical because X11 maps
     * certain Ctrl+key combos to control characters that overlap with
     * printable shortcuts — most notably Ctrl+I → Tab (ASCII 0x09).
     *
     * When Ctrl is held we MUST NOT auto-insert the lookup result, otherwise
     * Ctrl+I (italic) becomes a tab, Ctrl+M becomes a CR, etc.
     */
    int ctrl  = (ev->state & ControlMask);
    int shift = (ev->state & ShiftMask);

    /*
     * PRIORITY: If the input method committed text, insert it immediately.
     *
     * PreeditNothing mode (IM manages candidate window):
     *   status == XLookupChars  -> IM committed text (e.g. Chinese chars)
     *   status == XLookupBoth   -> IM committed text + keysym
     *
     * PreeditCallbacks mode:
     *   XFilterEvent consumed preedit keys -> we only see committed results
     *   status == XLookupChars  -> final committed text
     *
     * IMPORTANT: Skip auto-insertion when Ctrl is held.  X11 encodes
     * Ctrl+I as Tab, Ctrl+J as LF, Ctrl+M as CR, etc.  These must be
     * routed to the shortcut handler below, not inserted as text.
     */
    if (!ctrl && (status == XLookupChars ||
        (status == XLookupBoth && n > 0 &&
         !(n == 1 && (unsigned char)buf[0] < 0x20 && buf[0] != '\t' && buf[0] != '\n')))) {
        buf[n] = '\0';
        textbuf_insert(&editor, buf, (size_t)n);
        ensure_cursor_visible();
        draw();
        return;
    }

    if (status == XLookupNone || keysym == NoSymbol) {
        return;
    }

    /* ===== Control combinations ===== */
    if (ctrl) {
        switch (keysym) {
        case XK_s:
            /* Save: output text to stdout and exit */
            fflush(stdout);
            fwrite(editor.data, 1, editor.len, stdout);
            fflush(stdout);
            exit(0);
            break;

        case XK_bracketleft:
        case XK_Escape:
            /* Cancel and exit */
            exit(1);
            break;

        case XK_b:
            /* Bold **text** in markdown mode; backward word otherwise */
            if (config.markdown_mode)
                markdown_wrap("**", "**");
            else {
                if (shift)
                    sel_update_shift(&editor, word_edge(&editor, editor.cursor, -1));
                else
                    editor.cursor = word_edge(&editor, editor.cursor, -1);
            }
            break;

        case XK_i:
        case XK_Tab:
            /*
             * Italic *text* in markdown mode.
             * XK_Tab is needed because X11 encodes Ctrl+I as Tab (ASCII 0x09).
             */
            if (config.markdown_mode)
                markdown_wrap("*", "*");
            break;

        case XK_u:
            /* Underline __text__ in markdown mode */
            if (config.markdown_mode)
                markdown_wrap("__", "__");
            else
                textbuf_delete_range(&editor, line_start(&editor, editor.cursor), editor.cursor);
            break;

        case XK_grave:
            /* Strikethrough ~~text~~ in markdown mode (Ctrl+`) */
            if (config.markdown_mode)
                markdown_wrap("~~", "~~");
            break;

        case XK_equal:
            /* Highlight ==text== in markdown mode (Ctrl+=) */
            if (config.markdown_mode)
                markdown_wrap("==", "==");
            break;

        case XK_1: case XK_2: case XK_3:
        case XK_4: case XK_5: case XK_6:
            if (config.markdown_mode)
                markdown_heading(keysym - XK_0);
            break;

        case XK_a:
            /* Select all */
            editor.sel_start = 0;
            editor.sel_end = editor.len;
            editor.has_sel = 1;
            editor.cursor = editor.len;
            break;

        case XK_e:
            /* End of line */
            if (shift) {
                sel_update_shift(&editor, line_end(&editor, editor.cursor));
            } else {
                move_cursor_end(&editor);
            }
            break;

        case XK_k:
            /* Delete to end of line */
            {
                size_t le = line_end(&editor, editor.cursor);
                textbuf_delete_range(&editor, editor.cursor, le);
            }
            break;

        case XK_w:
            /* Delete previous word */
            {
                size_t we = word_edge(&editor, editor.cursor, -1);
                textbuf_delete_range(&editor, we, editor.cursor);
            }
            break;

        case XK_d:
            /* Delete next word */
            {
                size_t we = word_edge(&editor, editor.cursor, +1);
                textbuf_delete_range(&editor, editor.cursor, we);
            }
            break;

        case XK_f:
            /* Move forward one word */
            if (shift) {
                sel_update_shift(&editor, word_edge(&editor, editor.cursor, +1));
            } else {
                editor.cursor = word_edge(&editor, editor.cursor, +1);
            }
            break;

        case XK_Left:
            /* Ctrl+Left: backward word */
            if (shift) {
                sel_update_shift(&editor, word_edge(&editor, editor.cursor, -1));
            } else {
                editor.cursor = word_edge(&editor, editor.cursor, -1);
            }
            break;

        case XK_Right:
            /* Ctrl+Right: forward word */
            if (shift) {
                sel_update_shift(&editor, word_edge(&editor, editor.cursor, +1));
            } else {
                editor.cursor = word_edge(&editor, editor.cursor, +1);
            }
            break;

        case XK_Home:
            /* Ctrl+Home: beginning of document */
            if (shift) {
                sel_update_shift(&editor, 0);
            } else {
                move_cursor_to_start(&editor);
            }
            break;

        case XK_End:
            /* Ctrl+End: end of document */
            if (shift) {
                sel_update_shift(&editor, editor.len);
            } else {
                move_cursor_to_end(&editor);
            }
            break;

        case XK_j:
            /* Ctrl+J = newline */
            textbuf_insert(&editor, "\n", 1);
            break;

        case XK_l:
            /* Clear screen / redraw */
            draw();
            break;

        default:
            break;
        }
        ensure_cursor_visible();
        draw();
        return;
    }

    /* ===== Non-control keys ===== */
    switch (keysym) {
    case XK_Escape:
        exit(1);
        break;

    case XK_Return:
        textbuf_insert(&editor, "\n", 1);
        break;

    case XK_BackSpace:
        textbuf_delete_char(&editor, -1);
        break;

    case XK_Delete:
        textbuf_delete_char(&editor, +1);
        break;

    case XK_Tab:
        textbuf_insert(&editor, "    ", TAB_SIZE);
        break;

    case XK_Left:
        if (shift)
            sel_update_shift(&editor,
                utf8_next_rune(editor.data, editor.len, editor.cursor, -1));
        else
            move_cursor_left(&editor);
        break;

    case XK_Right:
        if (shift)
            sel_update_shift(&editor,
                utf8_next_rune(editor.data, editor.len, editor.cursor, +1));
        else
            move_cursor_right(&editor);
        break;

    case XK_Up:
        if (shift) {
            size_t old = editor.cursor;
            move_cursor_up(&editor);
            if (old != editor.cursor)
                sel_update_shift(&editor, editor.cursor);
        } else {
            move_cursor_up(&editor);
        }
        break;

    case XK_Down:
        if (shift) {
            size_t old = editor.cursor;
            move_cursor_down(&editor);
            if (old != editor.cursor)
                sel_update_shift(&editor, editor.cursor);
        } else {
            move_cursor_down(&editor);
        }
        break;

    case XK_Home:
        if (shift) {
            sel_update_shift(&editor, line_start(&editor, editor.cursor));
        } else {
            move_cursor_home(&editor);
        }
        break;

    case XK_End:
        if (shift) {
            sel_update_shift(&editor, line_end(&editor, editor.cursor));
        } else {
            move_cursor_end(&editor);
        }
        break;

    case XK_Page_Up:
        {
            int lh = get_line_height();
            int vis_h = config.height - 2 * PAD;
            int lines_to_scroll = vis_h / (lh > 0 ? lh : 18) - 1;
            for (int j = 0; j < lines_to_scroll; j++)
                move_cursor_up(&editor);
            scroll_y -= vis_h;
            if (scroll_y < 0) scroll_y = 0;
            if (shift)
                sel_update_shift(&editor, editor.cursor);
        }
        break;

    case XK_Page_Down:
        {
            int lh = get_line_height();
            int vis_h = config.height - 2 * PAD;
            int lines_to_scroll = vis_h / (lh > 0 ? lh : 18) - 1;
            for (int j = 0; j < lines_to_scroll; j++)
                move_cursor_down(&editor);
            scroll_y += vis_h;
            if (shift)
                sel_update_shift(&editor, editor.cursor);
        }
        break;

    default:
        /* Printable character: insert UTF-8 text from lookup string */
        if (n > 0) {
            buf[n] = '\0';
            /* Filter out control characters */
            int is_printable = 1;
            for (int j = 0; j < n; j++) {
                if (buf[j] < 0x20 && buf[j] != '\t') {
                    is_printable = 0;
                    break;
                }
            }
            if (is_printable)
                textbuf_insert(&editor, buf, (size_t)n);
        }
        break;
    }

    ensure_cursor_visible();
    draw();
}

/* ======================== Main ======================== */

int main(int argc, char **argv)
{
    /* Set locale for UTF-8 and input method */
    setlocale(LC_ALL, "");

    /*
     * CRITICAL: Set Xlib locale modifiers so that XOpenIM can find
     * the correct input method server (ibus, fcitx, etc.).
     * Without this, XMODIFIERS=@im=fcitx is ignored and XOpenIM fails.
     */
    XSetLocaleModifiers("");

    /* Check if nl_langinfo confirms UTF-8 */
    const char *codeset = nl_langinfo(CODESET);
    if (!codeset || (strcasecmp(codeset, "UTF-8") != 0 &&
                     strcasecmp(codeset, "utf8") != 0)) {
        fprintf(stderr, "editor: warning: locale codeset is '%s', expected UTF-8\n",
                codeset ? codeset : "unknown");
    }

    /* Parse command line arguments */
    parse_args(argc, argv);

    /* Read stdin if available */
    char *initial_text = read_stdin();

    /* Center window if position not specified */
    if (config.pos_x < 0 || config.pos_y < 0) {
        /* Temporary display connection to get screen size */
        Display *td = XOpenDisplay(NULL);
        if (td) {
            int ts = DefaultScreen(td);
            int sw = DisplayWidth(td, ts);
            int sh = DisplayHeight(td, ts);
            if (config.pos_x < 0)
                config.pos_x = (sw - config.width) / 2;
            if (config.pos_y < 0)
                config.pos_y = (sh - config.height) / 2;
            XCloseDisplay(td);
        } else {
            config.pos_x = 0;
            config.pos_y = 0;
        }
    }

    /* Initialize text buffer */
    textbuf_init(&editor, initial_text);
    free(initial_text);

    /* Initialize X11 */
    x11_init();

    /* Initialize rendering (Cairo + Pango) */
    rendering_init();

    /* Initialize input method */
    init_input_method();

    /* Initial draw */
    ensure_cursor_visible();
    draw();

    /* Main event loop */
    XEvent event;
    while (1) {
        XNextEvent(dpy, &event);

        /*
         * CRITICAL: Let the input method filter EVERY event before we process it.
         * Many IM implementations (ibus, fcitx) need to intercept FocusIn,
         * FocusOut, PropertyNotify, ClientMessage, etc. to manage their state.
         * XFilterEvent returns True if the event was consumed by the IM.
         */
        if (xic && XFilterEvent(&event, None))
            continue;

        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0)
                draw();
            break;

        case KeyPress:
            handle_keypress(&event.xkey);
            break;

        case FocusIn:
            if (xic)
                XSetICFocus(xic);
            break;

        case FocusOut:
            if (xic)
                XUnsetICFocus(xic);
            break;

        case ConfigureNotify:
            {
                int nw = event.xconfigure.width;
                int nh = event.xconfigure.height;
                if (nw != config.width || nh != config.height) {
                    config.width = nw;
                    config.height = nh;
                    rendering_resize(nw, nh);
                    ensure_cursor_visible();
                    draw();
                }
            }
            break;

        case VisibilityNotify:
            if (event.xvisibility.state != VisibilityUnobscured)
                XRaiseWindow(dpy, win);
            break;

        case DestroyNotify:
            goto cleanup;

        default:
            break;
        }
    }

cleanup:
    /* Cleanup resources */
    if (pango_layout) g_object_unref(pango_layout);
    if (font_desc) pango_font_description_free(font_desc);
    if (cr) cairo_destroy(cr);
    if (cairo_surface) cairo_surface_destroy(cairo_surface);
    if (xft_draw) XftDrawDestroy(xft_draw);
    if (xft_font) XftFontClose(dpy, xft_font);
    if (xic) XDestroyIC(xic);
    if (xim) XCloseIM(xim);
    if (win) XDestroyWindow(dpy, win);
    if (dpy) XCloseDisplay(dpy);
    free(editor.data);

    return 0;
}
