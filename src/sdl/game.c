// Home Row Horde: a typing defence game written in C on SDL3.
// Zombies shamble out of the forest toward your compound; type what they carry
// to shoot them. Ranks climb from single home-row letters to keeping a moving
// average words-per-minute. Now and then a boss monster arrives and the game
// switches to an arrow-key rhythm battle on the beat of the music.
//
// Native build:  npm run native     Web build (Emscripten):  npm run wasm
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "gfx.h"
#include "audio.h"
#include "drivers.h"

// ---- tuning ------------------------------------------------------------------
#define LANES 4
#define LANE_Y(l) (120 + (l) * 24)   // y of a walker's feet in lane l
#define WALL_X 44
#define WALL_STOP 64                 // enemies stop here and attack the wall
#define MAX_HP 10
#define MAX_EN 12
#define WPM_WINDOW 8.0f              // seconds in the moving average
#define BOSS_MIN_GAP 25.0f           // minimum seconds of typing between boss battles
#define BOSS_RANDOM_GAP 20.0f        // plus up to this many more, chosen at random
#define RECEPTOR_Y 46                 // arrows rise to the receptors at the top, as in StepMania

typedef struct {
  const char *name, *blurb;
  int quota;          // zombies to defeat
  float speed;        // pixels per second
  float interval;     // seconds between spawns
  int max_alive;
  int wpm;            // moving-average WPM to hold, 0 for none
  int chart;          // dance difficulty 0..3
} Rank;
static const Rank ranks_type[] = {
  { "SPROUT",  "F AND J, THEN HOME ROW", 12,  6, 4.0f, 2,  0, 0 },
  { "SCOUT",   "HOME ROW AND TOP ROW",   16,  8, 3.2f, 3,  0, 0 },
  { "SQUIRE",  "EVERY LETTER",           20, 10, 2.6f, 3,  0, 1 },
  { "ARCHER",  "SHORT WORDS",            16,  9, 3.2f, 3,  0, 1 },
  { "KNIGHT",  "LONGER WORDS, TOUGH BRUTES",  18, 10, 3.0f, 4,  0, 2 },
  { "CAPTAIN", "TWO-WORD FOES. KEEP 15 WPM",  20, 11, 2.6f, 4, 15, 2 },
  { "WARDEN",  "PHRASES. KEEP 25 WPM",        24, 11, 2.2f, 5, 25, 3 },
  { "LEGEND",  "LONG PHRASES. KEEP 40 WPM",   30, 11, 1.8f, 5, 40, 3 },
};
// Math mode: answer the sum a monster carries. Every answer is a whole number,
// zero or more, so only the digit keys are ever needed.
static const Rank ranks_math[] = {
  { "COUNTER",   "ADD SMALL NUMBERS",          12, 6, 4.0f, 2, 0, 0 },
  { "ADDER",     "ADD AND SUBTRACT TO 20",     14, 7, 3.6f, 3, 0, 0 },
  { "DOUBLER",   "EASY TIMES TABLES",          16, 7, 3.4f, 3, 0, 1 },
  { "DIVIDER",   "MULTIPLY AND DIVIDE",        16, 7, 3.4f, 3, 0, 1 },
  { "JUGGLER",   "TWO-DIGIT SUMS, TWO STEPS",  16, 6, 3.8f, 3, 0, 2 },
  { "ROOTER",    "SQUARES AND SQUARE ROOTS",   16, 6, 3.8f, 3, 0, 2 },
  { "POWERHOUSE", "EXPONENTS AND CUBE ROOTS",  18, 6, 3.6f, 4, 0, 3 },
  { "LOG LORD",  "LOGARITHMS",                 20, 6, 3.4f, 4, 0, 3 },
};
// Driver's test mode: multiple-choice questions, one at a time. The quota is the
// number of questions; the wall's hearts are the misses allowed plus one, so
// the full test fails on the seventh miss exactly as the real one does.
static const Rank ranks_drive[] = {
  { "QUICK QUIZ", "10 QUESTIONS, 2 MISSES ALLOWED", 10, 7, 0, 4, 0, 1 },
  { "FULL TEST",  "40 QUESTIONS, 6 MISSES ALLOWED", 40, 7, 0, 4, 0, 1 },
};
enum { MODE_TYPE, MODE_MATH, MODE_DRIVE, NMODES };
static const Rank *const mode_ranks[NMODES] = { ranks_type, ranks_math, ranks_drive };
static const int mode_nranks[NMODES] = { (int)SDL_arraysize(ranks_type), (int)SDL_arraysize(ranks_math), (int)SDL_arraysize(ranks_drive) };
static const char *const mode_names[NMODES] = { "TYPING", "MATH", "DRIVER'S TEST" };

static const char *const words3[] = { "AX","ASH","BAT","BOO","BOG","CAT","COB","CRY","DIG","DIM","EEK","ELF",
  "EYE","FOG","FUR","GAS","HAG","HEX","IMP","JAW","JAR","KEY","LID","MUD","NET","OAK","OWL","PIT","RAT",
  "RIP","ROT","SAD","SKY","TOE","URN","VAT","WEB","YEW","ZAP","ASK","LAD","DAD","FAD","QUA" };
static const char *const words5[] = { "AXES","BONE","BOLT","CAVE","CLAW","COLD","CROW","DARK","DOOM","DUSK",
  "EERIE","FANG","FEAR","FIRE","GATE","GHOST","GLOOM","GRAVE","HOWL","HAUNT","IVORY","JINX","KNOT","LAMP",
  "MIST","MOON","MOSS","NIGHT","OGRE","PYRE","QUAKE","RAVEN","SKULL","SLIME","SPELL","TOMB","TORCH","TREE",
  "URNS","VINE","WALL","WITCH","WOLF","YELL","ZEAL" };
static const char *const words7[] = { "ARROW","BRAMBLE","BROOM","CANDLE","CASTLE","COFFIN","CREAK","CRYPT",
  "DREAD","ECHOES","FLAME","FOREST","GHOUL","GOBLIN","HEDGE","INFERNO","JAGGED","KNIGHT","LANTERN","LIZARD",
  "MONSTER","MUMMY","NIGHTLY","OMENS","POTION","PUMPKIN","QUIVER","RATTLE","SCREAM","SHADOW","SHIVER",
  "SPIDER","SPIRIT","THORN","TREMBLE","UNDEAD","VAMPIRE","WIZARD","YONDER","ZOMBIE" };
static const char *const words10[] = { "APPARITION","BANSHEE","BARRICADE","CAULDRON","CEMETERY","COMPOUND",
  "CREATURE","DARKNESS","ECLIPSE","FRIGHTFUL","GARGOYLE","GRAVEYARD","HAUNTING","INCANTATION","JACKDAWS",
  "KNIGHTHOOD","LANTERNS","MAUSOLEUM","MIDNIGHT","MOONLIGHT","NECROMANCER","NIGHTMARE","OUTBREAK","PALISADE",
  "PHANTOM","QUARANTINE","RAMPARTS","SCARECROW","SKELETON","SURVIVOR","TOMBSTONE","TWILIGHT","UNDERTAKER",
  "VENGEANCE","WEREWOLF","WITCHCRAFT","YEARNING","ZOMBIFIED" };
// Tougher monsters carry a phrase: one hitpoint per word, with a space between.
static const char *const phrases2[] = { "BLACK CAT","BONE DUST","COLD NIGHT","CREAKY DOOR","DARK WOODS","DEAD LEAVES",
  "EVIL GRIN","FOGGY PATH","FULL MOON","GRIM REAPER","HOLD FAST","IRON GATE","JAGGED ROCK","KEEP WATCH","LAST LIGHT",
  "LOST SOUL","MOSSY CRYPT","NIGHT WATCH","OPEN GRAVE","PALE GHOST","QUIET TOMB","RED EYES","STONE WALL","TALL PINES",
  "UNDEAD ARMY","VILE CURSE","WILD HOWL","YELLOW FANGS","ZOMBIE HORDE" };
static const char *const phrases3[] = { "AIM FOR THE HEAD","BAR THE GATE","CLOSE THE SHUTTERS","DO NOT LOOK BACK",
  "EYES ON THE WOODS","FEAR THE FULL MOON","GUARD THE YARD","HOLD THE WALL","JUMP THE FENCE","KEEP THE FIRE LIT",
  "LIGHT THE TORCH","MIND THE GRAVES","NIGHT IS NOT OVER","ONE MORE WAVE","PRAY FOR THE DAWN","QUIET AS A GRAVE",
  "RUN TO THE TOWER","SAVE THE VILLAGE","THE DEAD RISE","UNDER A BLOOD MOON","VOICES IN THE DARK","WATCH THE TREE LINE",
  "WOLVES IN THE MIST","STAY ON HOME ROW","THEY NEVER SLEEP","ZOMBIES AT THE GATE" };
// Each word of a phrase, and the armour pip it breaks, gets its own colour.
static const Uint32 word_colors[4] = { C_SKIN, C_CYAN, C_PINK, C_ZOMBIE };
typedef struct { const char *const *w; int n; } Pool;
#define POOL(a) ((Pool){ a, (int)SDL_arraysize(a) })

static const char *const kb_rows[3] = { "QWERTYUIOP", "ASDFGHJKL;", "ZXCVBNM" };
static const char *const fingers[10] = { "PINKY", "RING", "MIDDLE", "POINTER", "POINTER",
                                         "POINTER", "POINTER", "MIDDLE", "RING", "PINKY" };

// ---- state -------------------------------------------------------------------
enum { ST_TITLE, ST_PLAY, ST_RANKUP, ST_GAMEOVER, ST_VICTORY, ST_CALIBRATE, ST_SHOP, ST_SCORES };
enum { SUB_TYPING, SUB_BOSS, SUB_BOSS_OUT };
enum { K_ZOMBIE, K_BAT, K_BRUTE };

typedef struct {
  bool alive, doomed;
  int kind, lane, variant, len, typed;
  int hp, words;       // words still to type, and words in total
  float x, rise, anim, atk, dying, hurt;
  char word[28];
  char q[24];          // math mode: the question shown; `word` is its answer
} Enemy;
typedef struct { bool on; float x, y; int target; bool at_boss, wound; } Bolt;
typedef struct { float x, y, vx, vy, life; Uint32 color; } Particle;
static int last_boss = -1;
static int autosync;
static void offset_set(int ms);
typedef struct { int step, col, state; } Note;   // state: 0 pending, 1 hit, 2 missed

static struct {
  int state, sub, rank, sel, mode;
  int hp_max;
  // driver's test: the question order, the one being asked, and how its answers are shuffled
  Uint8 q_order[64], q_perm[4]; int q_num, q_n, q_pick, q_right; bool q_active; float q_reveal;
  bool paused;
  int hp, kills, spawned, score, streak, correct, wrong, target, last_lane;
  int weapon;                      // weapon level, bought in the shop and kept
  int cash, potions;               // coins from fallen monsters; wall potions in the pack
  bool frost, frost_next;          // a frost charm slows this rank / is bought for the next one
  int shop_sel, shop_next;         // the shop's cursor, and the rank that starts on leaving
  bool entering, quit;             // typing a name for the high-score board; the run was ended by choice
  bool name_kept;                  // the name shown is last time's; the first key typed replaces it
  char name[10]; int score_pos;    // the name being typed, and where the new score landed
  float time, spawn_t, tclock, rank_clock, ward, fear_t, shake, wall_flash, state_t;
  float archer_y, shoot_t;
  float key_times[256]; int key_head;
  float wpm, best_wpm;
  float boss_timer;                 // typing seconds until the next boss battle
  Enemy en[MAX_EN];
  Bolt bolts[8];
  Particle parts[96];
  char msg[40]; float msg_t; Uint32 msg_color;
  // boss battle
  Note notes[DANCE_STEPS]; int nnotes, boss_hp, boss_max, hits, misses, combo;
  int sync_offer;   // after a battle: the average timing in ms that D will fold into the sound delay
  float sync_sum, sync_sq; int sync_n;   // signed timing of this battle's hits, for the summary and AutoSync
  float boss_x, boss_flash, press_flash[4], hit_flash[4], out_t;
  Uint32 hit_color[4];
  char judge_sub[16];
  bool boss_won;
  int boss_kind;
  char judge[12]; float judge_t; Uint32 judge_color;
} G;
#define ranks (mode_ranks[G.mode])
#define NRANKS (mode_nranks[G.mode])

static SDL_Window *win;
static SDL_Texture *screen_tex, *bg_tex;
static Spr s_merchant, s_potion, s_frost, s_coin;
static Spr s_zombie[3][2], s_bat[2], s_brute, s_lich, s_archer, s_bolt[7], s_tomb, s_heart, s_note;

// The archer's weapon has seven levels, bought in order from the shop between
// ranks with the coins that monsters drop. A weapon, once bought, is kept.
// From level 2 the impact knocks back monsters near the one hit; from level 3
// it destroys the closest ones, and the reach grows with every level.
typedef struct {
  const char *name;
  int price;         // coins to buy this level, having the one below
  float speed;       // bolt speed, pixels per second
  int blast, lanes;  // monsters within this many pixels and lanes of the hit are destroyed
  int push;          // monsters a little further out are knocked back this far
  Uint32 trail;
} Weapon;
static const Weapon weapons[] = {
  { "OAK ARROWS",      0,  520,   0, 0,  0, 0 },
  { "FIRE ARROWS",    50,  560,   0, 1, 10, C_SKIN },
  { "SILVER BOLTS",  120,  800,  24, 1, 12, C_WHITE },
  { "THUNDER BOLTS", 220,  900,  34, 1, 14, C_YELLOW },
  { "STORM STAFF",   360, 1200,  60, 2, 16, C_CYAN },
  { "DRAGON FLAME",  550, 1400,  80, 2, 18, C_RED },
  { "HOLY NOVA",     800, 1800, 150, 3, 20, C_PINK },
};
#define POTION_PRICE 120
#define POTION_MAX 3
#define FROST_PRICE 80
#define START_COINS_PER_RANK 120   // pocket money when a game starts above rank 1
#define NWEAPONS ((int)SDL_arraysize(weapons))
static int weapon_now(void) { return G.weapon; }

static void say(const char *m, Uint32 color, float secs);
static void weapon_set(int level) {
  char buf[40];
  G.weapon = level;
  SDL_snprintf(buf, sizeof(buf), "%s!", weapons[level].name);
  say(buf, C_YELLOW, 1.4f);
  for (int k = 0; k < 3; k++) sfx(WAVE_SQUARE, midi_hz(76 + k * 5), 0, 0.09f, 0.10f, k * 0.07f);
}

// Boss monsters for the W A S D battles. Each one plays a little differently.
typedef struct {
  const char *name, *hint;
  Spr *frames[2];
  int scale;
  bool hover;
  float note_speed;   // pixels per second
  int need_pct;       // share of the notes that must be hit to win
  int reveal_y;       // notes stay hidden below this line
  Uint32 color;
} Boss;
static const Boss bosses[] = {
  { "PUMPKIN KING", "HIT THE ARROWS ON THE BEAT", { &s_brute, &s_brute },               2, false,  90, 60, 196, C_SKIN },
  { "BAT QUEEN",    "HER ARROWS FLY FAST",        { &s_bat[0], &s_bat[1] },             3, true,  125, 55, 196, C_PURPLE },
  { "ROTTEN GIANT", "TOUGH HIDE: MISS VERY FEW",  { &s_zombie[2][0], &s_zombie[2][1] }, 3, false,  80, 70, 196, C_ZOMBIE },
  { "THE LICH",     "HIS ARROWS APPEAR LATE",     { &s_lich, &s_lich },                 2, false, 100, 60, 118, C_CYAN },
};
#define NBOSSES ((int)SDL_arraysize(bosses))
static int force_boss = -1;
static Uint64 last_ns;
static Uint32 rng_state = 0xC0FFEE;

// Headless test hooks (native only): HRH_SHOT=file.bmp HRH_FRAMES=n HRH_RANK=1..8
// HRH_BOT=keys-per-second HRH_BOSS=1
static struct { const char *shot; int frames, frame; float bot, bot_t; bool on; } T;

static Uint32 rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
// Scramble a seed thoroughly first: the generator on its own leaves nearby
// seeds (two launches a moment apart) producing nearly the same sequence.
static void seed_rng(Uint32 x) {
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  rng_state = x ? x : 0xC0FFEE;
  for (int i = 0; i < 16; i++) rnd();
}
static int rndi(int n) { return (int)(rnd() % (Uint32)n); }
static float rndf(void) { return (float)(rnd() & 0xFFFF) / 65536.0f; }

static void set_status(const char *s) {
  static char last[96];
  if (!strcmp(last, s)) return;
  SDL_strlcpy(last, s, sizeof(last));
#ifdef __EMSCRIPTEN__
  EM_ASM({ var e = document.getElementById('score'); if (e) e.textContent = UTF8ToString($0); }, s);
#endif
}

static void say(const char *m, Uint32 color, float secs) {
  SDL_strlcpy(G.msg, m, sizeof(G.msg)); G.msg_t = secs; G.msg_color = color;
}

static void puff(float x, float y, Uint32 color, int n) {
  for (int i = 0; i < (int)SDL_arraysize(G.parts) && n > 0; i++)
    if (G.parts[i].life <= 0) {
      G.parts[i] = (Particle){ x, y, (rndf() - 0.5f) * 80, -rndf() * 70, 0.3f + rndf() * 0.4f, color };
      n--;
    }
}

// ---- enemies -----------------------------------------------------------------
static float enemy_y(const Enemy *e) {   // vertical centre, for bolts and effects
  if (e->kind == K_BAT) return LANE_Y(e->lane) - 30 + sinf(e->anim * 5) * 3;
  return LANE_Y(e->lane) - (e->kind == K_BRUTE ? 15 : 12);
}

static bool first_letter_used(char c) {
  for (int i = 0; i < MAX_EN; i++)
    if (G.en[i].alive && !G.en[i].doomed && G.en[i].word[0] == c) return true;
  return false;
}

// A math question whose answer is a non-negative whole number. Bats ask one
// from the level below and brutes one from the level above.
static void math_question(Enemy *e) {
  int lvl = SDL_clamp(G.rank + (e->kind == K_BRUTE) - (e->kind == K_BAT), 0, 7);
  for (int tries = 0; tries < 30; tries++) {
    int a = 0, b = 0, c = 0, ans = 0, k = rndi(60);
    char q[24] = "";
    switch (lvl) {
      case 0: a = 1 + rndi(5); b = 1 + rndi(4); if (G.spawned == 0) a = b = 1;   // the very first one is 1+1
              ans = a + b; SDL_snprintf(q, sizeof(q), "%d+%d", a, b); break;
      case 1: if (k % 2) { a = 2 + rndi(9); b = 2 + rndi(9); ans = a + b; SDL_snprintf(q, sizeof(q), "%d+%d", a, b); }
              else { a = 5 + rndi(16); b = 1 + rndi(a - 1); ans = a - b; SDL_snprintf(q, sizeof(q), "%d-%d", a, b); }
              break;
      case 2: if (k % 3) { a = 2 + rndi(4); b = 1 + rndi(10); ans = a * b; SDL_snprintf(q, sizeof(q), "%d@%d", a, b); }
              else { a = 10 + rndi(40); b = 1 + rndi(9); ans = a + b; SDL_snprintf(q, sizeof(q), "%d+%d", a, b); }
              break;
      case 3: if (k % 2) { a = 2 + rndi(11); b = 2 + rndi(11); ans = a * b; SDL_snprintf(q, sizeof(q), "%d@%d", a, b); }
              else { b = 2 + rndi(11); ans = 2 + rndi(11); SDL_snprintf(q, sizeof(q), "%d#%d", b * ans, b); }
              break;
      case 4: if (k % 3 == 0) { a = 10 + rndi(90); b = 10 + rndi(90); ans = a + b; SDL_snprintf(q, sizeof(q), "%d+%d", a, b); }
              else if (k % 3 == 1) { a = 20 + rndi(80); b = 10 + rndi(a - 10); ans = a - b; SDL_snprintf(q, sizeof(q), "%d-%d", a, b); }
              else { a = 2 + rndi(8); b = 2 + rndi(8); c = 1 + rndi(9); ans = a * b + c; SDL_snprintf(q, sizeof(q), "%d@%d+%d", a, b, c); }
              break;
      case 5: if (k % 2) { a = 2 + rndi(14); ans = a * a; SDL_snprintf(q, sizeof(q), "%d^2", a); }
              else { ans = 2 + rndi(14); SDL_snprintf(q, sizeof(q), "$%d", ans * ans); }
              break;
      case 6: if (k % 4 == 0) { b = 2 + rndi(9); ans = 1 << b; SDL_snprintf(q, sizeof(q), "2^%d", b); }
              else if (k % 4 == 1) { a = 3 + rndi(3); b = 2 + rndi(3); ans = 1; for (c = 0; c < b; c++) ans *= a; SDL_snprintf(q, sizeof(q), "%d^%d", a, b); }
              else if (k % 4 == 2) { b = 2 + rndi(5); ans = 1; for (c = 0; c < b; c++) ans *= 10; SDL_snprintf(q, sizeof(q), "10^%d", b); }
              else { ans = 2 + rndi(9); SDL_snprintf(q, sizeof(q), "CBRT(%d)", ans * ans * ans); }
              break;
      default: {
        static const int bases[] = { 2, 2, 10, 3, 4, 5 }, top[] = { 10, 10, 6, 5, 5, 4 };
        int i = k % 6, v = 1;
        ans = 1 + rndi(top[i]);
        for (c = 0; c < ans; c++) v *= bases[i];
        SDL_snprintf(q, sizeof(q), "LOG%d(%d)", bases[i], v);
      }
    }
    bool clash = false;   // not the same question twice on the field
    for (int i = 0; i < MAX_EN; i++) if (G.en[i].alive && &G.en[i] != e && !strcmp(G.en[i].q, q)) clash = true;
    SDL_strlcpy(e->q, q, sizeof(e->q));
    SDL_snprintf(e->word, sizeof(e->word), "%d", ans);
    if (!clash) break;
  }
  e->len = (int)strlen(e->word); e->words = e->hp = 1;
}

static void pick_word(Enemy *e) {
  int r = G.rank;
  if (G.mode == MODE_MATH) { math_question(e); return; }
  if (r <= 2 || (r == 3 && e->kind == K_BAT)) {
    const char *set = r == 0 ? (G.spawned < 6 ? "FJDK" : "ASDFJKL")
                    : r == 1 ? "ASDFGHJKLQWERTYUIOP" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char c = set[rndi((int)strlen(set))];
    if (r == 0 && G.spawned < 4) c = "FJFJ"[G.spawned];   // a fixed, gentle opening
    else for (int t = 0; t < 20 && first_letter_used(c); t++) c = set[rndi((int)strlen(set))];
    e->word[0] = c; e->word[1] = 0;
  } else {
    Pool p;
    int roll = rndi(100);
    int phrase = 0;   // 0 for a single word, else which phrase list
    if (e->kind == K_BRUTE) phrase = r >= 6 ? 3 : 2;
    else if (e->kind == K_ZOMBIE) phrase = r == 5 ? (roll < 25 ? 2 : 0) : r == 6 ? (roll < 40 ? 2 : 0)
                                         : r == 7 ? (roll < 35 ? 2 : roll < 60 ? 3 : 0) : 0;
    if (phrase) p = phrase == 3 ? POOL(phrases3) : POOL(phrases2);
    else if (e->kind == K_BAT) p = r <= 5 ? POOL(words3) : POOL(words5);
    else if (e->kind == K_BRUTE) p = r <= 5 ? POOL(words7) : POOL(words10);
    else if (r == 3) p = POOL(words3);
    else if (r == 4) p = POOL(words5);
    else if (r == 5) p = rndi(2) ? POOL(words5) : POOL(words7);
    else if (r == 6) p = POOL(words7);
    else p = rndi(2) ? POOL(words7) : POOL(words10);
    const char *w = p.w[rndi(p.n)];
    for (int t = 0; t < 20 && first_letter_used(w[0]); t++) w = p.w[rndi(p.n)];
    SDL_strlcpy(e->word, w, sizeof(e->word));
  }
  e->len = (int)strlen(e->word);
  e->words = 1;
  for (int i = 0; i < e->len; i++) if (e->word[i] == ' ') e->words++;
  e->hp = e->words;
}

static int count_alive(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++) if (G.en[i].alive && !G.en[i].doomed) n++;
  return n;
}

static void spawn(void) {
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &G.en[i];
    if (e->alive) continue;
    *e = (Enemy){ .alive = true };
    int roll = rndi(100);
    e->kind = (G.rank >= 2 && roll < 15) ? K_BAT : (G.rank >= 4 && roll < 30) ? K_BRUTE : K_ZOMBIE;
    do e->lane = rndi(LANES); while (e->lane == G.last_lane);
    G.last_lane = e->lane;
    e->variant = rndi(3);
    e->x = e->kind == K_BAT ? 330 : 284 + rndi(24);
    e->rise = e->kind == K_BAT ? 1 : 0;
    pick_word(e);
    G.spawned++;
    return;
  }
}

static void kill_enemy(Enemy *e) {
  if (!e->alive || e->dying > 0) return;
  e->doomed = true; e->dying = 0.3f;
  if (G.mode != MODE_DRIVE) {   // in the driver's test, questions are counted, not monsters
    G.kills++;
    G.score += G.mode == MODE_MATH ? 50 + 10 * (int)strlen(e->q) : 10 * e->len;
    G.cash += (e->kind == K_BRUTE ? 15 : e->kind == K_BAT ? 4 : 6) + 3 * (e->words - 1);   // coins for the shop
  }
  puff(e->x, enemy_y(e), e->kind == K_BAT ? C_PURPLE : e->kind == K_BRUTE ? C_SKIN : C_ZOMBIE, 10);
  sfx(WAVE_NOISE, 0, 0, 0.12f, 0.20f, 0);
  sfx(WAVE_SQUARE, 330, -900, 0.14f, 0.10f, 0);
  if (G.target >= 0 && &G.en[G.target] == e) G.target = -1;
}

static void hurt_wall(int dmg, bool lethal) {
  G.hp -= dmg;
  if (!lethal && G.hp < 1) G.hp = 1;
  G.shake = 0.3f; G.wall_flash = 0.25f;
  sfx(WAVE_NOISE, 0, 0, 0.22f, 0.30f, 0);
  sfx(WAVE_TRI, 90, -120, 0.25f, 0.5f, 0);
}

static float boss_cy(void) {   // vertical centre of the current boss
  const Boss *b = &bosses[G.boss_kind];
  if (b->hover) return 140 + sinf(G.time * 3) * 6;
  return 190 - b->frames[0]->h * b->scale / 2.0f;
}

static void spark(float x, float y, Uint32 color) {
  for (int i = 0; i < (int)SDL_arraysize(G.parts); i++)
    if (G.parts[i].life <= 0) {
      G.parts[i] = (Particle){ x, y, (rndf() - 0.5f) * 14, (rndf() - 0.5f) * 14 - 8, 0.15f + rndf() * 0.2f, color };
      return;
    }
}

// The impact of an upgraded weapon: monsters close to the one hit are destroyed
// outright, whatever armour they have left, and those a little further out are
// knocked back.
static void kill_enemy(Enemy *e);
static void splash(const Enemy *hit) {
  const Weapon *w = &weapons[weapon_now()];
  if (!w->blast && !w->push) return;
  float reach = w->blast ? w->blast * 1.6f : 36;
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *o = &G.en[i];
    if (!o->alive || o->dying > 0 || o == hit || o->rise < 1) continue;
    float dx = SDL_fabsf(o->x - hit->x);
    if (abs(o->lane - hit->lane) > w->lanes || dx > reach) continue;
    if (dx <= w->blast) { o->doomed = true; kill_enemy(o); }
    else if (!o->doomed) { o->x = SDL_min(o->x + w->push, 300.0f); puff(o->x, enemy_y(o), w->trail, 2); }
  }
  if (w->blast) {   // a burst sized to the blast
    for (int k = 0; k < w->blast / 3; k++) {
      float a = rndf() * 6.2832f, r = rndf() * w->blast;
      puff(hit->x + cosf(a) * r, enemy_y(hit) + sinf(a) * r * 0.4f, w->trail, 1);
    }
    if (w->blast >= 100) G.shake = 0.15f;
  }
}

// A shot that breaks one word of armour: it staggers the monster but does not kill.
static void wound_enemy(Enemy *e) {
  if (!e->alive || e->dying > 0) return;
  e->hurt = 0.25f;
  e->x = SDL_min(e->x + 14, 300.0f);
  puff(e->x, enemy_y(e), C_WHITE, 5);
  sfx(WAVE_NOISE, 0, 0, 0.07f, 0.16f, 0);
  sfx(WAVE_SQUARE, 440, -600, 0.08f, 0.09f, 0);
}

static void fire_at(int target, bool at_boss, bool wound) {
  for (int i = 0; i < (int)SDL_arraysize(G.bolts); i++)
    if (!G.bolts[i].on) {
      G.bolts[i] = (Bolt){ true, 40, G.archer_y - 13, target, at_boss, wound };
      G.shoot_t = 0.15f;
      return;
    }
  // No free bolt: resolve the hit immediately so nothing is lost.
  if (!at_boss && target >= 0) { if (wound) wound_enemy(&G.en[target]); else kill_enemy(&G.en[target]); }
}
static void fire(float tx, float ty, int target, bool at_boss) { (void)tx; (void)ty; fire_at(target, at_boss, false); }

// ---- typing --------------------------------------------------------------------
static int nearest_enemy(void) {
  int best = -1;
  for (int i = 0; i < MAX_EN; i++)
    if (G.en[i].alive && !G.en[i].doomed && (best < 0 || G.en[i].x < G.en[best].x)) best = i;
  return best;
}
// The character the player should press next, or 0 when nothing is on the field.
static char next_char(void) {
  int i = G.target >= 0 ? G.target : nearest_enemy();
  return i < 0 ? 0 : G.en[i].word[G.en[i].typed];
}

static void quiz_resolve(int choice);

static void type_char(char c) {
  Enemy *e = NULL;
  if (G.mode == MODE_DRIVE) {   // a letter picks an answer
    if (G.q_active && c >= 'A' && c < 'A' + G.q_n) quiz_resolve(c - 'A');
    return;
  }
  // Two answers can start the same way (12 and 15). If the next key does not
  // fit the locked monster but does continue another one's answer, move the lock.
  if (G.target >= 0 && G.en[G.target].alive && !G.en[G.target].doomed && G.en[G.target].word[G.en[G.target].typed] != c) {
    Enemy *old = &G.en[G.target];
    for (int i = 0; i < MAX_EN; i++) {
      Enemy *o = &G.en[i];
      if (!o->alive || o->doomed || o == old || o->typed || o->words > 1 || old->words > 1) continue;
      if (!strncmp(o->word, old->word, (size_t)old->typed) && o->word[old->typed] == c) {
        o->typed = old->typed; old->typed = 0; G.target = i;
        break;
      }
    }
  }
  if (G.target >= 0 && G.en[G.target].alive && !G.en[G.target].doomed) e = &G.en[G.target];
  else {
    G.target = -1;
    for (int i = 0; i < MAX_EN; i++) {
      Enemy *o = &G.en[i];
      if (!o->alive || o->doomed) continue;
      // Coming back to a half-typed phrase, the next word's first letter is enough.
      bool match = o->word[o->typed] == c || (o->word[o->typed] == ' ' && o->word[o->typed + 1] == c);
      if (match && (G.target < 0 || o->x < G.en[G.target].x)) G.target = i;
    }
    if (G.target >= 0) { e = &G.en[G.target]; if (e->word[e->typed] == ' ' && c != ' ') e->typed++; }
  }
  if (!e || e->word[e->typed] != c) {
    if (c == ' ' || count_alive() == 0) return;   // a stray space, or nothing to type yet: not a mistake
    G.wrong++; G.streak = 0;
    sfx(WAVE_SQUARE, 110, 0, 0.09f, 0.12f, 0);
    return;
  }
  e->typed++;
  G.correct++; G.streak++;
  G.key_times[G.key_head++ & 255] = G.tclock;
  int mult = 1 + G.streak / 10; if (mult > 5) mult = 5;
  G.score += 10 * mult;
  if (e->x < 300) e->x += 1.5f;   // each hit staggers the walker a little
  sfx(WAVE_SQUARE, 660.0f + 40.0f * (float)(G.streak % 12), 0, 0.04f, 0.08f, 0);
  G.archer_y = (float)LANE_Y(e->lane);
  if (e->typed >= e->len) {
    e->doomed = true;
    fire(e->x, enemy_y(e), G.target, false);
    G.target = -1;
  } else if (e->word[e->typed] == ' ') {   // a word is done: break one piece of armour
    e->hp--;
    fire_at(G.target, false, true);
  }
}

// ---- boss battle (the W A S D rhythm mode) ----------------------------------------
// Columns run left, down, up, right: the arrow keys (W A S D also works).
static Uint64 key_ts;   // timestamp of the key press being handled, 0 when there is none

static void boss_build_chart(int level) {
  G.nnotes = 0;
  for (int bar = 0; bar < 8; bar++) {
    // Columns follow the melody: lower pitches to the left, higher to the right.
    int pitches[8], np = 0;
    for (int i = 0; i < 8; i++) {
      int p = dance_lead[bar * 8 + i], j;
      if (!p) continue;
      for (j = 0; j < np && pitches[j] != p; j++) {}
      if (j == np) pitches[np++] = p;
    }
    for (int a = 0; a < np; a++) for (int b = a + 1; b < np; b++)
      if (pitches[b] < pitches[a]) { int t = pitches[a]; pitches[a] = pitches[b]; pitches[b] = t; }
    for (int i = 0; i < 8; i++) {
      int p = dance_lead[bar * 8 + i];
      if (!p) continue;
      bool keep = level == 0 ? i % 4 == 0                       // two notes a bar
                : level == 1 ? i % 2 == 0                       // every beat
                : level == 2 ? (i % 2 == 0 || bar >= 3)         // beats, then the tune
                : true;                                          // every note
      if (!keep) continue;
      int col = 0;
      while (pitches[col] != p) col++;
      G.notes[G.nnotes++] = (Note){ bar * 8 + i, col > 3 ? 3 : col, 0 };
    }
  }
}
static double note_time(const Note *n) { return (DANCE_COUNT_IN + n->step) * DANCE_STEP_SEC; }

static void boss_start(void) {
  G.sub = SUB_BOSS; G.target = -1;
  boss_build_chart(ranks[G.rank].chart);
  do G.boss_kind = rndi(NBOSSES); while (G.boss_kind == last_boss);
  if (force_boss >= 0) G.boss_kind = force_boss % NBOSSES;
  last_boss = G.boss_kind;
  G.boss_max = G.boss_hp = (G.nnotes * bosses[G.boss_kind].need_pct + 99) / 100;
  G.hits = G.misses = G.combo = 0; G.sync_sum = G.sync_sq = 0; G.sync_n = 0; G.boss_x = 350; G.boss_won = false; G.judge_t = 0;
  audio_song(SONG_DANCE);
  sfx(WAVE_SQUARE, 110, 0, 0.5f, 0.12f, 0);
  sfx(WAVE_SQUARE, 104, 0, 0.5f, 0.12f, 0);
}

static void boss_end(bool won) {
  G.boss_won = won; G.sub = SUB_BOSS_OUT; G.out_t = 3.0f;
  audio_song(SONG_TYPING);
  if (won) {
    G.score += 500; G.cash += 100;
    puff(G.boss_x, boss_cy(), bosses[G.boss_kind].color, 30); puff(G.boss_x, boss_cy() - 20, C_YELLOW, 20);
    for (int i = 0; i < MAX_EN; i++) if (G.en[i].alive && !G.en[i].doomed) kill_enemy(&G.en[i]);
    if (G.mode != MODE_DRIVE) G.hp = G.hp + 2 > MAX_HP ? MAX_HP : G.hp + 2;   // test misses are not healed
    for (int i = 0; i < 4; i++) sfx(WAVE_SQUARE, midi_hz(69 + i * 4 + (i == 3 ? 0 : 0)), 0, 0.12f, 0.12f, i * 0.1f);
  } else {
    if (G.mode != MODE_DRIVE) hurt_wall(2, false);
    sfx(WAVE_SQUARE, 220, -150, 0.6f, 0.12f, 0);
  }
  G.boss_timer = BOSS_MIN_GAP + rndf() * BOSS_RANDOM_GAP;
  // Report how the presses sat against the beat. With AutoSync on, and only when
  // they were consistent, shift the sound delay toward them a little.
  if (G.sync_n >= 8 && T.bot <= 0) {
    float mean = G.sync_sum / G.sync_n, var = G.sync_sq / G.sync_n - mean * mean;
    float sd = var > 0 ? sqrtf(var) : 0;
    int ms = (int)lroundf(mean * 1000);
    char buf[40];
    if (autosync && G.sync_n >= 12 && sd < 0.035f && ms != 0) {
      offset_set(A.offset_ms + SDL_clamp(ms, -20, 20));
      SDL_snprintf(buf, sizeof(buf), "AUTOSYNC: DELAY %+d MS", A.offset_ms);
      say(buf, C_CYAN, 2.5f);
    } else {
      SDL_snprintf(buf, sizeof(buf), "AVERAGE %d MS %s", abs(ms), ms >= 0 ? "LATE" : "EARLY");
      say(buf, C_CYAN, ms ? 5.0f : 2.5f);
      // Offer to fold that into the sound delay, and hold the pause a little longer to allow it.
      if (ms) { G.sync_offer = ms; G.out_t = 5.0f; }
    }
  }
}

static double hit_window(void);
static void boss_judge(const char *label, Uint32 color) {
  SDL_strlcpy(G.judge, label, sizeof(G.judge)); G.judge_t = 0.5f; G.judge_color = color; G.judge_sub[0] = 0;
}

// Judgement windows are StepMania's defaults: 22.5, 45, 90 and 135 ms, with its
// widest 180 ms window kept only for the beginner ranks.
static double hit_window(void) { return ranks[G.rank].chart <= 1 && !(G.mode == MODE_TYPE && G.rank == 3) ? 0.180 : 0.135; }

static void boss_press(int col) {
  if (G.sub != SUB_BOSS) return;
  G.press_flash[col] = 0.12f;
  double t = audio_song_time_at(key_ts);
  double good = hit_window();
  for (int i = 0; i < G.nnotes; i++) {
    Note *n = &G.notes[i];
    if (n->state || n->col != col) continue;
    double off = t - note_time(n), d = fabs(off);   // positive: pressed late
    if (d > good) continue;
    n->state = 1; G.hits++; G.combo++;
    G.sync_sum += (float)off; G.sync_sq += (float)(off * off); G.sync_n++;
    int tier = d <= 0.0225 ? 0 : d <= 0.045 ? 1 : d <= 0.090 ? 2 : d <= 0.135 ? 3 : 4;
    static const char *const names[5] = { "FANTASTIC", "PERFECT", "GREAT", "GOOD", "OK" };
    static const Uint32 colors[5] = { C_WHITE, C_YELLOW, C_ZOMBIE, C_CYAN, C_PURPLE };
    G.score += 60 - tier * 10;
    boss_judge(names[tier], colors[tier]);
    if (tier) SDL_snprintf(G.judge_sub, sizeof(G.judge_sub), "%s %d MS", off > 0 ? "LATE" : "EARLY", (int)(d * 1000));
    G.hit_flash[col] = 0.2f; G.hit_color[col] = colors[tier];
    sfx(WAVE_SQUARE, 1320.0f - tier * 110.0f, 0, 0.05f, 0.07f, 0);
    if (G.boss_hp > 0) { G.boss_hp--; fire(G.boss_x, boss_cy(), -1, true); }
    return;
  }
}

static void boss_update(float dt) {
  double t = audio_song_time();
  if (G.boss_x > 274) { G.boss_x -= 40 * dt; if (G.boss_x < 274) G.boss_x = 274; }
  for (int i = 0; i < G.nnotes; i++) {
    Note *n = &G.notes[i];
    if (n->state == 0 && t > note_time(n) + (hit_window())) {
      n->state = 2; G.misses++; G.combo = 0;
      boss_judge("MISS", C_RED);
      if (G.mode != MODE_DRIVE && G.rank >= 3 && G.misses % 8 == 0) hurt_wall(1, false);
    }
  }
  if (T.bot > 0)
    for (int i = 0; i < G.nnotes; i++)
      if (G.notes[i].state == 0 && t >= note_time(&G.notes[i])) boss_press(G.notes[i].col);
  bool bolts_done = true;
  for (int i = 0; i < (int)SDL_arraysize(G.bolts); i++) if (G.bolts[i].on) bolts_done = false;
  if (G.boss_hp <= 0 && bolts_done) boss_end(true);
  else if (t > (DANCE_COUNT_IN + DANCE_STEPS) * DANCE_STEP_SEC + 0.4) boss_end(false);
}

// ---- game flow -------------------------------------------------------------------
static void start_rank(int r) {
  float boss_timer = G.boss_timer;
  int score = G.score, sel = G.sel, streak = G.streak, weapon = G.weapon, mode = G.mode;
  int cash = G.cash, potions = G.potions; bool frost = G.frost_next;
  SDL_memset(&G, 0, sizeof(G));
  G.mode = mode;
  G.state = ST_PLAY; G.rank = r; G.sel = sel; G.score = score; G.streak = streak; G.weapon = weapon;
  G.cash = cash; G.potions = potions; G.frost = frost; G.score_pos = -1;
  G.hp = G.hp_max = MAX_HP; G.target = -1; G.last_lane = -1; G.ward = 1;
  if (G.mode == MODE_DRIVE) {
    G.hp = G.hp_max = r == 0 ? 3 : 7;   // misses allowed, plus one
    for (int i = 0; i < NQUIZ; i++) G.q_order[i] = (Uint8)i;
    for (int i = NQUIZ - 1; i > 0; i--) { int j = rndi(i + 1); Uint8 t = G.q_order[i]; G.q_order[i] = G.q_order[j]; G.q_order[j] = t; }
  }
  G.spawn_t = 1.0f; G.archer_y = (float)LANE_Y(1);
  G.boss_timer = boss_timer > 0 ? boss_timer : BOSS_MIN_GAP + rndf() * BOSS_RANDOM_GAP;
  audio_song(SONG_TYPING);
}

static bool blob_write(const char *name, const void *data, int n);
static bool blob_read(const char *name, void *data, int n);
static void set_paused(bool paused);
static void go_title(void);

static void enter_shop(int next_rank) {
  G.state = ST_SHOP; G.state_t = 0; G.shop_sel = 0; G.shop_next = next_rank; G.msg_t = 0;
}

static void new_game(int r) {
  G.score = 0; G.streak = 0; G.weapon = 0; G.potions = 0; G.frost_next = false; G.boss_timer = 0;
  G.cash = G.mode == MODE_DRIVE ? 0 : r * START_COINS_PER_RANK;
  // Starting above rank 1 means starting with nothing bought: visit the shop first.
  if (G.cash > 0) enter_shop(r); else start_rank(r);
}

// ---- the shop ------------------------------------------------------------------------
enum { SHOP_WEAPON, SHOP_POTION, SHOP_FROST, SHOP_LEAVE, SHOP_SLOTS };

static void shop_buy(int slot) {
  int price = 0;
  const char *no = NULL;
  if (slot == SHOP_LEAVE) { start_rank(G.shop_next); return; }
  if (slot == SHOP_WEAPON) { if (G.weapon + 1 >= NWEAPONS) no = "NOTHING FINER TO SELL"; else price = weapons[G.weapon + 1].price; }
  else if (slot == SHOP_POTION) { if (G.potions >= POTION_MAX) no = "YOUR PACK IS FULL"; else price = POTION_PRICE; }
  else { if (G.frost_next) no = "ALREADY YOURS"; else price = FROST_PRICE; }
  if (!no && G.cash < price) no = "NOT ENOUGH COINS";
  if (no) { say(no, C_RED, 1.2f); sfx(WAVE_SQUARE, 110, 0, 0.09f, 0.12f, 0); return; }
  G.cash -= price;
  if (slot == SHOP_WEAPON) weapon_set(G.weapon + 1);
  else {
    if (slot == SHOP_POTION) G.potions++; else G.frost_next = true;
    say(slot == SHOP_POTION ? "WALL POTION!" : "FROST CHARM!", C_YELLOW, 1.2f);
    sfx(WAVE_SQUARE, 880, 400, 0.12f, 0.1f, 0);
  }
}

// ---- high scores -----------------------------------------------------------------------
#define NSCORES 8
typedef struct { char name[10]; int score, rank; } Score;
static struct { Uint32 magic; char last_name[10]; Score board[NMODES][NSCORES]; } HS;
#define HS_MAGIC 0x48534331u

static void scores_load(void) {
  if (T.on || !blob_read("scores.dat", &HS, (int)sizeof(HS)) || HS.magic != HS_MAGIC) SDL_memset(&HS, 0, sizeof(HS));
  HS.magic = HS_MAGIC;
  HS.last_name[sizeof(HS.last_name) - 1] = 0;
  for (int m = 0; m < NMODES; m++) for (int i = 0; i < NSCORES; i++) HS.board[m][i].name[sizeof(HS.board[m][i].name) - 1] = 0;
}

// Ends the run and, if the score makes the board, asks for a name.
static void end_run(int state) {
  G.state = state; G.state_t = 0; G.score_pos = -1;
  G.entering = !T.on && G.score > 0 && G.score > HS.board[G.mode][NSCORES - 1].score;
  if (G.entering) { SDL_strlcpy(G.name, HS.last_name, sizeof(G.name)); G.name_kept = G.name[0] != 0; }
}

static void score_commit(void) {
  Score *b = HS.board[G.mode];
  int pos = NSCORES - 1;
  while (pos > 0 && G.score > b[pos - 1].score) { b[pos] = b[pos - 1]; pos--; }
  if (!G.name[0]) SDL_strlcpy(G.name, "ANON", sizeof(G.name));
  SDL_strlcpy(b[pos].name, G.name, sizeof(b[pos].name));
  b[pos].score = G.score; b[pos].rank = G.rank + 1;
  SDL_strlcpy(HS.last_name, G.name, sizeof(HS.last_name));
  blob_write("scores.dat", &HS, (int)sizeof(HS));
  G.entering = false; G.score_pos = pos; G.state_t = 0;
  for (int k = 0; k < 4; k++) sfx(WAVE_SQUARE, midi_hz(72 + k * 4), 0, 0.1f, 0.1f, k * 0.08f);
}

static void name_key(SDL_Keycode key) {
  int n = (int)strlen(G.name);
  char c = key >= SDLK_A && key <= SDLK_Z ? (char)('A' + (key - SDLK_A)) : key >= SDLK_0 && key <= SDLK_9 ? (char)('0' + (key - SDLK_0)) : 0;
  if (G.name_kept && (c || key == SDLK_BACKSPACE)) { G.name[0] = 0; n = 0; G.name_kept = false; if (!c) return; }
  if (c && n < 8) { G.name[n] = c; G.name[n + 1] = 0; sfx(WAVE_SQUARE, 880, 0, 0.03f, 0.07f, 0); }
  else if (key == SDLK_BACKSPACE && n > 0) G.name[n - 1] = 0;
  else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && G.state_t > 0.5f) score_commit();
}

// Q on the pause screen: a run that made the board still gets its name taken.
static void quit_run(void) {
  set_paused(false);
  if (T.on || G.score <= 0 || G.score <= HS.board[G.mode][NSCORES - 1].score) { go_title(); return; }
  audio_song(SONG_NONE);
  G.quit = true;
  end_run(ST_GAMEOVER);
}


// Pausing stops the audio device too, so the song clock that judges boss notes
// freezes with the game and picks up exactly where it left off.
static void set_paused(bool paused) {
  G.paused = paused;
  if (A.stream) { if (paused) SDL_PauseAudioStreamDevice(A.stream); else SDL_ResumeAudioStreamDevice(A.stream); }
}

// The beat-timing adjustment is remembered between visits on the web.
static void pref_file(const char *name, char *path, size_t n) {
  char *dir = SDL_GetPrefPath("vibecda", "home-row-horde");
  SDL_snprintf(path, n, "%s%s", dir ? dir : "", name);
  SDL_free(dir);
}

// Sync settings are remembered: in localStorage on the web, in a small file natively.
static int autosync;   // 1: nudge the sound delay after each boss battle, as StepMania's AutoSync does
static void settings_save(void) {
#ifdef __EMSCRIPTEN__
  EM_ASM({ try { localStorage.setItem('hrh-sync2', $0 + ',' + $1 + ',' + $2); } catch (e) {} }, A.offset_ms, A.visual_ms, autosync);
#else
  if (T.on) return;
  char path[512]; pref_file("sync2.txt", path, sizeof(path));
  FILE *f = fopen(path, "w");
  if (f) { fprintf(f, "%d %d %d\n", A.offset_ms, A.visual_ms, autosync); fclose(f); }
#endif
}
static void settings_load(void) {
  // Defaults until the player calibrates: the device's starting sound delay, and
  // no sight lead (StepMania's VisualDelaySeconds was 0 on the same machine).
  A.offset_ms = A.default_offset_ms;
  A.visual_ms = 0;
#ifdef __EMSCRIPTEN__
  int found = EM_ASM_INT({ try { return (localStorage.getItem('hrh-sync2') || '').split(',').length == 3 ? 1 : 0; } catch (e) { return 0; } });
  if (found) {
    A.offset_ms = EM_ASM_INT({ try { return parseInt(localStorage.getItem('hrh-sync2').split(',')[0], 10) || 0; } catch (e) { return 0; } });
    A.visual_ms = EM_ASM_INT({ try { return parseInt(localStorage.getItem('hrh-sync2').split(',')[1], 10) || 0; } catch (e) { return 0; } });
    autosync = EM_ASM_INT({ try { return parseInt(localStorage.getItem('hrh-sync2').split(',')[2], 10) || 0; } catch (e) { return 0; } });
  }
#else
  if (T.on) return;
  char path[512]; pref_file("sync2.txt", path, sizeof(path));
  FILE *f = fopen(path, "r");
  if (f) {
    int o, v, a;
    if (fscanf(f, "%d %d %d", &o, &v, &a) == 3) { A.offset_ms = o; A.visual_ms = v; autosync = a; }
    fclose(f);
  }
#endif
  A.offset_ms = SDL_clamp(A.offset_ms, -250, 250);
  A.visual_ms = SDL_clamp(A.visual_ms, -100, 200);
  autosync = autosync ? 1 : 0;
}
static void offset_set(int ms) { A.offset_ms = SDL_clamp(ms, -250, 250); settings_save(); }
static void offset_change(int delta) { offset_set(A.offset_ms + delta); }
static void visual_set(int ms) { A.visual_ms = SDL_clamp(ms, -100, 200); settings_save(); }

// ---- beat calibration ----------------------------------------------------------------
// Two steps, because sound and picture are delayed by different amounts.
// SOUND: a plain 4/4 click plays and the player taps along by ear; the average
//   distance from the beat becomes the sound delay used to judge presses.
// SIGHT: the click is silent and the player taps when a rising arrow fills the
//   receptor; the average becomes how far ahead arrows are drawn, so that what
//   is seen lines up with what is heard.
#define CAL_TAPS 16
#define CAL_BEAT (DANCE_STEP_SEC * 2)
enum { CAL_SOUND, CAL_SIGHT };
static struct { int mode, n; float err[CAL_TAPS]; float last, flash; } Cal;

static void calibrate_mode(int mode) {
  SDL_memset(&Cal, 0, sizeof(Cal));
  Cal.mode = mode;
  A.song_muted = mode == CAL_SIGHT;
  G.state_t = 0;
}
static void calibrate_start(void) {
  G.state = ST_CALIBRATE;
  calibrate_mode(CAL_SOUND);
  audio_song(SONG_CALIBRATE);
}
static int cal_count(void) { return Cal.n < CAL_TAPS ? Cal.n : CAL_TAPS; }
// The mean of the taps, leaving out the two earliest and two latest once there
// are enough, so one stumble does not skew the result.
static float cal_mean(void) {
  float v[CAL_TAPS], sum = 0; int n = cal_count();
  for (int i = 0; i < n; i++) v[i] = Cal.err[i];
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (v[b] < v[a]) { float t = v[a]; v[a] = v[b]; v[b] = t; }
  int trim = n >= 10 ? 2 : 0;
  for (int i = trim; i < n - trim; i++) sum += v[i];
  return n ? sum / (float)(n - 2 * trim) : 0;
}
static float cal_spread(void) {
  float m = cal_mean(), sum = 0; int n = cal_count();
  for (int i = 0; i < n; i++) sum += (Cal.err[i] - m) * (Cal.err[i] - m);
  return n > 1 ? sqrtf(sum / (n - 1)) : 0;
}
// What saving now would store for the current step.
static int cal_result(void) {
  int ms = (int)lroundf(cal_mean() * 1000);
  return Cal.mode == CAL_SOUND ? ms : A.visual_ms + ms;
}
static void calibrate_tap(void) {
  // Sound taps are measured against the raw clock; sight taps against the
  // clock as already corrected for sound, so the two delays stay separate.
  double t = Cal.mode == CAL_SOUND ? audio_raw_time_at(key_ts) : audio_song_time_at(key_ts);
  double e = t - floor(t / CAL_BEAT + 0.5) * CAL_BEAT;   // distance from the nearest beat
  Cal.last = (float)e; Cal.flash = 0.12f;
  if (G.state_t < 1.0f) return;                          // let the click settle first
  Cal.err[Cal.n++ % CAL_TAPS] = (float)e;
}

// ---- small files -----------------------------------------------------------------------
// Fixed-size blobs: a file in the preferences folder natively, localStorage on the web.
static bool blob_write(const char *name, const void *data, int n) {
#ifdef __EMSCRIPTEN__
  return EM_ASM_INT({ try {
    var b = HEAPU8.subarray($0, $0 + $1);
    var s = '';
    for (var i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
    localStorage.setItem('hrh-' + UTF8ToString($2), btoa(s)); return 1;
  } catch (e) { return 0; } }, data, n, name);
#else
  if (T.on) return false;
  char path[512]; pref_file(name, path, sizeof(path));
  FILE *f = fopen(path, "wb");
  bool ok = false;
  if (f) { ok = fwrite(data, (size_t)n, 1, f) == 1; fclose(f); }
  return ok;
#endif
}
static bool blob_read(const char *name, void *data, int n) {
#ifdef __EMSCRIPTEN__
  return EM_ASM_INT({ try {
    var s = localStorage.getItem('hrh-' + UTF8ToString($2)); if (!s) return 0;
    s = atob(s); if (s.length != $1) return 0;
    for (var i = 0; i < s.length; i++) HEAPU8[$0 + i] = s.charCodeAt(i);
    return 1;
  } catch (e) { return 0; } }, data, n, name);
#else
  char path[512]; pref_file(name, path, sizeof(path));
  FILE *f = fopen(path, "rb");
  bool ok = false;
  if (f) { ok = fread(data, (size_t)n, 1, f) == 1; fclose(f); }
  return ok;
#endif
}

// ---- save and load -------------------------------------------------------------------
// F6 stores the whole game in progress; F7 brings it back, paused.
typedef struct { Uint32 magic, size; int last_boss; } SaveHead;
#define SAVE_MAGIC 0x48524831u
static struct { SaveHead head; char game[sizeof(G)]; } save_buf;

static void save_game(void) {
  if (G.state != ST_PLAY) { say("NOTHING TO SAVE YET", C_GREY, 1.5f); return; }
  save_buf.head = (SaveHead){ SAVE_MAGIC, (Uint32)sizeof(G), last_boss };
  SDL_memcpy(save_buf.game, &G, sizeof(G));
  bool ok = blob_write("save.dat", &save_buf, (int)sizeof(save_buf));
  say(ok ? "GAME SAVED" : "COULD NOT SAVE", ok ? C_ZOMBIE : C_RED, 1.5f);
  if (ok) sfx(WAVE_SQUARE, 880, 400, 0.12f, 0.1f, 0);
}

static void set_paused(bool paused);
static void load_game(void) {
  bool ok = blob_read("save.dat", &save_buf, (int)sizeof(save_buf));
  // A save from a different version of the game has a different size and is refused.
  if (!ok || save_buf.head.magic != SAVE_MAGIC || save_buf.head.size != sizeof(G)) { say("NO SAVED GAME FOUND", C_RED, 1.5f); return; }
  SDL_memcpy(&G, save_buf.game, sizeof(G));
  last_boss = save_buf.head.last_boss;
  A.song_muted = false;
  // A battle cannot be rejoined mid-song: the boss returns a few seconds after loading instead.
  if (G.sub != SUB_TYPING) { G.sub = SUB_TYPING; G.boss_timer = 4; }
  for (int i = 0; i < (int)SDL_arraysize(G.bolts); i++) if (G.bolts[i].at_boss) G.bolts[i].on = false;
  audio_song(SONG_TYPING);
  say("GAME LOADED", C_ZOMBIE, 1.5f);
  set_paused(true);   // start paused, so there is time to get hands on the keys
}

static void go_title(void) {
  int sel = G.sel, mode = G.mode;
  set_paused(false);
  A.song_muted = false;
  SDL_memset(&G, 0, sizeof(G));
  G.state = ST_TITLE; G.sel = sel; G.mode = mode; G.target = -1;
  audio_song(SONG_TYPING);
}

static void update_wpm(float dt) {
  int n = 0;
  int total = G.key_head < 256 ? G.key_head : 256;
  for (int i = 0; i < total; i++) if (G.key_times[i] > G.tclock - WPM_WINDOW) n++;
  float window = G.rank_clock < WPM_WINDOW ? (G.rank_clock < 2 ? 2 : G.rank_clock) : WPM_WINDOW;
  float now = (float)n / 5.0f * 60.0f / window;
  G.wpm += (now - G.wpm) * (dt * 3 > 1 ? 1 : dt * 3);
  if (G.rank_clock > WPM_WINDOW && G.wpm > G.best_wpm) G.best_wpm = G.wpm;
}

// ---- driver's test ---------------------------------------------------------------------
static const Quiz *quiz_now(void) { return &quiz_bank[G.q_order[G.q_num % NQUIZ]]; }

// Ask the next question: one monster per answer, lettered from the top lane down.
static void quiz_next(void) {
  const Quiz *Q = quiz_now();
  G.q_n = 0;
  while (G.q_n < 4 && Q->a[G.q_n]) G.q_n++;
  for (int i = 0; i < G.q_n; i++) G.q_perm[i] = (Uint8)i;
  for (int i = G.q_n - 1; i > 0; i--) { int j = rndi(i + 1); Uint8 t = G.q_perm[i]; G.q_perm[i] = G.q_perm[j]; G.q_perm[j] = t; }
  for (int i = 0; i < MAX_EN; i++) G.en[i].alive = false;
  for (int i = 0; i < G.q_n; i++) {
    Enemy *e = &G.en[i];
    *e = (Enemy){ .alive = true, .kind = K_ZOMBIE, .lane = i, .variant = i % 3, .x = 292, .len = 1, .words = 1, .hp = 1 };
    e->word[0] = (char)('A' + i);
  }
  G.q_active = true; G.q_pick = -1; G.target = -1;
}

// An answer was chosen (or -1: the monsters reached the wall first).
static void quiz_resolve(int choice) {
  bool right = choice >= 0 && G.q_perm[choice] == 0;
  G.q_active = false; G.q_pick = choice; G.kills++;
  G.q_reveal = right ? 1.3f : 3.0f;   // a miss leaves the right answer up longer
  for (int i = 0; i < G.q_n; i++) {
    Enemy *e = &G.en[i];
    e->doomed = true;
    if (right && i == choice) { G.archer_y = (float)LANE_Y(e->lane); fire(e->x, enemy_y(e), i, false); }
    else { e->dying = 0.3f; puff(e->x, enemy_y(e), right ? C_ZOMBIE : C_RED, 6); }
  }
  if (right) {
    G.q_right++; G.correct++; G.streak++; G.score += 100;
    sfx(WAVE_SQUARE, 880, 600, 0.12f, 0.10f, 0);
  } else {
    G.wrong++; G.streak = 0;
    hurt_wall(1, true);
  }
}

static void update_quiz(float dt) {
  const Rank *R = &ranks[G.rank];
  if (G.q_reveal > 0) {
    G.q_reveal -= dt;
    if (G.q_reveal > 0) return;
    G.q_num++;
    for (int i = 0; i < MAX_EN; i++) G.en[i].alive = false;
    if (G.boss_timer <= 0 && G.kills < R->quota - 1) { boss_start(); return; }   // bosses only come between questions
  }
  if (!G.q_active) { if (G.kills < R->quota) quiz_next(); return; }
  G.boss_timer -= dt;
  bool ready = true;
  for (int i = 0; i < G.q_n; i++) {
    Enemy *e = &G.en[i];
    e->anim += dt;
    if (e->rise < 1) { e->rise += dt * 1.2f; ready = false; continue; }
    if (e->x > WALL_STOP) e->x -= R->speed * dt;
    else { quiz_resolve(-1); return; }
  }
  if (T.bot > 0 && ready) {   // the test bot always knows the answer
    T.bot_t -= dt;
    if (T.bot_t <= 0) { T.bot_t = 1.0f / T.bot; for (int i = 0; i < G.q_n; i++) if (G.q_perm[i] == 0) { quiz_resolve(i); break; } }
  }
}

static void update_typing(float dt) {
  const Rank *R = &ranks[G.rank];
  if (G.mode == MODE_DRIVE) { update_quiz(dt); return; }
  int alive = count_alive();
  // The typing clock only runs while there is something to type.
  if (alive > 0) { G.tclock += dt; G.rank_clock += dt; }
  update_wpm(dt);

  G.spawn_t -= dt;
  if (G.spawned < R->quota && alive < R->max_alive && (G.spawn_t <= 0 || (R->wpm && alive < 2) || alive == 0)) {
    spawn();
    G.spawn_t = R->interval * (0.8f + rndf() * 0.4f);
  }

  if (R->wpm && G.rank_clock > WPM_WINDOW) {
    if (G.wpm < R->wpm) G.ward -= dt / 6.0f; else G.ward += dt / 3.0f;
    if (G.ward > 1) G.ward = 1;
    if (G.ward <= 0) {
      G.ward = 0; G.fear_t -= dt;
      if (G.fear_t <= 0) { G.fear_t = 2.5f; hurt_wall(1, true); say("TYPE FASTER!", C_RED, 1.2f); }
    }
  }

  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &G.en[i];
    if (!e->alive || e->dying > 0) continue;
    e->anim += dt;
    if (e->rise < 1) { e->rise += dt * 1.2f; continue; }
    float v = R->speed * (e->kind == K_BAT ? 1.8f : e->kind == K_BRUTE ? 0.6f : 1.0f) * (G.frost ? 0.8f : 1.0f);
    if (e->x > WALL_STOP) e->x -= v * dt;
    else if (!e->doomed) {
      e->atk -= dt;
      if (e->atk <= 0) { e->atk = 1.5f; hurt_wall(e->kind == K_BRUTE ? 2 : 1, true); }
    }
  }

  if (T.bot > 0) {
    T.bot_t -= dt;
    char c = next_char();
    if (T.bot_t <= 0 && c) { T.bot_t = 1.0f / T.bot; type_char(c); }
  }

  if (alive > 0) G.boss_timer -= dt;
  if (G.boss_timer <= 0 && G.kills < R->quota - 1) boss_start();
}

static void update(float dt) {
  G.time += dt; G.state_t += dt;
  if (G.shake > 0) G.shake -= dt;
  if (G.wall_flash > 0) G.wall_flash -= dt;
  if (G.msg_t > 0) G.msg_t -= dt;
  if (G.judge_t > 0) G.judge_t -= dt;
  if (G.shoot_t > 0) G.shoot_t -= dt;
  if (G.boss_flash > 0) G.boss_flash -= dt;
  for (int i = 0; i < 4; i++) { if (G.press_flash[i] > 0) G.press_flash[i] -= dt; if (G.hit_flash[i] > 0) G.hit_flash[i] -= dt; }
  if (Cal.flash > 0) Cal.flash -= dt;
  for (int i = 0; i < (int)SDL_arraysize(G.parts); i++) {
    Particle *p = &G.parts[i];
    if (p->life <= 0) continue;
    p->life -= dt; p->x += p->vx * dt; p->y += p->vy * dt; p->vy += 160 * dt;
  }
  if (G.state != ST_PLAY) {
    if (T.bot > 0 && G.state == ST_RANKUP && G.state_t > 1) enter_shop(G.rank + 1);
    else if (T.bot > 0 && G.state == ST_SHOP) {   // the test bot buys every weapon it can afford
      while (G.weapon + 1 < NWEAPONS && G.cash >= weapons[G.weapon + 1].price) shop_buy(SHOP_WEAPON);
      shop_buy(SHOP_LEAVE);
    }
    return;
  }

  if (G.sub == SUB_TYPING) update_typing(dt);
  else if (G.sub == SUB_BOSS) boss_update(dt);
  else { G.out_t -= dt; if (G.out_t <= 0) { G.sub = SUB_TYPING; G.sync_offer = 0; } }

  for (int i = 0; i < (int)SDL_arraysize(G.bolts); i++) {
    Bolt *b = &G.bolts[i];
    if (!b->on) continue;
    float tx, ty;
    if (b->at_boss) { tx = G.boss_x; ty = boss_cy(); }
    else if (b->target >= 0 && G.en[b->target].alive) { tx = G.en[b->target].x; ty = enemy_y(&G.en[b->target]); }
    else { b->on = false; continue; }
    float dx = tx - b->x, dy = ty - b->y, d = sqrtf(dx * dx + dy * dy), stepd = weapons[weapon_now()].speed * dt;
    if (d <= stepd + 4) {
      b->on = false;
      if (b->at_boss) { G.boss_flash = 0.1f; puff(tx - 8, ty, C_YELLOW, 3); }
      else if (b->wound) wound_enemy(&G.en[b->target]);
      else { kill_enemy(&G.en[b->target]); splash(&G.en[b->target]); }
    } else {
      b->x += dx / d * stepd; b->y += dy / d * stepd;
      if (weapons[weapon_now()].trail) spark(b->x - 4, b->y, weapons[weapon_now()].trail);
    }
  }
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &G.en[i];
    if (e->alive && e->dying > 0) { e->dying -= dt; if (e->dying <= 0) e->alive = false; }
    if (e->hurt > 0) e->hurt -= dt;
  }

  if (G.hp <= 0 && G.potions > 0 && G.mode != MODE_DRIVE) {   // a wall potion saves the night
    G.potions--; G.hp = 5;
    for (int i = 0; i < MAX_EN; i++) if (G.en[i].alive && !G.en[i].doomed) { G.en[i].x = SDL_min(G.en[i].x + 70, 300.0f); G.en[i].atk = 1.5f; }
    say("POTION! THE WALL STANDS", C_ZOMBIE, 2.0f);
    for (int k = 0; k < 3; k++) sfx(WAVE_SQUARE, midi_hz(64 + k * 7), 0, 0.12f, 0.12f, k * 0.09f);
  } else if (G.hp <= 0) {
    G.hp = 0; end_run(ST_GAMEOVER);
    audio_song(SONG_NONE);
    for (int i = 0; i < 4; i++) sfx(WAVE_SQUARE, midi_hz(57 - i * 3), 0, 0.3f, 0.14f, i * 0.28f);
  } else if (G.kills >= ranks[G.rank].quota && G.sub == SUB_TYPING && G.q_reveal <= 0) {
    if (G.rank + 1 >= NRANKS || G.mode == MODE_DRIVE) end_run(ST_VICTORY); else { G.state = ST_RANKUP; G.state_t = 0; }
    for (int i = 0; i < 5; i++) sfx(WAVE_SQUARE, midi_hz(69 + (int[]){ 0, 4, 7, 12, 16 }[i]), 0, 0.14f, 0.12f, i * 0.11f);
  }
}

// ---- drawing -----------------------------------------------------------------------
static void draw_pine(int cx, int base, int h, Uint32 color) {
  for (int row = 0; row < h; row++) {
    int w = (row % 9) + row / 4 + 1;
    rect((float)(cx - w), (float)(base - h + row), (float)(w * 2), 1, color);
  }
  rect((float)(cx - 1), (float)base, 2, 4, color);
}

static void build_background(void) {
  bg_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
  SDL_SetTextureScaleMode(bg_tex, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderTarget(ren, bg_tex);
  Uint32 save = rng_state; rng_state = 77;
  rect(0, 0, SCREEN_W, 60, 0x00002C);
  rect(0, 60, SCREEN_W, 40, 0x080050);
  for (int i = 0; i < 50; i++) rect((float)rndi(SCREEN_W), (float)(16 + rndi(60)), 1, 1, rndi(3) ? C_GREY : C_WHITE);
  // moon
  for (int y = -18; y <= 18; y++) {
    int w = (int)sqrtf((float)(18 * 18 - y * y));
    rect((float)(262 - w), (float)(44 + y), (float)(w * 2), 1, 0xF8F8B8);
  }
  rect(254, 36, 5, 4, 0xD8D890); rect(266, 48, 6, 5, 0xD8D890); rect(270, 34, 3, 3, 0xD8D890); rect(252, 52, 3, 2, 0xD8D890);
  for (int x = -6; x < SCREEN_W + 10; x += 9 + rndi(8)) draw_pine(x, 96, 26 + rndi(26), 0x002838);
  for (int x = 4; x < SCREEN_W + 10; x += 22 + rndi(20)) draw_pine(x, 98, 44 + rndi(24), 0x001018);
  // forest floor and lanes
  rect(0, 98, SCREEN_W, 100, 0x0C2C08);
  for (int l = 0; l < LANES; l++) rect(0, (float)(LANE_Y(l) - 5), SCREEN_W, 8, 0x143810);
  for (int i = 0; i < 260; i++) {
    int x = rndi(SCREEN_W), y = 100 + rndi(94);
    rect((float)x, (float)y, 1, 2, 0x1C5C14); rect((float)(x + 1), (float)(y + 1), 1, 1, 0x1C5C14);
  }
  for (int i = 0; i < 40; i++) rect((float)rndi(SCREEN_W), (float)(100 + rndi(94)), 2, 1, 0x30302C);
  // graves along the tree line, where the dead climb out
  for (int l = 0; l < LANES; l++) {
    spr(s_tomb, (float)(292 + (l % 2) * 12), (float)(LANE_Y(l) - 21));
    if (l % 2) spr(s_tomb, 268, (float)(LANE_Y(l) - 22));
  }
  // the compound yard
  rect(0, 98, WALL_X, 100, 0x3C2810);
  for (int i = 0; i < 40; i++) rect((float)rndi(WALL_X), (float)(100 + rndi(94)), 2, 1, 0x503818);
  rect(2, 100, 12, 10, C_DBROWN); rect(0, 97, 16, 4, C_DRED); rect(6, 104, 4, 6, C_BLACK);   // a little hut
  rng_state = save;
  SDL_SetRenderTarget(ren, NULL);
}

static void draw_wall(void) {
  bool flash = G.wall_flash > 0;
  for (int y = 90, i = 0; y < 198; y += 9, i++) {
    bool cracked = (G.hp <= 6 && i % 3 == 1) || (G.hp <= 3 && i % 3 != 0);
    Uint32 body = flash ? C_RED : cracked ? 0x80500C : C_BROWN;
    rect(WALL_X, (float)y, 14, 8, body);
    rect(WALL_X, (float)y, 14, 1, flash ? C_WHITE : 0xD8A038);
    rect(WALL_X, (float)(y + 7), 14, 1, C_DBROWN);
    rect((float)(WALL_X + 14), (float)(y + 2), 2, 4, body);     // sharpened tip facing the forest
    rect((float)(WALL_X + 16), (float)(y + 3), 1, 2, body);
    if (cracked) { rect((float)(WALL_X + 4 + i % 5), (float)(y + 2), 1, 4, C_BLACK); rect((float)(WALL_X + 5 + i % 5), (float)(y + 4), 2, 1, C_BLACK); }
  }
  for (int k = 0; k < 2; k++) {   // torches
    float ty = k ? 176.0f : 94.0f;
    bool f = ((int)(G.time * 8) + k) % 2;
    rect(WALL_X + 6, ty - 6, 2, 8, C_DBROWN);
    rect(WALL_X + 5, ty - 11, 4, 5, C_RED);
    rect((float)(WALL_X + 6), ty - (f ? 14.0f : 12.0f), 2, f ? 6.0f : 4.0f, C_YELLOW);
  }
}

static void draw_enemy(const Enemy *e, float bob) {
  if ((e->dying > 0 && ((int)(e->dying * 40) & 1)) || (e->hurt > 0 && ((int)(e->hurt * 30) & 1))) return;
  int f = ((int)(e->anim * (e->kind == K_BAT ? 8 : 3))) & 1;
  if (e->kind == K_BAT) {
    Spr s = s_bat[f];
    spr(s, e->x - s.w / 2.0f, enemy_y(e) - s.h / 2.0f + bob);
  } else {
    Spr s = e->kind == K_BRUTE ? s_brute : s_zombie[e->variant][f];
    int rows = (int)((float)s.h * (e->rise > 1 ? 1 : e->rise));
    float y = (float)(LANE_Y(e->lane) - rows) + bob + (e->kind == K_BRUTE && f ? 1 : 0);
    spr_rows(s, e->x - s.w / 2.0f, y, rows);
    if (e->rise < 1) rect(e->x - 9, (float)LANE_Y(e->lane) - 1, 18, 2, 0x30302C);   // disturbed earth
  }
}

// Which word of its phrase position `pos` falls in.
static int word_at(const Enemy *e, int pos) {
  int w = 0;
  for (int i = 0; i < pos && i < e->len; i++) if (e->word[i] == ' ') w++;
  return w;
}

// Draws a monster's text with typed letters dimmed. A phrase is coloured word by
// word to match its armour pips; the word being typed is the bright one.
static void draw_words(float x, float y, const Enemy *e, int from, int to, bool targeted, int scale) {
  x = SDL_floorf(x);   // whole pixels only, or the glyphs smear
  for (int i = from; i < to; i++) {
    int w = word_at(e, i), current = word_at(e, e->typed);
    Uint32 color = i < e->typed ? C_DGREY
                 : e->words == 1 ? (targeted ? C_YELLOW : C_WHITE)
                 : w == current ? (targeted ? C_WHITE : word_colors[w % 4])
                 : word_colors[w % 4];
    char ch[2] = { e->word[i], 0 };
    text(x + (float)((i - from) * FONT_W * scale), y, ch, color, scale);
  }
  // waiting on the space bar: blink a marker in the gap
  if (targeted && e->word[e->typed] == ' ' && e->typed >= from && e->typed < to && ((int)(G.time * 6) & 1))
    rect(x + (float)((e->typed - from) * FONT_W * scale), y + 6.0f * scale, 5.0f * scale, (float)scale, C_YELLOW);
}

static void draw_label(const Enemy *e, bool targeted) {
  if (e->doomed || e->rise < 0.3f) return;
  if (G.mode == MODE_DRIVE) {   // just the answer's letter, beside the monster and in its colour
    char letter[2] = { e->word[0], 0 };
    float lx = SDL_floorf(e->x) - 24, ly = (float)(LANE_Y(e->lane) - 19);
    rect(lx - 1, ly - 1, 15, 15, C_BLACK);
    rect(lx, ly, 13, 13, word_colors[e->lane % 4]);
    text(lx + 2, ly + 3, letter, C_BLACK, 1); text(lx + 3, ly + 3, letter, C_BLACK, 1);
    return;
  }
  if (G.mode == MODE_MATH) {   // the question, then whatever of the answer has been typed
    char shown[40];
    SDL_snprintf(shown, sizeof(shown), "%s=%.*s", e->q, e->typed, e->word);
    int mw = ((int)strlen(e->q) + 1 + e->len) * FONT_W + 5;
    float mtop = e->kind == K_BAT ? enemy_y(e) - 8 : (float)(LANE_Y(e->lane) - (e->kind == K_BRUTE ? 30 : 24));
    float mx = SDL_clamp(SDL_floorf(e->x - mw / 2.0f), 2.0f, (float)(SCREEN_W - 2 - mw)), my = SDL_floorf(mtop - 12);
    rect(mx, my, (float)mw, 11, C_BLACK);
    frame(mx, my, (float)mw, 11, targeted ? C_YELLOW : C_DGREY);
    float tx = text_n(mx + 3, my + 2, shown, (int)strlen(e->q) + 1, C_WHITE, 1);
    text(tx, my + 2, shown + strlen(e->q) + 1, C_YELLOW, 1);
    if (targeted && ((int)(G.time * 6) & 1)) rect(tx + (float)(e->typed * FONT_W), my + 8, 5, 1, C_YELLOW);
    return;
  }
  int w = text_w(e->word, 1) + 6;
  float top = e->kind == K_BAT ? enemy_y(e) - 8 : (float)(LANE_Y(e->lane) - (e->kind == K_BRUTE ? 30 : 24));
  float x = SDL_floorf(e->x - w / 2.0f), y = SDL_floorf(top - 12);
  if (x < 2) x = 2;
  if (x + w > SCREEN_W - 2) x = (float)(SCREEN_W - 2 - w);
  Uint32 edge = !targeted ? C_DGREY : e->words > 1 ? word_colors[word_at(e, e->typed) % 4] : C_YELLOW;
  rect(x, y, (float)w, 11, C_BLACK);
  frame(x, y, (float)w, 11, edge);
  draw_words(x + 3, y + 2, e, 0, e->len, targeted, 1);
  if (e->words > 1)   // armour pips: one per word, emptied as each word is typed
    for (int k = 0; k < e->words; k++) {
      float px = x + 1 + (float)(k * 6), py = y - 5;
      rect(px, py, 5, 4, C_BLACK);
      if (k >= e->words - e->hp) rect(px + 1, py + 1, 3, 2, word_colors[k % 4]);
      else frame(px, py, 5, 4, C_DGREY);
    }
  if (targeted && ((int)(G.time * 6) & 1)) rect(e->x - 1, y + 12, 3, 2, edge);
}

static void draw_keyboard(char want) {
  const char *set = G.rank == 0 ? "ASDFJKL" : G.rank == 1 ? "ASDFGHJKLQWERTYUIOP" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int want_col = -1, want_row = -1;
  for (int r = 0; r < 3; r++)
    for (int c = 0; kb_rows[r][c]; c++) {
      char k = kb_rows[r][c];
      float x = (float)(8 + c * 11 + r * 4), y = (float)(202 + r * 12);
      bool hot = k == want, in_set = strchr(set, k) != NULL;
      if (hot) { want_col = c; want_row = r; }
      Uint32 bg = hot ? (((int)(G.time * 5) & 1) ? C_YELLOW : C_SKIN) : in_set ? C_DGREY : 0x303030;
      rect(x, y, 10, 11, bg);
      char s[2] = { k, 0 };
      text(x + 3, y + 2, s, hot ? C_BLACK : in_set ? C_WHITE : C_DGREY, 1);
      if (k == 'F' || k == 'J') rect(x + 3, y + 10, 4, 1, hot ? C_BLACK : C_WHITE);   // home-row bumps
    }
  if (want_col >= 0) {
    char big[2] = { want, 0 };
    text_shadow(140, 204, big, C_YELLOW, 4);
    text(172, 204, want_col < 5 ? "LEFT HAND" : "RIGHT HAND", C_WHITE, 1);
    text(172, 215, fingers[want_col], C_CYAN, 1);
    text(172, 226, want_row == 1 ? "HOME ROW" : want_row == 0 ? "REACH UP" : "REACH DOWN", C_DGREY, 1);
  } else text(140, 214, "GET READY...", C_DGREY, 1);
}

static void draw_typing_hud(void) {
  const Rank *R = &ranks[G.rank];
  char buf[48];
  SDL_snprintf(buf, sizeof(buf), "WPM %d", (int)(G.wpm + 0.5f));
  text(8, 203, buf, C_WHITE, 2);
  if (R->wpm) {
    bool ok = G.wpm >= R->wpm || G.rank_clock <= WPM_WINDOW;
    float span = R->wpm * 2.0f, fill = G.wpm / span; if (fill > 1) fill = 1;
    rect(8, 221, 102, 6, C_DGREY); rect(9, 222, 100, 4, C_BLACK);
    rect(9, 222, 100 * fill, 4, ok ? C_GREEN : C_RED);
    rect(58, 219, 1, 10, C_WHITE);
    SDL_snprintf(buf, sizeof(buf), "NEED %d", R->wpm);
    text(114, 221, buf, ok ? C_GREY : C_RED, 1);
    text(8, 230, "WARD", C_CYAN, 1);
    rect(36, 230, 74, 7, C_DGREY); rect(37, 231, 72, 5, C_BLACK);
    if (G.ward > 0) rect(37, 231, 72 * G.ward, 5, C_CYAN);
    else if ((int)(G.time * 4) & 1) text(40, 230, "WARD IS DOWN", C_RED, 1);
  } else {
    int total = G.correct + G.wrong;
    SDL_snprintf(buf, sizeof(buf), "ACCURACY %d%%", total ? G.correct * 100 / total : 100);
    text(8, 222, buf, C_GREY, 1);
    SDL_snprintf(buf, sizeof(buf), "STREAK %d", G.streak);
    text(8, 231, buf, G.streak >= 10 ? C_YELLOW : C_GREY, 1);
  }
  int i = G.target >= 0 ? G.target : nearest_enemy();
  if (i >= 0) {
    const Enemy *e = &G.en[i];
    bool locked = G.target >= 0;
    if (G.mode == MODE_MATH) {
      char shown[40];
      SDL_snprintf(shown, sizeof(shown), "%s=%.*s", e->q, e->typed, e->word);
      int scale = strlen(shown) + (size_t)(e->len - e->typed) > 9 ? 2 : 3;
      float x = SDL_floorf(238 - (float)((strlen(e->q) + 1 + (size_t)e->len) * FONT_W * scale) / 2.0f);
      float tx = text_n(x, scale == 2 ? 212.0f : 208.0f, shown, (int)strlen(e->q) + 1, C_WHITE, scale);
      text(tx, scale == 2 ? 212.0f : 208.0f, shown + strlen(e->q) + 1, C_YELLOW, scale);
    } else if (e->words == 1) {
      int scale = e->len > 6 ? 2 : 3;
      draw_words(238 - text_w(e->word, scale) / 2.0f, scale == 2 ? 212.0f : 208.0f, e, 0, e->len, locked, scale);
    } else {
      // A phrase: the word being typed is shown large, the whole phrase small beneath it.
      int from = e->typed, to;
      if (e->word[from] == ' ') text_center(238, 203, "SPACE", C_YELLOW, 2);
      else {
        while (from > 0 && e->word[from - 1] != ' ') from--;
        for (to = from; to < e->len && e->word[to] != ' '; to++) {}
        draw_words(238 - ((to - from) * FONT_W * 2 - 2) / 2.0f, 203, e, from, to, locked, 2);
      }
      draw_words(238 - text_w(e->word, 1) / 2.0f, 224, e, 0, e->len, locked, 1);
    }
  }
}

static const double boss_angles[4] = { -90, 180, 0, 90 };   // left, down, up, right
static void overlay(int alpha);
static void draw_arrow(float cx, float cy, float size, int col, Uint32 rgb, int alpha) {
  SDL_FRect d = { cx - size / 2, cy - size / 2, size, size };
  SDL_SetTextureColorMod(s_note.t, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
  SDL_SetTextureAlphaMod(s_note.t, (Uint8)alpha);
  SDL_RenderTextureRotated(ren, s_note.t, NULL, &d, boss_angles[col], NULL, SDL_FLIP_NONE);
  SDL_SetTextureColorMod(s_note.t, 255, 255, 255);
  SDL_SetTextureAlphaMod(s_note.t, 255);
}

static void draw_calibrate(void) {
  char buf[64];
  int n = cal_count();
  bool sight = Cal.mode == CAL_SIGHT;
  double t = audio_song_time() + A.visual_ms / 1000.0;
  double beat = t / CAL_BEAT, phase = beat - floor(beat);
  SDL_RenderTexture(ren, bg_tex, NULL, NULL);
  overlay(190);
  text_center(160, 8, "BEAT CALIBRATION", C_YELLOW, 2);
  text_center(160, 28, sight ? "STEP 2 OF 2: SIGHT (THE CLICK IS SILENT)" : "STEP 1 OF 2: SOUND", C_CYAN, 1);
  text_center(160, 40, sight ? "TAP WHEN A RED ARROW FILLS THE GREY ONE" : "LISTEN, AND TAP SPACE ON EVERY CLICK", C_WHITE, 1);
  rect_a(144, 52, 32, 88, 0x101028, 255);
  // In the sound step nothing moves: the eyes must not lead the ears.
  int v = Cal.flash > 0 ? 255 : sight ? 110 : 80;
  draw_arrow(160, 64, 14, 2, (Uint32)(v << 16 | v << 8 | v), 255);
  if (sight) {
    SDL_Rect clip = { 144, 52, 32, 88 };
    SDL_SetRenderClipRect(ren, &clip);
    for (int k = 0; k < 3; k++) draw_arrow(160, 64 + (float)((k + 1 - phase) * CAL_BEAT) * 90, 14, 2, 0xF83800, 255);
    SDL_SetRenderClipRect(ren, NULL);
  }
  if (Cal.n || Cal.flash > 0) {
    int ms = (int)lroundf(Cal.last * 1000);
    SDL_snprintf(buf, sizeof(buf), "LAST TAP: %d MS %s", abs(ms), ms >= 0 ? "LATE" : "EARLY");
    text_center(160, 146, buf, C_GREY, 1);
  }
  if (n >= 4) {
    SDL_snprintf(buf, sizeof(buf), "MEASURED %s: %+d MS", sight ? "SIGHT LEAD" : "SOUND DELAY", cal_result());
    text_center(160, 158, buf, C_CYAN, 1);
    SDL_snprintf(buf, sizeof(buf), "%d TAPS, STEADY TO WITHIN %d MS", n, (int)lroundf(cal_spread() * 1000));
    text_center(160, 169, buf, cal_spread() < 0.03f ? C_ZOMBIE : C_SKIN, 1);
  } else text_center(160, 158, "KEEP TAPPING...", C_CYAN, 1);
  SDL_snprintf(buf, sizeof(buf), "SAVED: SOUND %+d MS   SIGHT %+d MS", A.offset_ms, A.visual_ms);
  text_center(160, 183, buf, C_WHITE, 1);
  text_center(160, 196, n >= 8 ? (sight ? "ENTER: SAVE THE SIGHT LEAD" : "ENTER: SAVE, THEN GO TO STEP 2") : "ENTER SAVES AFTER 8 TAPS",
              n >= 8 ? C_YELLOW : C_DGREY, 1);
  text_center(160, 207, "TAB: OTHER STEP   R: START OVER   - +: NUDGE BY 2", C_GREY, 1);
  SDL_snprintf(buf, sizeof(buf), "A: AUTOSYNC AFTER BOSS BATTLES IS %s", autosync ? "ON" : "OFF");
  text_center(160, 218, buf, autosync ? C_ZOMBIE : C_GREY, 1);
  text_center(160, 229, "ESC: BACK TO THE TITLE", C_GREY, 1);
}

static void draw_boss(void) {
  // Drawn slightly ahead of the sound clock, by the measured display delay.
  double t = audio_song_time() + A.visual_ms / 1000.0;
  double beat = t / (DANCE_STEP_SEC * 2);
  float bob = (beat - floor(beat)) < 0.5 ? -2.0f : 0.0f;
  const Boss *B = &bosses[G.boss_kind];
  Spr body = *B->frames[(int)(beat * 2) & 1];
  float bw = (float)(body.w * B->scale), bh = (float)(body.h * B->scale);
  float top = boss_cy() - bh / 2 + (B->hover ? 0 : bob);
  if (!(G.boss_flash > 0 && ((int)(G.time * 40) & 1))) {
    SDL_FRect d = { SDL_floorf(G.boss_x - bw / 2), SDL_floorf(top), bw, bh };
    SDL_RenderTexture(ren, body.t, NULL, &d);
    if (G.boss_kind == 0) {   // the king's crown
      rect(G.boss_x - 10, top - 8, 20, 2, C_YELLOW);
      for (int i = 0; i < 3; i++) rect(G.boss_x - 10 + i * 8, top - 12, 4, 4, C_YELLOW);
    }
  }
  float bar = SDL_min(top - 22, 150.0f);
  rect(G.boss_x - 26, bar, 52, 6, C_DGREY); rect(G.boss_x - 25, bar + 1, 50, 4, C_BLACK);
  rect(G.boss_x - 25, bar + 1, 50.0f * G.boss_hp / (float)(G.boss_max ? G.boss_max : 1), 4, C_RED);

  // note highway
  rect_a(90, 18, 140, 178, C_BLACK, 190);
  frame(90, 18, 140, 178, C_DPURP);
  text_center(160, 22, B->name, B->color, 1);
  // Receptors sit at the top and pulse with the beat; arrows rise to meet them.
  double phase = t > 0 ? beat - floor(beat) : 1;
  int pulse = 100 + (int)(110 * (1 - phase) * (1 - phase));
  for (int c = 0; c < 4; c++) {
    float cx = 112.0f + 32.0f * c;
    rect_a(cx - 15, 34, 30, 158, 0x101028, 255);
    if (G.hit_flash[c] > 0) rect_a(cx - 15, 34, 30, 158, G.hit_color[c], (int)(G.hit_flash[c] / 0.2f * 70));
    int v = G.press_flash[c] > 0 ? 255 : pulse;
    draw_arrow(cx, RECEPTOR_Y, G.press_flash[c] > 0 ? 12 : 14, c, (Uint32)(v << 16 | v << 8 | v), 255);
  }
  SDL_Rect clip = { 90, 34, 140, B->reveal_y - 34 };
  SDL_SetRenderClipRect(ren, &clip);
  for (int i = 0; i < G.nnotes; i++) {
    const Note *n = &G.notes[i];
    if (n->state == 1) continue;
    float y = RECEPTOR_Y + (float)(note_time(n) - t) * B->note_speed;
    if (y < 26 || y > 204) continue;
    // Coloured by rhythm, like StepMania's note skin: on the beat red, off the beat blue.
    Uint32 col = n->state == 2 ? 0x505050 : n->step % 2 == 0 ? 0xF83800 : 0x3C7CFC;
    draw_arrow(112.0f + 32.0f * n->col, SDL_floorf(y), 14, n->col, col, 255);
  }
  SDL_SetRenderClipRect(ren, NULL);
  // A hit lights the receptor: a bright arrow in the judgement's colour that swells and fades.
  for (int c = 0; c < 4; c++)
    if (G.hit_flash[c] > 0) {
      float k = G.hit_flash[c] / 0.2f;
      draw_arrow(112.0f + 32.0f * c, RECEPTOR_Y, 14, c, C_WHITE, 255);
      draw_arrow(112.0f + 32.0f * c, RECEPTOR_Y, 14 + 14 * (1 - k), c, G.hit_color[c], (int)(k * 220));
    }

  double count_in = DANCE_COUNT_IN * DANCE_STEP_SEC;
  if (t < count_in) {
    text_center(160, 66, "BOSS BATTLE!", C_RED, 2);
    text_center(160, 90, "MOVE YOUR HAND TO", C_WHITE, 1);
    text_center(160, 104, "ARROW KEYS", C_YELLOW, 2);
    text_center(160, 128, B->hint, C_GREY, 1);
    int left = (int)ceil((count_in - t) / (DANCE_STEP_SEC * 2));
    if (left <= 3 && left >= 1) { char s[2] = { (char)('0' + left), 0 }; text_center(160, 144, s, C_WHITE, 3); }
  } else if (G.judge_t > 0) {
    text_center(160, 108, G.judge, G.judge_color, 2);
    if (G.judge_sub[0]) text_center(160, 126, G.judge_sub, C_GREY, 1);
    if (G.combo >= 4) { char s[16]; SDL_snprintf(s, sizeof(s), "COMBO %d", G.combo); text_center(160, 138, s, C_WHITE, 1); }
  }
}

// Greedy word wrap. Returns the number of lines used; draws only if `draw`.
static int text_wrap(float x, float y, const char *s, int cols, Uint32 color, bool draw) {
  int lines = 0;
  while (*s) {
    int n = (int)strlen(s);
    if (n > cols) { n = cols; while (n > 0 && s[n] != ' ') n--; if (n == 0) n = cols; }
    if (draw) text_n(x, y + (float)(lines * 9), s, n, color, 1);
    lines++; s += n;
    while (*s == ' ') s++;
  }
  return lines;
}

// The question and its lettered answers, on a board across the sky. Once it is
// answered the right answer turns green and a wrong pick turns red.
static void draw_quiz(void) {
  if (!G.q_active && G.q_reveal <= 0) return;
  const Quiz *Q = quiz_now();
  rect_a(2, 17, 316, 80, 0x000018, 240);
  frame(2, 17, 316, 80, C_DPURP);
  int lines = text_wrap(8, 21, Q->q, 50, C_WHITE, true);
  float y = 21 + (float)(lines * 9) + 4;
  for (int i = 0; i < G.q_n; i++, y += 10) {
    bool right = G.q_perm[i] == 0, shown = !G.q_active;
    Uint32 color = !shown ? C_GREY : right ? C_ZOMBIE : i == G.q_pick ? C_RED : C_DGREY;
    char letter[2] = { (char)('A' + i), 0 };
    rect(8, y - 1, 9, 9, shown ? color : word_colors[i]);
    text(10, y, letter, C_BLACK, 1);
    text(22, y, Q->a[G.q_perm[i]], color, 1);
  }
}

static void draw_play(void) {
  const Rank *R = &ranks[G.rank];
  char buf[64];
  bool boss = G.sub == SUB_BOSS;
  float bob = 0;
  if (boss) { double b = (audio_song_time()) / (DANCE_STEP_SEC * 2); bob = (b - floor(b)) < 0.5 ? -2.0f : 0.0f; }

  SDL_RenderTexture(ren, bg_tex, NULL, NULL);
  spr(s_archer, 20 + (G.shoot_t > 0 ? -1.0f : 0.0f), G.archer_y - s_archer.h);
  draw_wall();
  for (int l = 0; l < LANES; l++)
    for (int i = 0; i < MAX_EN; i++)
      if (G.en[i].alive && G.en[i].lane == l) draw_enemy(&G.en[i], bob);
  if (boss) draw_boss();
  for (int i = 0; i < (int)SDL_arraysize(G.bolts); i++)
    if (G.bolts[i].on) spr(s_bolt[weapon_now()], G.bolts[i].x - 5, G.bolts[i].y - 1);
  for (int i = 0; i < (int)SDL_arraysize(G.parts); i++)
    if (G.parts[i].life > 0) rect(G.parts[i].x, G.parts[i].y, 2, 2, G.parts[i].color);
  if (!boss)
    for (int i = 0; i < MAX_EN; i++)
      if (G.en[i].alive && i != G.target) draw_label(&G.en[i], false);
  if (!boss && G.target >= 0 && G.en[G.target].alive) draw_label(&G.en[G.target], true);

  // top bar
  rect(0, 0, SCREEN_W, 15, C_BLACK); rect(0, 15, SCREEN_W, 1, C_DGREY);
  if (G.mode == MODE_DRIVE) SDL_snprintf(buf, sizeof(buf), "%s", R->name);   // the test has no ranks to number
  else SDL_snprintf(buf, sizeof(buf), "R%d %s", G.rank + 1, R->name);
  text(4, 4, buf, C_YELLOW, 1);
  if (G.mode == MODE_DRIVE) SDL_snprintf(buf, sizeof(buf), "RIGHT %d/%d", G.q_right, G.kills);
  else SDL_snprintf(buf, sizeof(buf), "%s %d/%d", G.mode == MODE_MATH ? "SOLVED" : "SLAIN", G.kills, R->quota);
  text(G.mode == MODE_TYPE ? 76.0f : 82.0f, 4, buf, C_WHITE, 1);
  SDL_snprintf(buf, sizeof(buf), "PTS %d", G.score);
  text(G.mode == MODE_TYPE ? 154.0f : 166.0f, 4, buf, C_CYAN, 1);
  for (int i = MAX_HP - G.hp_max; i < MAX_HP; i++) {
    if (i - (MAX_HP - G.hp_max) < G.hp) spr(s_heart, (float)(236 + i * 8), 5);
    else rect((float)(238 + i * 8), 7, 3, 2, C_DGREY);
  }

  if (G.mode == MODE_DRIVE && !boss) draw_quiz();
  if (!boss && G.mode != MODE_DRIVE) {   // current weapon, coins, and anything carried
    int w = weapon_now();
    SDL_snprintf(buf, sizeof(buf), "LV%d %s", w + 1, weapons[w].name);
    text_shadow(4, 20, buf, w ? weapons[w].trail : C_GREY, 1);
    SDL_snprintf(buf, sizeof(buf), "%d", G.cash);
    spr(s_coin, (float)(310 - text_w(buf, 1) - 9), 20);
    text_shadow((float)(310 - text_w(buf, 1)), 20, buf, C_YELLOW, 1);
    for (int k = 0; k < G.potions; k++) spr(s_potion, (float)(250 - text_w(buf, 1) - k * 10), 18);
    if (G.frost) spr(s_frost, 150, 19);
  }

  // bottom panel
  rect(0, 198, SCREEN_W, 42, C_BLACK); rect(0, 198, SCREEN_W, 1, C_DGREY);
  if (boss) {
    SDL_snprintf(buf, sizeof(buf), "HITS %d  MISSES %d", G.hits, G.misses);
    text_center(160, 206, buf, C_WHITE, 1);
    text_center(160, 220, "USE THE ARROW KEYS, ON THE BEAT", C_GREY, 1);
  } else if (G.sub == SUB_BOSS_OUT) {
    SDL_snprintf(buf, sizeof(buf), G.boss_won ? "%s IS BEATEN!" : "%s SMASHED THE WALL!", bosses[G.boss_kind].name);
    text_center(160, 204, buf, G.boss_won ? C_YELLOW : C_RED, 1);
    text_center(160, 216, "FINGERS BACK ON F AND J", C_WHITE, 1);
    char s[2] = { (char)('0' + (int)ceilf(G.out_t)), 0 };
    text_center(160, 227, s, C_CYAN, 1);
  } else if (G.mode == MODE_DRIVE) {
    SDL_snprintf(buf, sizeof(buf), "QUESTION %d OF %d", SDL_min(G.kills + (G.q_active ? 1 : 0), R->quota), R->quota);
    text_center(160, 204, buf, C_WHITE, 1);
    SDL_snprintf(buf, sizeof(buf), "MISSES LEFT: %d", G.hp - 1);
    text_center(160, 216, buf, G.hp > 2 ? C_GREY : C_RED, 1);
    text_center(160, 228, "PRESS THE LETTER OF THE RIGHT ANSWER", C_CYAN, 1);
  } else if (G.mode == MODE_TYPE && G.rank <= 2) draw_keyboard(next_char());
  else draw_typing_hud();

  if (G.msg_t > 0) text_center(160, 70, G.msg, G.msg_color, 2);
  if (G.sub == SUB_BOSS_OUT && G.sync_offer) {
    SDL_snprintf(buf, sizeof(buf), "PRESS D TO SET YOUR DELAY TO %+d MS", A.offset_ms + G.sync_offer);
    text_center(160, 92, buf, C_WHITE, 1);
  }
}

static void overlay(int alpha) { rect_a(0, 0, SCREEN_W, SCREEN_H, C_BLACK, alpha); }

static void draw_title(void) {
  SDL_RenderTexture(ren, bg_tex, NULL, NULL);
  overlay(150);
  text_center(160, 10, "HOME ROW", C_RED, 4);
  text_center(160, 42, "HORDE", C_YELLOW, 4);
  // the three games, side by side
  float tab = 34;
  for (int m = 0; m < NMODES; m++) {
    int w = text_w(mode_names[m], 1) + 12;
    if (m == G.mode) rect(tab, 76, (float)w, 13, C_YELLOW);
    else frame(tab, 76, (float)w, 13, C_DGREY);
    text(tab + 6, 79, mode_names[m], m == G.mode ? C_BLACK : C_GREY, 1);
    tab += (float)w + 26;
  }
  text(22, 79, "<", C_WHITE, 1); text(292, 79, ">", C_WHITE, 1);
  for (int i = 0; i < NRANKS; i++) {
    float y = (float)(98 + i * 12);
    bool on = i == G.sel;
    char buf[64];
    if (on) { rect_a(24, y - 2, 272, 11, C_DPURP, 255); text(28, y, ">", C_YELLOW, 1); }
    SDL_snprintf(buf, sizeof(buf), "%d %s", i + 1, ranks[i].name);
    text(40, y, buf, on ? C_YELLOW : C_GREY, 1);
    text(118, y, ranks[i].blurb, on ? C_WHITE : C_DGREY, 1);
  }
  if (G.mode == MODE_DRIVE) {
    text_center(160, 134, "PRACTICE QUESTIONS WRITTEN FROM THE", C_DGREY, 1);
    text_center(160, 144, "IDAHO DRIVER'S HANDBOOK, JULY 2026.", C_DGREY, 1);
    text_center(160, 154, "NOT THE OFFICIAL TEST. STUDY THE HANDBOOK.", C_DGREY, 1);
  }
  if (G.msg_t > 0) text_center(160, 200, G.msg, G.msg_color, 1);
  else text_center(160, 200, "LEFT / RIGHT: GAME   UP / DOWN: LEVEL   F7: LOAD", C_GREY, 1);
  if ((int)(G.time * 2) & 1) text_center(160, 214, "PRESS ENTER TO START", C_WHITE, 1);
#ifndef __EMSCRIPTEN__
  text_center(160, 228, "F2: CALIBRATE   F3: HIGH SCORES   F11: FULLSCREEN", C_DGREY, 1);
#else
  text_center(160, 228, "F2: CALIBRATE THE BEAT   F3: HIGH SCORES", C_DGREY, 1);
#endif
}

static void draw_scores(float y, int mode, int highlight) {
  char buf[48];
  for (int i = 0; i < NSCORES; i++) {
    const Score *sc = &HS.board[mode][i];
    bool on = i == highlight;
    if (sc->score > 0) SDL_snprintf(buf, sizeof(buf), "%d. %-8s %7d  R%d", i + 1, sc->name, sc->score, sc->rank);
    else SDL_snprintf(buf, sizeof(buf), "%d. --------", i + 1);
    if (on) rect_a(78, y + (float)(i * 10) - 2, 164, 10, C_DPURP, 255);
    text(84, y + (float)(i * 10), buf, on ? C_YELLOW : sc->score > 0 ? C_WHITE : C_DGREY, 1);
  }
}

static void draw_banner(void) {
  char buf[64];
  int total = G.correct + G.wrong, acc = total ? G.correct * 100 / total : 100;
  float avg = G.rank_clock > 1 ? (float)G.correct / 5.0f / (G.rank_clock / 60.0f) : 0;
  bool drive = G.mode == MODE_DRIVE;
  overlay(G.state == ST_RANKUP ? 170 : 205);
  if (G.state != ST_RANKUP) rect(0, 198, SCREEN_W, 42, C_BLACK);
  if (G.state == ST_RANKUP) {
    text_center(160, 50, "RANK UP!", C_YELLOW, 4);
    SDL_snprintf(buf, sizeof(buf), "NEXT RANK: %s", ranks[G.rank + 1].name);
    text_center(160, 90, buf, C_WHITE, 1);
    text_center(160, 102, ranks[G.rank + 1].blurb, C_CYAN, 1);
    if (G.mode == MODE_TYPE) SDL_snprintf(buf, sizeof(buf), "ACCURACY %d%%   SPEED %d WPM", acc, (int)(avg + 0.5f));
    else SDL_snprintf(buf, sizeof(buf), "ACCURACY %d%%", acc);
    text_center(160, 124, buf, C_GREY, 1);
    SDL_snprintf(buf, sizeof(buf), "SCORE %d   COINS %d", G.score, G.cash);
    text_center(160, 136, buf, C_CYAN, 1);
    text_center(160, 164, "ENTER: VISIT THE SHOP", C_WHITE, 1);
    return;
  }
  if (G.state == ST_VICTORY) {
    text_center(160, 20, drive ? "YOU PASSED!" : "DAWN BREAKS!", C_YELLOW, 3);
    if (drive) SDL_snprintf(buf, sizeof(buf), "%d OF %d CORRECT", G.q_right, G.kills);
    else SDL_snprintf(buf, sizeof(buf), "THE COMPOUND IS SAFE");
    text_center(160, 48, buf, C_WHITE, 2);
  } else if (G.quit) text_center(160, 30, "RUN OVER", C_SKIN, 3);
  else {
    text_center(160, 20, drive ? "TEST FAILED" : "THE WALL HAS", C_RED, 3);
    text_center(160, 46, drive ? "TOO MANY MISSES" : "FALLEN", C_RED, drive ? 2 : 3);
  }
  SDL_snprintf(buf, sizeof(buf), "SCORE %d   ACCURACY %d%%", G.score, acc);
  text_center(160, 76, buf, C_CYAN, 1);
  if (G.entering) {
    text_center(160, 100, "NEW HIGH SCORE!", C_YELLOW, 2);
    text_center(160, 126, "TYPE YOUR NAME, THEN PRESS ENTER", C_WHITE, 1);
    SDL_snprintf(buf, sizeof(buf), "%s%s", G.name, ((int)(G.time * 3) & 1) && strlen(G.name) < 8 ? "-" : " ");
    text_center(160, 146, buf, C_ZOMBIE, 3);
    return;
  }
  SDL_snprintf(buf, sizeof(buf), "HIGH SCORES: %s", mode_names[G.mode]);
  text_center(160, 94, buf, C_YELLOW, 1);
  draw_scores(108, G.mode, G.score_pos);
  if (G.state == ST_VICTORY) text_center(160, 214, "ENTER: BACK TO THE TITLE", C_WHITE, 1);
  else {
    text_center(160, 204, drive ? "ENTER: TAKE THE TEST AGAIN" : "ENTER: TRY THIS RANK AGAIN", C_WHITE, 1);
    text_center(160, 216, "ESC: BACK TO THE TITLE", C_GREY, 1);
  }
}

// The high-score board on its own, from the title screen.
static void draw_scores_screen(void) {
  char buf[48];
  SDL_RenderTexture(ren, bg_tex, NULL, NULL);
  overlay(200);
  text_center(160, 20, "HIGH SCORES", C_YELLOW, 3);
  SDL_snprintf(buf, sizeof(buf), "< %s >", mode_names[G.mode]);
  text_center(160, 56, buf, C_WHITE, 2);
  draw_scores(90, G.mode, -1);
  text_center(160, 200, "LEFT / RIGHT: OTHER GAMES", C_GREY, 1);
  text_center(160, 214, "ESC: BACK TO THE TITLE", C_GREY, 1);
}

// The shop: a merchant between two fires, with wares on the counter.
static void draw_shop(void) {
  char buf[64];
  const char *line1 = "", *line2 = "";
  char l2[48] = "";
  rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK);
  for (int y = 0; y < SCREEN_H; y += 8)       // brick walls on three sides
    for (int x = -8 + (y / 8 % 2) * 8; x < SCREEN_W; x += 16) {
      if (y >= 16 && x >= 16 && x < SCREEN_W - 24) continue;
      rect((float)x, (float)y, 15, 7, y < 16 ? 0x7C2800 : 0x582000);
    }
  rect(128, 232, 64, 8, C_BLACK);             // the way out
  text_center(160, 24, "GOOD COIN BUYS GOOD STEEL, TRAVELER.", C_WHITE, 1);
  SDL_FRect m = { 144, 40, 32, 38 };
  SDL_RenderTexture(ren, s_merchant.t, NULL, &m);
  for (int k = 0; k < 2; k++) {               // the two fires
    float fx = k ? 216.0f : 92.0f;
    bool f = ((int)(G.time * 8) + k) % 2;
    rect(fx, 70, 12, 4, C_DGREY); rect(fx + 2, 74, 8, 3, C_DGREY);
    rect(fx + 1, f ? 56.0f : 59.0f, 10, f ? 14.0f : 11.0f, C_RED);
    rect(fx + 3, f ? 60.0f : 62.0f, 6, f ? 10.0f : 8.0f, C_YELLOW);
  }
  for (int slot = 0; slot < SHOP_SLOTS; slot++) {
    float cx = 56.0f + 68.0f * (float)slot;
    bool on = slot == G.shop_sel;
    const char *n1 = "", *n2 = "";
    int price = -1;
    char w1[16] = "", w2[16] = "";
    if (slot == SHOP_WEAPON) {
      int next = G.weapon + 1 < NWEAPONS ? G.weapon + 1 : G.weapon;
      SDL_sscanf(weapons[next].name, "%15s %15s", w1, w2);
      n1 = w1; n2 = w2;
      SDL_FRect d = { cx - 15, 108, 30, 9 };
      SDL_RenderTexture(ren, s_bolt[next].t, NULL, &d);
      if (G.weapon + 1 < NWEAPONS) price = weapons[next].price;
      if (on) {
        if (price < 0) line1 = "YOU HOLD THE FINEST WEAPON THERE IS.";
        else if (weapons[next].blast) { line1 = "ITS BLAST DESTROYS MONSTERS NEAR THE ONE YOU HIT."; SDL_snprintf(l2, sizeof(l2), "LEVEL %d. BLAST REACH %d.", next + 1, weapons[next].blast); }
        else { line1 = "KNOCKS BACK MONSTERS NEAR THE ONE YOU HIT."; SDL_snprintf(l2, sizeof(l2), "LEVEL %d.", next + 1); }
      }
    } else if (slot == SHOP_POTION) {
      n1 = "WALL"; n2 = "POTION";
      SDL_FRect d = { cx - 8, 102, 16, 20 };
      SDL_RenderTexture(ren, s_potion.t, NULL, &d);
      if (G.potions < POTION_MAX) price = POTION_PRICE;
      if (on) { line1 = "IF THE WALL FALLS, IT IS REBUILT TO 5 HEARTS."; SDL_snprintf(l2, sizeof(l2), "YOU CARRY %d OF %d.", G.potions, POTION_MAX); }
    } else if (slot == SHOP_FROST) {
      n1 = "FROST"; n2 = "CHARM";
      SDL_FRect d = { cx - 9, 103, 18, 18 };
      SDL_RenderTexture(ren, s_frost.t, NULL, &d);
      if (!G.frost_next) price = FROST_PRICE;
      if (on) line1 = "MONSTERS WALK SLOWER FOR THE WHOLE NEXT RANK.";
    } else {
      n1 = "HEAD"; n2 = "OUT";
      rect(cx - 8, 100, 16, 22, C_BROWN); rect(cx - 6, 103, 12, 19, C_BLACK); rect(cx + 2, 111, 2, 2, C_YELLOW);
      if (on) { line1 = "BACK TO THE WALL."; SDL_snprintf(l2, sizeof(l2), "RANK %d AWAITS.", G.shop_next + 1); }
    }
    if (on) frame(cx - 31, 96, 62, 66, ((int)(G.time * 4) & 1) ? C_YELLOW : C_SKIN);
    text_center(cx, 128, n1, on ? C_WHITE : C_GREY, 1);
    text_center(cx, 137, n2, on ? C_WHITE : C_GREY, 1);
    if (slot != SHOP_LEAVE) {
      if (price < 0) text_center(cx, 150, "SOLD", C_DGREY, 1);
      else {
        SDL_snprintf(buf, sizeof(buf), "%d", price);
        spr(s_coin, cx - text_w(buf, 1) / 2.0f - 5, 150);
        text(SDL_floorf(cx - text_w(buf, 1) / 2.0f) + 4, 150, buf, G.cash >= price ? C_YELLOW : C_RED, 1);
      }
    }
  }
  if (l2[0]) line2 = l2;
  if (G.msg_t > 0) text_center(160, 172, G.msg, G.msg_color, 2);
  else { text_center(160, 170, line1, C_CYAN, 1); text_center(160, 181, line2, C_GREY, 1); }
  SDL_snprintf(buf, sizeof(buf), "YOUR COINS: %d", G.cash);
  text_center(160, 198, buf, C_YELLOW, 1);
  text_center(160, 211, "LEFT / RIGHT: CHOOSE   ENTER: BUY", C_GREY, 1);
  text_center(160, 222, "ESC: HEAD OUT", C_GREY, 1);
}

static void update_status(void) {
  char buf[96];
  const Rank *R = &ranks[G.rank];
  if (G.state == ST_TITLE) SDL_snprintf(buf, sizeof(buf), "Home Row Horde. Press Enter to start.");
  else if (G.state == ST_CALIBRATE) SDL_snprintf(buf, sizeof(buf), "Beat calibration, %s step. Tap space on every beat. Sound %d ms, sight %d ms.", Cal.mode == CAL_SIGHT ? "sight" : "sound", A.offset_ms, A.visual_ms);
  else if (G.state == ST_SHOP) SDL_snprintf(buf, sizeof(buf), "Shop. %d coins. Weapon level %d. Enter buys, Escape heads out.", G.cash, G.weapon + 1);
  else if (G.state == ST_SCORES && HS.board[G.mode][0].score > 0) SDL_snprintf(buf, sizeof(buf), "High scores, %s. 1. %s %d.", mode_names[G.mode], HS.board[G.mode][0].name, HS.board[G.mode][0].score);
  else if (G.state == ST_SCORES) SDL_snprintf(buf, sizeof(buf), "High scores, %s. No scores yet.", mode_names[G.mode]);
  else if ((G.state == ST_GAMEOVER || G.state == ST_VICTORY) && G.entering) SDL_snprintf(buf, sizeof(buf), "New high score! Type your name and press Enter.");
  else if ((G.state == ST_GAMEOVER || G.state == ST_VICTORY) && G.score_pos >= 0) SDL_snprintf(buf, sizeof(buf), "High score saved: %s %d. Press Enter.", G.name, G.score);
  else if (G.state == ST_PLAY && G.paused) SDL_snprintf(buf, sizeof(buf), "Paused. Press Enter to keep playing.");
  else if (G.state == ST_GAMEOVER) SDL_snprintf(buf, sizeof(buf), G.mode == MODE_DRIVE ? "Test failed: too many misses. Press Enter to try again." : "The wall has fallen. Press Enter to try again.");
  else if (G.state == ST_VICTORY && G.mode == MODE_DRIVE) SDL_snprintf(buf, sizeof(buf), "You passed! %d of %d correct.", G.q_right, G.kills);
  else if (G.state == ST_VICTORY) SDL_snprintf(buf, sizeof(buf), "Dawn breaks. You win! Score %d.", G.score);
  else if (G.state == ST_RANKUP) SDL_snprintf(buf, sizeof(buf), "Rank up! Press Enter for rank %d.", G.rank + 2);
  else if (G.sub == SUB_BOSS) SDL_snprintf(buf, sizeof(buf), "Boss battle: hit the arrow keys on the beat.");
  else if (G.mode == MODE_DRIVE)
    SDL_snprintf(buf, sizeof(buf), "Driver's test. Question %d of %d. Right so far %d.", SDL_min(G.kills + (G.q_active ? 1 : 0), R->quota), R->quota, G.q_right);
  else if (G.mode == MODE_MATH) {
    int i = G.sub == SUB_TYPING ? (G.target >= 0 ? G.target : nearest_enemy()) : -1;
    if (i >= 0) SDL_snprintf(buf, sizeof(buf), "Math rank %d %s. Solved %d/%d. Solve %s", G.rank + 1, R->name, G.kills, R->quota, G.en[i].q);
    else SDL_snprintf(buf, sizeof(buf), "Math rank %d %s. Solved %d/%d.", G.rank + 1, R->name, G.kills, R->quota);
  } else {
    char c = G.sub == SUB_TYPING ? next_char() : 0;
    if (c == ' ') SDL_snprintf(buf, sizeof(buf), "Rank %d %s. Slain %d/%d. Press the space bar.", G.rank + 1, R->name, G.kills, R->quota);
    else if (c) SDL_snprintf(buf, sizeof(buf), "Rank %d %s. Slain %d/%d. Type %c", G.rank + 1, R->name, G.kills, R->quota, c);
    else SDL_snprintf(buf, sizeof(buf), "Rank %d %s. Slain %d/%d.", G.rank + 1, R->name, G.kills, R->quota);
  }
  set_status(buf);
}

static void draw(void) {
  SDL_SetRenderTarget(ren, screen_tex);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (G.state == ST_TITLE) draw_title();
  else if (G.state == ST_CALIBRATE) draw_calibrate();
  else if (G.state == ST_SHOP) draw_shop();
  else if (G.state == ST_SCORES) draw_scores_screen();
  else {
    draw_play();
    if (G.state != ST_PLAY) draw_banner();
    else if (G.paused) {
      overlay(170);
      text_center(160, 70, "PAUSED", C_YELLOW, 4);
      text_center(160, 124, "ENTER OR ESC: KEEP PLAYING", C_WHITE, 1);
      text_center(160, 138, "Q: QUIT TO THE TITLE", C_GREY, 1);
      text_center(160, 152, "F6: SAVE THE GAME   F7: LOAD IT", C_GREY, 1);
      if (G.msg_t > 0) text_center(160, 100, G.msg, G.msg_color, 1);
      char buf[48];
      SDL_snprintf(buf, sizeof(buf), "< > BEAT TIMING: %+d MS", A.offset_ms);
      text_center(160, 166, buf, C_CYAN, 1);
      text_center(160, 180, "ARROWS REACH THE TOP BEFORE THE BEAT? PRESS >", C_GREY, 1);
      text_center(160, 191, "OR PRESS F2 ON THE TITLE SCREEN TO MEASURE IT", C_GREY, 1);
    }
  }

  SDL_SetRenderTarget(ren, NULL);
  int ow, oh;
  SDL_GetRenderOutputSize(ren, &ow, &oh);
#ifdef __EMSCRIPTEN__
  // The page sizes the canvas, so fill it exactly; CSS keeps the pixels crisp.
  float scale = SDL_min((float)ow / SCREEN_W, (float)oh / SCREEN_H);
#else
  int scale = SDL_min(ow / SCREEN_W, oh / SCREEN_H);
  if (scale < 1) scale = 1;
#endif
  float sx = G.shake > 0 ? (float)(rndi(3) - 1) * scale : 0, sy = G.shake > 0 ? (float)(rndi(3) - 1) * scale : 0;
  SDL_FRect dst = { (ow - SCREEN_W * scale) / 2.0f + sx, (oh - SCREEN_H * scale) / 2.0f + sy,
                    (float)(SCREEN_W * scale), (float)(SCREEN_H * scale) };
  set_color(C_BLACK, 255);
  SDL_RenderClear(ren);
  SDL_RenderTexture(ren, screen_tex, NULL, &dst);
}

// ---- input ---------------------------------------------------------------------------
static void key_down(SDL_Keycode key) {
  bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE;
  if (key == SDLK_F6) { save_game(); return; }
  if (key == SDLK_F7) { load_game(); return; }
  if ((G.state == ST_GAMEOVER || G.state == ST_VICTORY) && G.entering) { name_key(key); return; }
  switch (G.state) {
    case ST_CALIBRATE: {
      bool sight = Cal.mode == CAL_SIGHT;
      int step = key == SDLK_MINUS || key == SDLK_KP_MINUS ? -2 : key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS ? 2 : 0;
      if (key == SDLK_ESCAPE) go_title();
      else if (key == SDLK_R) calibrate_mode(Cal.mode);
      else if (key == SDLK_TAB) calibrate_mode(sight ? CAL_SOUND : CAL_SIGHT);
      else if (key == SDLK_A) { autosync = !autosync; settings_save(); }
      else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (cal_count() >= 8) {
          sfx(WAVE_SQUARE, 1320, 0, 0.12f, 0.1f, 0);
          if (sight) { visual_set(cal_result()); calibrate_mode(CAL_SIGHT); }   // stay, so the result can be checked
          else { offset_set(cal_result()); calibrate_mode(CAL_SIGHT); }
        }
      }
      else if (step && sight) visual_set(A.visual_ms + step);
      else if (step) offset_change(step);
      else if (key == SDLK_SPACE || key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) calibrate_tap();
      break;
    }
    case ST_SHOP:
      if (key == SDLK_LEFT) { G.shop_sel = (G.shop_sel + SHOP_SLOTS - 1) % SHOP_SLOTS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key == SDLK_RIGHT) { G.shop_sel = (G.shop_sel + 1) % SHOP_SLOTS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key >= SDLK_1 && key < SDLK_1 + SHOP_SLOTS) G.shop_sel = (int)(key - SDLK_1);
      else if (enter && G.state_t > 0.3f) shop_buy(G.shop_sel);
      else if (key == SDLK_ESCAPE) shop_buy(SHOP_LEAVE);
      break;
    case ST_SCORES:
      if (key == SDLK_LEFT || key == SDLK_RIGHT) G.mode = (G.mode + (key == SDLK_RIGHT ? 1 : NMODES - 1)) % NMODES;
      else if (key == SDLK_ESCAPE || enter || key == SDLK_F3) { G.sel = 0; go_title(); }
      break;
    case ST_TITLE:
      if (key == SDLK_F2) { calibrate_start(); break; }
      if (key == SDLK_F3) { G.state = ST_SCORES; break; }
      if (key == SDLK_LEFT || key == SDLK_RIGHT) {   // switch between typing, math, and the driver's test
        G.mode = (G.mode + (key == SDLK_RIGHT ? 1 : NMODES - 1)) % NMODES; G.sel = 0;
        sfx(WAVE_SQUARE, 880, 0, 0.04f, 0.08f, 0);
        break;
      }
      if (key == SDLK_UP) { G.sel = (G.sel + NRANKS - 1) % NRANKS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key == SDLK_DOWN) { G.sel = (G.sel + 1) % NRANKS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key >= SDLK_1 && key < SDLK_1 + NRANKS) G.sel = (int)(key - SDLK_1);
      else if (enter) new_game(G.sel);
      break;
    case ST_PLAY:
      if (G.paused) {
        if (enter || key == SDLK_ESCAPE) set_paused(false);
        else if (key == SDLK_Q) quit_run();
        else if (key == SDLK_LEFT) offset_change(-10);
        else if (key == SDLK_RIGHT) offset_change(10);
        break;
      }
      if (key == SDLK_ESCAPE) { set_paused(true); break; }
      if (G.sub == SUB_BOSS) {
        if (key == SDLK_A || key == SDLK_LEFT) boss_press(0);
        else if (key == SDLK_S || key == SDLK_DOWN) boss_press(1);
        else if (key == SDLK_W || key == SDLK_UP) boss_press(2);
        else if (key == SDLK_D || key == SDLK_RIGHT) boss_press(3);
      } else if (G.sub == SUB_BOSS_OUT && key == SDLK_D && G.sync_offer) {
        // Fold the battle's average timing into the sound delay; offset_set also saves it.
        char buf[40];
        offset_set(A.offset_ms + G.sync_offer);
        G.sync_offer = 0;
        SDL_snprintf(buf, sizeof(buf), "DELAY SAVED: %+d MS", A.offset_ms);
        say(buf, C_ZOMBIE, 2.0f);
        sfx(WAVE_SQUARE, 1320, 0, 0.12f, 0.1f, 0);
      } else if (G.sub == SUB_TYPING) {
        if (key >= SDLK_A && key <= SDLK_Z) type_char((char)('A' + (key - SDLK_A)));
        else if (key >= SDLK_0 && key <= SDLK_9) type_char((char)('0' + (key - SDLK_0)));
        else if (key >= SDLK_KP_1 && key <= SDLK_KP_9) type_char((char)('1' + (key - SDLK_KP_1)));
        else if (key == SDLK_KP_0) type_char('0');
        else if (key == SDLK_SPACE) type_char(' ');
        else if (key == SDLK_BACKSPACE) G.target = -1;
      }
      break;
    case ST_RANKUP:
      if (enter && G.state_t > 0.5f) enter_shop(G.rank + 1);
      else if (key == SDLK_ESCAPE) go_title();
      break;
    case ST_GAMEOVER:
      if (enter && G.state_t > 0.5f) new_game(G.rank);
      else if (key == SDLK_ESCAPE) go_title();
      break;
    case ST_VICTORY:
      if ((enter && G.state_t > 0.5f) || key == SDLK_ESCAPE) go_title();
      break;
  }
}

// ---- SDL callbacks ---------------------------------------------------------------------
SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  (void)appstate; (void)argc; (void)argv;
#ifndef __EMSCRIPTEN__
  T.shot = getenv("HRH_SHOT");
  if (getenv("HRH_FRAMES")) T.frames = atoi(getenv("HRH_FRAMES"));
  if (getenv("HRH_BOT")) T.bot = (float)atof(getenv("HRH_BOT"));
  T.on = T.frames > 0;
#endif
  SDL_SetAppMetadata("Home Row Horde", "1.0", "org.vibecda.homerowhorde");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return SDL_APP_FAILURE; }
  if (!SDL_CreateWindowAndRenderer("Home Row Horde", SCREEN_W * 3, SCREEN_H * 3, SDL_WINDOW_RESIZABLE, &win, &ren)) {
    SDL_Log("Could not create a window: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_SetRenderVSync(ren, 1);
  screen_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, SCREEN_W, SCREEN_H);
  SDL_SetTextureScaleMode(screen_tex, SDL_SCALEMODE_NEAREST);

  font_init();
  s_zombie[0][0] = SPRITE(zombie_a, NULL);   s_zombie[0][1] = SPRITE(zombie_b, NULL);
  s_zombie[1][0] = SPRITE(zombie_a, "PBpn"); s_zombie[1][1] = SPRITE(zombie_b, "PBpn");
  s_zombie[2][0] = SPRITE(zombie_a, "PRpr"); s_zombie[2][1] = SPRITE(zombie_b, "PRpr");
  s_bat[0] = SPRITE(bat_a, NULL); s_bat[1] = SPRITE(bat_b, NULL);
  s_brute = SPRITE(brute, NULL); s_archer = SPRITE(archer, NULL);
  s_lich = SPRITE(lich, NULL);
  s_merchant = SPRITE(merchant, NULL); s_potion = SPRITE(potion, NULL); s_frost = SPRITE(frost, NULL); s_coin = SPRITE(coin, NULL);
  s_bolt[0] = SPRITE(bolt, "RgYG");       // oak arrow, plain iron tip
  s_bolt[1] = SPRITE(bolt, NULL);         // fire arrow
  s_bolt[2] = SPRITE(bolt, "oGRWYW");     // silver bolt
  s_bolt[3] = SPRITE(bolt, "oYRWYWWY");   // thunder bolt
  s_bolt[4] = SPRITE(bolt, "oCRWYCWC");   // storm bolt
  s_bolt[5] = SPRITE(bolt, "oRRYYYWR");   // dragon flame
  s_bolt[6] = SPRITE(bolt, "oMRWYWWM");   // holy nova
  s_tomb = SPRITE(tombstone, NULL); s_heart = SPRITE(heart, NULL); s_note = SPRITE(note_arrow, NULL);
  build_background();

  // Seed from the clock so that every session differs (question order, words,
  // bosses). Headless test runs keep the fixed seed and stay repeatable, unless
  // HRH_SEED asks for a particular one.
  if (!T.on) {
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    seed_rng((Uint32)now ^ (Uint32)(now >> 32) ^ (Uint32)SDL_GetPerformanceCounter());
  }
#ifndef __EMSCRIPTEN__
  else if (getenv("HRH_SEED")) seed_rng((Uint32)strtoul(getenv("HRH_SEED"), NULL, 10));
#endif
  audio_init(T.on);
  settings_load();
  scores_load();
  go_title();
#ifndef __EMSCRIPTEN__
  if (getenv("HRH_MODE")) G.mode = SDL_clamp(atoi(getenv("HRH_MODE")) - 1, 0, NMODES - 1);
  if (getenv("HRH_RANK")) new_game(SDL_clamp(atoi(getenv("HRH_RANK")) - 1, 0, NRANKS - 1));
  if (getenv("HRH_SHOP")) { G.cash = atoi(getenv("HRH_SHOP")); G.weapon = 2; G.potions = 1; enter_shop(3); }
  if (getenv("HRH_WEAPON")) G.weapon = SDL_clamp(atoi(getenv("HRH_WEAPON")) - 1, 0, NWEAPONS - 1);
  if (getenv("HRH_CALIB")) {   // show the calibration screen with some sample taps
    calibrate_start();
    if (atoi(getenv("HRH_CALIB")) == 2) calibrate_mode(CAL_SIGHT);
    for (Cal.n = 0; Cal.n < 10; Cal.n++) Cal.err[Cal.n] = 0.024f + 0.004f * (float)(Cal.n % 4);
    Cal.last = 0.031f;
  }
  if (getenv("HRH_BOSS") && G.state == ST_PLAY) { G.boss_timer = 0.5f; force_boss = atoi(getenv("HRH_BOSS")) - 1; }
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--fullscreen")) SDL_SetWindowFullscreen(win, true);
#endif
  last_ns = SDL_GetTicksNS();
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  (void)appstate;
  if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
#ifndef __EMSCRIPTEN__
  // F11 or Alt+Enter toggles fullscreen. On the web, the page's button does this.
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
      (event->key.key == SDLK_F11 || (event->key.key == SDLK_RETURN && (event->key.mod & SDL_KMOD_ALT)))) {
    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
    return SDL_APP_CONTINUE;
  }
#endif
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) { key_ts = event->key.timestamp; key_down(event->key.key); key_ts = 0; }
  // Clicking away or switching tabs pauses, so the horde never advances unseen.
  if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST && G.state == ST_PLAY && !G.paused && !T.on) set_paused(true);
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;
  Uint64 now = SDL_GetTicksNS();
  float dt = (float)(now - last_ns) / 1e9f;
  last_ns = now;
  if (dt > 0.05f) dt = 0.05f;
  if (T.on) dt = 1.0f / 60.0f;

  if (!G.paused) { audio_pump(dt); update(dt); }
  update_status();
  draw();

  if (T.on && ++T.frame >= T.frames) {
    if (T.shot) {
      SDL_SetRenderTarget(ren, screen_tex);
      SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
      if (s) { SDL_SaveBMP(s, T.shot); SDL_DestroySurface(s); }
      SDL_SetRenderTarget(ren, NULL);
    }
    printf("state=%d sub=%d rank=%d kills=%d hp=%d score=%d correct=%d wrong=%d wpm=%.0f hits=%d misses=%d\n",
           G.state, G.sub, G.rank + 1, G.kills, G.hp, G.score, G.correct, G.wrong, G.wpm, G.hits, G.misses);
    return SDL_APP_SUCCESS;
  }
  SDL_RenderPresent(ren);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) { (void)appstate; (void)result; }
