// Home Row Horde: pixel art, palette, and font. Everything is drawn in code;
// the game loads no image files.
#ifndef HRH_GFX_H
#define HRH_GFX_H

#include <SDL3/SDL.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240

static SDL_Renderer *ren;

// ---- palette (an NES-flavoured set) ----------------------------------------
#define C_BLACK  0x000000
#define C_WHITE  0xFCFCFC
#define C_GREY   0xBCBCBC
#define C_DGREY  0x7C7C7C
#define C_RED    0xD82800
#define C_DRED   0xA81000
#define C_SKIN   0xFC9838
#define C_BROWN  0xAC7C00
#define C_DBROWN 0x503000
#define C_YELLOW 0xF8B800
#define C_GREEN  0x00A800
#define C_DGREEN 0x005800
#define C_ZOMBIE 0x58D854
#define C_BLUE   0x0058F8
#define C_NAVY   0x0000BC
#define C_PURPLE 0x6844FC
#define C_DPURP  0x44009C
#define C_TEAL   0x008888
#define C_CYAN   0x3CBCFC
#define C_PINK   0xF878F8

static Uint32 pal(char c) {
  switch (c) {
    case 'K': return C_BLACK;  case 'W': return C_WHITE;  case 'G': return C_GREY;
    case 'g': return C_DGREY;  case 'R': return C_RED;    case 'r': return C_DRED;
    case 'O': return C_SKIN;   case 'o': return C_BROWN;  case 'b': return C_DBROWN;
    case 'Y': return C_YELLOW; case 'L': return C_GREEN;  case 'l': return C_DGREEN;
    case 'z': return C_ZOMBIE; case 'B': return C_BLUE;   case 'n': return C_NAVY;
    case 'P': return C_PURPLE; case 'p': return C_DPURP;  case 'T': return C_TEAL;
    case 'C': return C_CYAN;   case 'M': return C_PINK;
  }
  return 0xFF00FF;
}

typedef struct { int w, h; SDL_Texture *t; } Spr;

// Build a texture from rows of palette characters. '.' is transparent. `swap`
// is an optional list of from/to character pairs for palette-swapped variants.
static Spr make_sprite(const char *const *rows, int n, const char *swap) {
  Spr s = { 0, n, NULL };
  for (int i = 0; i < n; i++) { int l = (int)strlen(rows[i]); if (l > s.w) s.w = l; }
  Uint8 *px = SDL_calloc((size_t)s.w * s.h, 4);
  for (int y = 0; y < n; y++) {
    int l = (int)strlen(rows[y]);
    for (int x = 0; x < l; x++) {
      char c = rows[y][x];
      if (c == '.') continue;
      for (const char *p = swap; p && p[0] && p[1]; p += 2) if (*p == c) { c = p[1]; break; }
      Uint32 rgb = pal(c);
      Uint8 *o = px + 4 * (y * s.w + x);
      o[0] = rgb >> 16; o[1] = (rgb >> 8) & 255; o[2] = rgb & 255; o[3] = 255;
    }
  }
  s.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, s.w, s.h);
  SDL_UpdateTexture(s.t, NULL, px, s.w * 4);
  SDL_SetTextureScaleMode(s.t, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(s.t, SDL_BLENDMODE_BLEND);
  SDL_free(px);
  return s;
}
#define SPRITE(rows, swap) make_sprite(rows, (int)SDL_arraysize(rows), swap)

// ---- sprites ---------------------------------------------------------------
#define ZOMBIE_TOP \
  "......KKKKKK....", \
  ".....KzzzzzzK...", \
  "....KzzzzzzzzK..", \
  "....KzRzzRzzzK..", \
  "....KzzzzzzzzK..", \
  "....KzKWKWKzK...", \
  ".....KzzzzzK....", \
  "....KKPPPPKK....", \
  "..KKzKPPPPPPK...", \
  ".KzzzzPPPPPPK...", \
  ".KzKKKPPPPPPK...", \
  "..K..KPPpPPPK...", \
  ".KzzzzPPpPPPK...", \
  ".KzKKKPPPPPPK...", \
  "..K..KPPPPPPK...", \
  ".....KooooooK...", \
  ".....KooooooK..."

static const char *const zombie_a[] = { ZOMBIE_TOP,
  ".....KooKKooK...",
  ".....KooK.KooK..",
  "....KooK..KooK..",
  "....KooK..KooK..",
  "....KzzK..KzzK..",
  "...KzzzK.KzzzK..",
  "...KKKKK.KKKKK..",
};
static const char *const zombie_b[] = { ZOMBIE_TOP,
  ".....KooooooK...",
  "......KooooK....",
  "......KooooK....",
  "......KooooK....",
  "......KzzzzK....",
  ".....KzzzzzK....",
  ".....KKKKKKK....",
};

static const char *const bat_a[] = {
  "K..K........K..K",
  "KPKK........KKPK",
  "KPPK..K..K..KPPK",
  "KPPPKKPKKPKKPPPK",
  "KPPPPPPPPPPPPPPK",
  ".KPPPRPPPPRPPPK.",
  "..KPPPPPPPPPPK..",
  "...KKPPWWPPKK...",
  ".....KPPPPK.....",
  "......KKKK......",
};
static const char *const bat_b[] = {
  "......K..K......",
  ".....KPKKPK.....",
  "..KKKPPPPPPKKK..",
  ".KPPPRPPPPRPPPK.",
  "KPPPPPPPPPPPPPPK",
  "KPPPKPPWWPPKPPPK",
  "KPPK.KPPPPK.KPPK",
  "KPK...KKKK...KPK",
  "KK............KK",
};

static const char *const brute[] = {
  ".........ll.........",
  "........KllK........",
  ".....KKKKllKKKK.....",
  "...KKOOOOOOOOOOKK...",
  "..KOOOYOOOOOOYOOOK..",
  ".KOOOOOOOOOOOOOOOOK.",
  ".KOOKKKOOOOOOKKKOOK.",
  ".KOOOKKOOOOOOKKOOOK.",
  ".KOOOOOOOKKOOOOOOOK.",
  ".KOOKOOOOOOOOOOKOOK.",
  ".KOOKKYKKYKKYKKKOOK.",
  "..KOOKKKKKKKKKKOOK..",
  "...KKOOOOOOOOOOKK...",
  ".....KKKKKKKKKK.....",
  "....KooooooooooK....",
  "..KKooobooooboooKK..",
  ".KooooooooooooooooK.",
  "KooKooooooooooooKooK",
  "KooKooobooooboooKooK",
  "KooKooooooooooooKooK",
  "KzzKooooooooooooKzzK",
  "KzzKooooooooooooKzzK",
  ".KK.KooooooooooK.KK.",
  "....KooooKKooooK....",
  "....KoooK..KoooK....",
  "....KoooK..KoooK....",
  "....KoooK..KoooK....",
  "...KKoooK..KoooKK...",
  "..KggggK....KggggK..",
  "..KKKKKK....KKKKKK..",
};

static const char *const archer[] = {
  "....KKKKKK......",
  "...KTTTTTTK.....",
  "..KTTTTTTTTK....",
  "..KTTKOOOOK.....",
  "..KTTKOKOOK.....",
  "..KTTKOOOOK.....",
  "...KTTKKKK...oK.",
  "..KKTTTTKK..oK..",
  ".KTTTTTTTTKoK...",
  ".KTTTTTTTTTOKW..",
  ".KTTKTTTTKKoKW..",
  ".KTTKTTTTK.oKW..",
  ".KOOKTTTTK..oK..",
  "..KKKYYYYK...oK.",
  "...KTTTTTK......",
  "...KTTTTTK......",
  "...KTTKTTK......",
  "...KbbKbbK......",
  "...KbbKbbK......",
  "...KbbKbbK......",
  "...KbbKbbK......",
  "..KbbbKbbbK.....",
  "..KKKKKKKKK.....",
};

static const char *const bolt[] = {
  ".W.......R",
  "WoooooooYR",
  ".W.......R",
};

static const char *const tombstone[] = {
  "...KKKKKK...",
  "..KGGGGGGK..",
  ".KGGGGGGGgK.",
  ".KGGKKKGGgK.",
  ".KGGGKGGGgK.",
  ".KGGGKGGGgK.",
  ".KGGGGGGGgK.",
  ".KGGGGGGGgK.",
  ".KGGGGGGGgK.",
  "KKGGGGGGGgKK",
  "KllKlKKlKllK",
};

// The Lich: a robed skull, drawn double size as a boss.
static const char *const lich[] = {
  "......KKKKKKKK......",
  ".....KppppppppK.....",
  "....KppppppppppK....",
  "...KppKKKKKKKKppK...",
  "...KpKWWWWWWWWKpK...",
  "...KpKWKKWWKKWKpK...",
  "...KpKWKRWWRKWKpK...",
  "...KpKWWWKKWWWKpK...",
  "...KpKWWWWWWWWKpK...",
  "...KppKWKWKWKWKpK...",
  "...KpppKKKKKKKppK...",
  "..KppppppppppppppK..",
  ".KppPppppppppppPppK.",
  "KppPPppppYYppppPPppK",
  "KppKPppppYYppppPKppK",
  "KppKPppppppppppPKppK",
  "KWWKPppppppppppPKWWK",
  "KWWKPppppppppppPKWWK",
  ".KK.KPppppppppPK.KK.",
  "....KPppppppppPK....",
  "....KPppppppppPK....",
  "...KPPppppppppPPK...",
  "...KPppppppppppPK...",
  "..KPPppppppppppPPK..",
  "..KPppppppppppppPK..",
  ".KPPppppppppppppPPK.",
  ".KPppppppppppppppPK.",
  "KPPppppppppppppppPPK",
  "KPpPpPpPpPpPpPpPpPpK",
  "KKKKKKKKKKKKKKKKKKKK",
};

static const char *const heart[] = {
  ".RR.RR.",
  "RRRRRRR",
  "RRRRRRR",
  ".RRRRR.",
  "..RRR..",
  "...R...",
};

// White arrow pointing up; tinted and rotated for each dance column.
static const char *const note_arrow[] = {
  "......KK......",
  ".....KWWK.....",
  "....KWWWWK....",
  "...KWWWWWWK...",
  "..KWWWWWWWWK..",
  ".KWWWWWWWWWWK.",
  "KWWWWWWWWWWWWK",
  "KKKKKWWWWKKKKK",
  "....KWWWWK....",
  "....KWWWWK....",
  "....KWWWWK....",
  "....KWWWWK....",
  "....KWWWWK....",
  "....KKKKKK....",
};

// ---- drawing helpers ---------------------------------------------------------
static void set_color(Uint32 rgb, int a) {
  SDL_SetRenderDrawColor(ren, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, (Uint8)a);
}
static void rect_a(float x, float y, float w, float h, Uint32 rgb, int a) {
  SDL_FRect r = { x, y, w, h };
  set_color(rgb, a);
  SDL_RenderFillRect(ren, &r);
}
static void rect(float x, float y, float w, float h, Uint32 rgb) { rect_a(x, y, w, h, rgb, 255); }
static void frame(float x, float y, float w, float h, Uint32 rgb) {
  rect(x, y, w, 1, rgb); rect(x, y + h - 1, w, 1, rgb);
  rect(x, y, 1, h, rgb); rect(x + w - 1, y, 1, h, rgb);
}
static void spr(Spr s, float x, float y) {
  SDL_FRect d = { SDL_floorf(x), SDL_floorf(y), (float)s.w, (float)s.h };
  SDL_RenderTexture(ren, s.t, NULL, &d);
}
// Draw only the top `rows` rows of a sprite (used for zombies rising from the ground).
static void spr_rows(Spr s, float x, float y, int rows) {
  if (rows <= 0) return;
  if (rows > s.h) rows = s.h;
  SDL_FRect src = { 0, 0, (float)s.w, (float)rows };
  SDL_FRect d = { SDL_floorf(x), SDL_floorf(y), (float)s.w, (float)rows };
  SDL_RenderTexture(ren, s.t, &src, &d);
}

// ---- font: the classic 5x7 column-encoded LCD glyphs, uppercase only ---------
static const Uint8 font5x7[128][5] = {
  ['!'] = {0x00,0x00,0x5F,0x00,0x00}, ['\''] = {0x00,0x05,0x03,0x00,0x00},
  ['%'] = {0x23,0x13,0x08,0x64,0x62}, ['('] = {0x00,0x1C,0x22,0x41,0x00},
  [')'] = {0x00,0x41,0x22,0x1C,0x00}, ['*'] = {0x14,0x08,0x3E,0x08,0x14},
  ['+'] = {0x08,0x08,0x3E,0x08,0x08}, [','] = {0x00,0x50,0x30,0x00,0x00},
  ['-'] = {0x08,0x08,0x08,0x08,0x08}, ['.'] = {0x00,0x60,0x60,0x00,0x00},
  ['/'] = {0x20,0x10,0x08,0x04,0x02}, [':'] = {0x00,0x36,0x36,0x00,0x00},
  [';'] = {0x00,0x56,0x36,0x00,0x00}, ['<'] = {0x08,0x14,0x22,0x41,0x00},
  ['='] = {0x14,0x14,0x14,0x14,0x14}, ['>'] = {0x00,0x41,0x22,0x14,0x08},
  ['?'] = {0x02,0x01,0x51,0x09,0x06},
  // Maths symbols borrowed onto spare characters: ^ power, $ square root,
  // # divide, @ multiply.
  ['^'] = {0x04,0x02,0x01,0x02,0x04}, ['$'] = {0x10,0x20,0x7F,0x01,0x01},
  ['#'] = {0x08,0x08,0x2A,0x08,0x08}, ['@'] = {0x22,0x14,0x08,0x14,0x22},
  ['0'] = {0x3E,0x51,0x49,0x45,0x3E}, ['1'] = {0x00,0x42,0x7F,0x40,0x00},
  ['2'] = {0x42,0x61,0x51,0x49,0x46}, ['3'] = {0x21,0x41,0x45,0x4B,0x31},
  ['4'] = {0x18,0x14,0x12,0x7F,0x10}, ['5'] = {0x27,0x45,0x45,0x45,0x39},
  ['6'] = {0x3C,0x4A,0x49,0x49,0x30}, ['7'] = {0x01,0x71,0x09,0x05,0x03},
  ['8'] = {0x36,0x49,0x49,0x49,0x36}, ['9'] = {0x06,0x49,0x49,0x29,0x1E},
  ['A'] = {0x7E,0x11,0x11,0x11,0x7E}, ['B'] = {0x7F,0x49,0x49,0x49,0x36},
  ['C'] = {0x3E,0x41,0x41,0x41,0x22}, ['D'] = {0x7F,0x41,0x41,0x22,0x1C},
  ['E'] = {0x7F,0x49,0x49,0x49,0x41}, ['F'] = {0x7F,0x09,0x09,0x09,0x01},
  ['G'] = {0x3E,0x41,0x49,0x49,0x7A}, ['H'] = {0x7F,0x08,0x08,0x08,0x7F},
  ['I'] = {0x00,0x41,0x7F,0x41,0x00}, ['J'] = {0x20,0x40,0x41,0x3F,0x01},
  ['K'] = {0x7F,0x08,0x14,0x22,0x41}, ['L'] = {0x7F,0x40,0x40,0x40,0x40},
  ['M'] = {0x7F,0x02,0x0C,0x02,0x7F}, ['N'] = {0x7F,0x04,0x08,0x10,0x7F},
  ['O'] = {0x3E,0x41,0x41,0x41,0x3E}, ['P'] = {0x7F,0x09,0x09,0x09,0x06},
  ['Q'] = {0x3E,0x41,0x51,0x21,0x5E}, ['R'] = {0x7F,0x09,0x19,0x29,0x46},
  ['S'] = {0x46,0x49,0x49,0x49,0x31}, ['T'] = {0x01,0x01,0x7F,0x01,0x01},
  ['U'] = {0x3F,0x40,0x40,0x40,0x3F}, ['V'] = {0x1F,0x20,0x40,0x20,0x1F},
  ['W'] = {0x3F,0x40,0x38,0x40,0x3F}, ['X'] = {0x63,0x14,0x08,0x14,0x63},
  ['Y'] = {0x07,0x08,0x70,0x08,0x07}, ['Z'] = {0x61,0x51,0x49,0x45,0x43},
};
#define FONT_W 6
static SDL_Texture *font_tex;

static void font_init(void) {
  const int W = 128 * FONT_W, H = 8;
  Uint8 *px = SDL_calloc((size_t)W * H, 4);
  for (int c = 0; c < 128; c++)
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 7; row++)
        if (font5x7[c][col] & (1 << row)) {
          Uint8 *o = px + 4 * (row * W + c * FONT_W + col);
          o[0] = o[1] = o[2] = o[3] = 255;
        }
  font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
  SDL_UpdateTexture(font_tex, NULL, px, W * 4);
  SDL_SetTextureScaleMode(font_tex, SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(font_tex, SDL_BLENDMODE_BLEND);
  SDL_free(px);
}
static int text_w(const char *s, int scale) { return (int)strlen(s) * FONT_W * scale - scale; }
// Draws at most `n` characters (n < 0 draws the whole string). Returns the x after the text.
static float text_n(float x, float y, const char *s, int n, Uint32 rgb, int scale) {
  SDL_SetTextureColorMod(font_tex, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
  for (int i = 0; s[i] && (n < 0 || i < n); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 128 && c != ' ') {
      SDL_FRect src = { (float)(c * FONT_W), 0, 5, 7 };
      SDL_FRect dst = { x, y, 5.0f * scale, 7.0f * scale };
      SDL_RenderTexture(ren, font_tex, &src, &dst);
    }
    x += FONT_W * scale;
  }
  return x;
}
static void text(float x, float y, const char *s, Uint32 rgb, int scale) { text_n(x, y, s, -1, rgb, scale); }
static void text_shadow(float x, float y, const char *s, Uint32 rgb, int scale) {
  text(x + scale, y + scale, s, C_BLACK, scale);
  text(x, y, s, rgb, scale);
}
static void text_center(float cx, float y, const char *s, Uint32 rgb, int scale) {
  text_shadow(SDL_floorf(cx - text_w(s, scale) / 2.0f), y, s, rgb, scale);
}

#endif
