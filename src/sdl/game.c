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
#define RECEPTOR_Y 170

typedef struct {
  const char *name, *blurb;
  int quota;          // zombies to defeat
  float speed;        // pixels per second
  float interval;     // seconds between spawns
  int max_alive;
  int wpm;            // moving-average WPM to hold, 0 for none
  int chart;          // dance difficulty 0..3
} Rank;
static const Rank ranks[] = {
  { "SPROUT",  "F AND J, THEN HOME ROW", 12,  6, 4.0f, 2,  0, 0 },
  { "SCOUT",   "HOME ROW AND TOP ROW",   16,  8, 3.2f, 3,  0, 0 },
  { "SQUIRE",  "EVERY LETTER",           20, 10, 2.6f, 3,  0, 1 },
  { "ARCHER",  "SHORT WORDS",            16,  9, 3.2f, 3,  0, 1 },
  { "KNIGHT",  "LONGER WORDS, TOUGH BRUTES",  18, 10, 3.0f, 4,  0, 2 },
  { "CAPTAIN", "TWO-WORD FOES. KEEP 15 WPM",  20, 11, 2.6f, 4, 15, 2 },
  { "WARDEN",  "PHRASES. KEEP 25 WPM",        24, 11, 2.2f, 5, 25, 3 },
  { "LEGEND",  "LONG PHRASES. KEEP 40 WPM",   30, 11, 1.8f, 5, 40, 3 },
};
#define NRANKS ((int)SDL_arraysize(ranks))

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
enum { ST_TITLE, ST_PLAY, ST_RANKUP, ST_GAMEOVER, ST_VICTORY };
enum { SUB_TYPING, SUB_BOSS, SUB_BOSS_OUT };
enum { K_ZOMBIE, K_BAT, K_BRUTE };

typedef struct {
  bool alive, doomed;
  int kind, lane, variant, len, typed;
  int hp, words;       // words still to type, and words in total
  float x, rise, anim, atk, dying, hurt;
  char word[28];
} Enemy;
typedef struct { bool on; float x, y; int target; bool at_boss, wound; } Bolt;
typedef struct { float x, y, vx, vy, life; Uint32 color; } Particle;
typedef struct { int step, col, state; } Note;   // state: 0 pending, 1 hit, 2 missed

static struct {
  int state, sub, rank, sel;
  bool paused;
  int hp, kills, spawned, score, streak, correct, wrong, target, last_lane;
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
  float boss_x, boss_flash, press_flash[4], out_t;
  bool boss_won;
  int boss_kind;
  char judge[12]; float judge_t; Uint32 judge_color;
} G;

static SDL_Window *win;
static SDL_Texture *screen_tex, *bg_tex;
static Spr s_zombie[3][2], s_bat[2], s_brute, s_lich, s_archer, s_bolt[4], s_tomb, s_heart, s_note;

// The archer's weapon is earned by typing without a mistake: each streak length
// below unlocks the next one, and a wrong key drops back to oak arrows. Better
// weapons fly faster and their impact knocks back monsters near the one hit.
typedef struct { const char *name; int streak; float speed; int splash, push; Uint32 trail; } Weapon;
static const Weapon weapons[] = {
  { "OAK ARROWS",    0,  520,  0,  0, 0 },
  { "FIRE ARROWS",  10,  560, 36, 10, C_SKIN },
  { "SILVER BOLTS", 25,  800, 44, 14, C_WHITE },
  { "STORM STAFF",  50, 1400, 60, 18, C_CYAN },
};
#define NWEAPONS ((int)SDL_arraysize(weapons))
static int weapon_now(void) {
  int w = 0;
  while (w + 1 < NWEAPONS && G.streak >= weapons[w + 1].streak) w++;
  return w;
}

// Boss monsters for the W A S D battles. Each one plays a little differently.
typedef struct {
  const char *name, *hint;
  Spr *frames[2];
  int scale;
  bool hover;
  float note_speed;   // pixels per second
  int need_pct;       // share of the notes that must be hit to win
  int reveal_y;       // notes stay hidden above this line
  Uint32 color;
} Boss;
static const Boss bosses[] = {
  { "PUMPKIN KING", "HIT THE ARROWS ON THE BEAT", { &s_brute, &s_brute },               2, false,  90, 60, 32, C_SKIN },
  { "BAT QUEEN",    "HER ARROWS FLY FAST",        { &s_bat[0], &s_bat[1] },             3, true,  125, 55, 32, C_PURPLE },
  { "ROTTEN GIANT", "TOUGH HIDE: MISS VERY FEW",  { &s_zombie[2][0], &s_zombie[2][1] }, 3, false,  80, 70, 32, C_ZOMBIE },
  { "THE LICH",     "HIS ARROWS APPEAR LATE",     { &s_lich, &s_lich },                 2, false, 100, 60, 96, C_CYAN },
};
#define NBOSSES ((int)SDL_arraysize(bosses))
static int last_boss = -1, force_boss = -1;
static Uint64 last_ns;
static Uint32 rng_state = 0xC0FFEE;

// Headless test hooks (native only): HRH_SHOT=file.bmp HRH_FRAMES=n HRH_RANK=1..8
// HRH_BOT=keys-per-second HRH_BOSS=1
static struct { const char *shot; int frames, frame; float bot, bot_t; bool on; } T;

static Uint32 rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
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

static void pick_word(Enemy *e) {
  int r = G.rank;
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
  G.kills++;
  G.score += 10 * e->len;
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

// Upgraded weapons stagger the monsters around the one they hit.
static void splash(const Enemy *hit) {
  const Weapon *w = &weapons[weapon_now()];
  if (!w->splash) return;
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *o = &G.en[i];
    if (!o->alive || o->doomed || o == hit || o->rise < 1) continue;
    if (SDL_fabsf(o->x - hit->x) > w->splash || abs(o->lane - hit->lane) > 1) continue;
    o->x = SDL_min(o->x + w->push, 300.0f);
    puff(o->x, enemy_y(o), w->trail, 2);
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

static void type_char(char c) {
  Enemy *e = NULL;
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
    if (weapon_now() > 0) say("STREAK BROKEN: OAK ARROWS", C_RED, 1.2f);
    G.wrong++; G.streak = 0;
    sfx(WAVE_SQUARE, 110, 0, 0.09f, 0.12f, 0);
    return;
  }
  e->typed++;
  int had = weapon_now();
  G.correct++; G.streak++;
  if (weapon_now() > had) {
    char buf[40];
    SDL_snprintf(buf, sizeof(buf), "%s!", weapons[weapon_now()].name);
    say(buf, C_YELLOW, 1.4f);
    for (int k = 0; k < 3; k++) sfx(WAVE_SQUARE, midi_hz(76 + k * 5), 0, 0.09f, 0.10f, k * 0.07f);
  }
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
static const double boss_angles[4] = { -90, 180, 0, 90 };
static const Uint32 boss_colors[4] = { C_PINK, C_CYAN, C_ZOMBIE, C_YELLOW };

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
  G.hits = G.misses = G.combo = 0; G.boss_x = 350; G.boss_won = false; G.judge_t = 0;
  audio_song(SONG_DANCE);
  sfx(WAVE_SQUARE, 110, 0, 0.5f, 0.12f, 0);
  sfx(WAVE_SQUARE, 104, 0, 0.5f, 0.12f, 0);
}

static void boss_end(bool won) {
  G.boss_won = won; G.sub = SUB_BOSS_OUT; G.out_t = 3.0f;
  audio_song(SONG_TYPING);
  if (won) {
    G.score += 500;
    puff(G.boss_x, boss_cy(), bosses[G.boss_kind].color, 30); puff(G.boss_x, boss_cy() - 20, C_YELLOW, 20);
    for (int i = 0; i < MAX_EN; i++) if (G.en[i].alive && !G.en[i].doomed) kill_enemy(&G.en[i]);
    G.hp = G.hp + 2 > MAX_HP ? MAX_HP : G.hp + 2;
    for (int i = 0; i < 4; i++) sfx(WAVE_SQUARE, midi_hz(69 + i * 4 + (i == 3 ? 0 : 0)), 0, 0.12f, 0.12f, i * 0.1f);
  } else {
    hurt_wall(2, false);
    sfx(WAVE_SQUARE, 220, -150, 0.6f, 0.12f, 0);
  }
  G.boss_timer = BOSS_MIN_GAP + rndf() * BOSS_RANDOM_GAP;
}

static void boss_judge(const char *label, Uint32 color) {
  SDL_strlcpy(G.judge, label, sizeof(G.judge)); G.judge_t = 0.5f; G.judge_color = color;
}

static void boss_press(int col) {
  if (G.sub != SUB_BOSS) return;
  G.press_flash[col] = 0.12f;
  double t = audio_song_time();
  double good = G.rank < 3 ? 0.20 : 0.14;
  for (int i = 0; i < G.nnotes; i++) {
    Note *n = &G.notes[i];
    if (n->state || n->col != col) continue;
    double d = fabs(note_time(n) - t);
    if (d > good) continue;
    n->state = 1; G.hits++; G.combo++;
    bool perfect = d < 0.07;
    G.score += perfect ? 60 : 30;
    boss_judge(perfect ? "PERFECT" : "GOOD", perfect ? C_YELLOW : C_ZOMBIE);
    sfx(WAVE_SQUARE, perfect ? 1320 : 990, 0, 0.05f, 0.07f, 0);
    if (G.boss_hp > 0) { G.boss_hp--; fire(G.boss_x, boss_cy(), -1, true); }
    return;
  }
}

static void boss_update(float dt) {
  double t = audio_song_time();
  if (G.boss_x > 274) { G.boss_x -= 40 * dt; if (G.boss_x < 274) G.boss_x = 274; }
  for (int i = 0; i < G.nnotes; i++) {
    Note *n = &G.notes[i];
    if (n->state == 0 && t > note_time(n) + (G.rank < 3 ? 0.20 : 0.14)) {
      n->state = 2; G.misses++; G.combo = 0;
      boss_judge("MISS", C_RED);
      if (G.rank >= 3 && G.misses % 8 == 0) hurt_wall(1, false);
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
  int score = G.score, sel = G.sel, streak = G.streak;
  SDL_memset(&G, 0, sizeof(G));
  G.state = ST_PLAY; G.rank = r; G.sel = sel; G.score = score; G.streak = streak;
  G.hp = MAX_HP; G.target = -1; G.last_lane = -1; G.ward = 1;
  G.spawn_t = 1.0f; G.archer_y = (float)LANE_Y(1);
  G.boss_timer = boss_timer > 0 ? boss_timer : BOSS_MIN_GAP + rndf() * BOSS_RANDOM_GAP;
  audio_song(SONG_TYPING);
}

static void new_game(int r) { G.score = 0; G.streak = 0; G.boss_timer = 0; start_rank(r); }

// Pausing stops the audio device too, so the song clock that judges boss notes
// freezes with the game and picks up exactly where it left off.
static void set_paused(bool paused) {
  G.paused = paused;
  if (A.stream) { if (paused) SDL_PauseAudioStreamDevice(A.stream); else SDL_ResumeAudioStreamDevice(A.stream); }
}

// The beat-timing adjustment is remembered between visits on the web.
static void offset_change(int delta) {
  A.offset_ms = SDL_clamp(A.offset_ms + delta, -250, 250);
#ifdef __EMSCRIPTEN__
  EM_ASM({ try { localStorage.setItem('hrh-beat-offset', String($0)); } catch (e) {} }, A.offset_ms);
#endif
}
static void offset_load(void) {
#ifdef __EMSCRIPTEN__
  A.offset_ms = EM_ASM_INT({ try { return parseInt(localStorage.getItem('hrh-beat-offset') || '0', 10) || 0; } catch (e) { return 0; } });
  A.offset_ms = SDL_clamp(A.offset_ms, -250, 250);
#endif
}

static void go_title(void) {
  int sel = G.sel;
  set_paused(false);
  SDL_memset(&G, 0, sizeof(G));
  G.state = ST_TITLE; G.sel = sel; G.target = -1;
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

static void update_typing(float dt) {
  const Rank *R = &ranks[G.rank];
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
    float v = R->speed * (e->kind == K_BAT ? 1.8f : e->kind == K_BRUTE ? 0.6f : 1.0f);
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
  for (int i = 0; i < 4; i++) if (G.press_flash[i] > 0) G.press_flash[i] -= dt;
  for (int i = 0; i < (int)SDL_arraysize(G.parts); i++) {
    Particle *p = &G.parts[i];
    if (p->life <= 0) continue;
    p->life -= dt; p->x += p->vx * dt; p->y += p->vy * dt; p->vy += 160 * dt;
  }
  if (G.state != ST_PLAY) {
    if (T.bot > 0 && G.state == ST_RANKUP && G.state_t > 1) start_rank(G.rank + 1);
    return;
  }

  if (G.sub == SUB_TYPING) update_typing(dt);
  else if (G.sub == SUB_BOSS) boss_update(dt);
  else { G.out_t -= dt; if (G.out_t <= 0) G.sub = SUB_TYPING; }

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

  if (G.hp <= 0) {
    G.hp = 0; G.state = ST_GAMEOVER; G.state_t = 0;
    audio_song(SONG_NONE);
    for (int i = 0; i < 4; i++) sfx(WAVE_SQUARE, midi_hz(57 - i * 3), 0, 0.3f, 0.14f, i * 0.28f);
  } else if (G.kills >= ranks[G.rank].quota && G.sub == SUB_TYPING) {
    G.state = G.rank + 1 >= NRANKS ? ST_VICTORY : ST_RANKUP; G.state_t = 0;
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
    if (e->words == 1) {
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

static void draw_boss(void) {
  double t = audio_song_time();
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
  for (int c = 0; c < 4; c++) {
    float cx = 112.0f + 32.0f * c;
    rect_a(cx - 15, 32, 30, 152, 0x101028, 255);
    SDL_FRect d = { cx - 7, RECEPTOR_Y - 7, 14, 14 };
    bool lit = G.press_flash[c] > 0;
    SDL_SetTextureColorMod(s_note.t, lit ? 255 : 110, lit ? 255 : 110, lit ? 255 : 110);
    SDL_RenderTextureRotated(ren, s_note.t, NULL, &d, boss_angles[c], NULL, SDL_FLIP_NONE);
  }
  SDL_Rect clip = { 90, B->reveal_y, 140, 180 - B->reveal_y };
  SDL_SetRenderClipRect(ren, &clip);
  for (int i = 0; i < G.nnotes; i++) {
    const Note *n = &G.notes[i];
    if (n->state == 1) continue;
    float y = RECEPTOR_Y - (float)(note_time(n) - t) * B->note_speed;
    if (y < 20 || y > 200) continue;
    SDL_FRect d = { 112.0f + 32.0f * n->col - 7, SDL_floorf(y) - 7, 14, 14 };
    Uint32 col = n->state == 2 ? 0x505050 : boss_colors[n->col];
    SDL_SetTextureColorMod(s_note.t, (Uint8)(col >> 16), (Uint8)(col >> 8), (Uint8)col);
    SDL_RenderTextureRotated(ren, s_note.t, NULL, &d, boss_angles[n->col], NULL, SDL_FLIP_NONE);
  }
  SDL_SetRenderClipRect(ren, NULL);
  SDL_SetTextureColorMod(s_note.t, 255, 255, 255);

  double count_in = DANCE_COUNT_IN * DANCE_STEP_SEC;
  if (t < count_in) {
    text_center(160, 60, "BOSS BATTLE!", C_RED, 2);
    text_center(160, 84, "MOVE YOUR HAND TO", C_WHITE, 1);
    text_center(160, 100, "ARROW KEYS", C_YELLOW, 2);
    text_center(160, 126, B->hint, C_GREY, 1);
    int left = (int)ceil((count_in - t) / (DANCE_STEP_SEC * 2));
    if (left <= 3 && left >= 1) { char s[2] = { (char)('0' + left), 0 }; text_center(160, 140, s, C_WHITE, 3); }
  } else if (G.judge_t > 0) {
    text_center(160, 120, G.judge, G.judge_color, 2);
    if (G.combo >= 4) { char s[16]; SDL_snprintf(s, sizeof(s), "COMBO %d", G.combo); text_center(160, 138, s, C_WHITE, 1); }
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
  SDL_snprintf(buf, sizeof(buf), "R%d %s", G.rank + 1, R->name);
  text(4, 4, buf, C_YELLOW, 1);
  SDL_snprintf(buf, sizeof(buf), "SLAIN %d/%d", G.kills, R->quota);
  text(76, 4, buf, C_WHITE, 1);
  SDL_snprintf(buf, sizeof(buf), "PTS %d", G.score);
  text(154, 4, buf, C_CYAN, 1);
  for (int i = 0; i < MAX_HP; i++) {
    if (i < G.hp) spr(s_heart, (float)(236 + i * 8), 5);
    else rect((float)(238 + i * 8), 7, 3, 2, C_DGREY);
  }

  if (!boss) {   // current weapon, and how far the clean streak is from the next one
    int w = weapon_now();
    if (w + 1 < NWEAPONS) SDL_snprintf(buf, sizeof(buf), "%s  STREAK %d/%d", weapons[w].name, G.streak, weapons[w + 1].streak);
    else SDL_snprintf(buf, sizeof(buf), "%s  STREAK %d", weapons[w].name, G.streak);
    text_shadow(4, 20, buf, w ? weapons[w].trail : C_GREY, 1);
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
  } else if (G.rank <= 2) draw_keyboard(next_char());
  else draw_typing_hud();

  if (G.msg_t > 0) text_center(160, 70, G.msg, G.msg_color, 2);
}

static void overlay(int alpha) { rect_a(0, 0, SCREEN_W, SCREEN_H, C_BLACK, alpha); }

static void draw_title(void) {
  SDL_RenderTexture(ren, bg_tex, NULL, NULL);
  overlay(150);
  text_center(160, 14, "HOME ROW", C_RED, 4);
  text_center(160, 46, "HORDE", C_YELLOW, 4);
  text_center(160, 80, "TYPE TO HOLD THE WALL", C_WHITE, 1);
  for (int i = 0; i < NRANKS; i++) {
    float y = (float)(98 + i * 12);
    bool on = i == G.sel;
    char buf[64];
    if (on) { rect_a(24, y - 2, 272, 11, C_DPURP, 255); text(28, y, ">", C_YELLOW, 1); }
    SDL_snprintf(buf, sizeof(buf), "%d %s", i + 1, ranks[i].name);
    text(40, y, buf, on ? C_YELLOW : C_GREY, 1);
    text(112, y, ranks[i].blurb, on ? C_WHITE : C_DGREY, 1);
  }
  text_center(160, 200, "UP / DOWN OR 1-8: CHOOSE YOUR RANK", C_GREY, 1);
  if ((int)(G.time * 2) & 1) text_center(160, 214, "PRESS ENTER TO START", C_WHITE, 1);
#ifndef __EMSCRIPTEN__
  text_center(160, 228, "F11: FULLSCREEN", C_DGREY, 1);
#endif
}

static void draw_banner(void) {
  char buf[64];
  int total = G.correct + G.wrong, acc = total ? G.correct * 100 / total : 100;
  float avg = G.rank_clock > 1 ? (float)G.correct / 5.0f / (G.rank_clock / 60.0f) : 0;
  overlay(170);
  if (G.state == ST_GAMEOVER) {
    text_center(160, 60, "THE WALL HAS", C_RED, 3);
    text_center(160, 86, "FALLEN", C_RED, 3);
    text_center(160, 150, "ENTER: TRY THIS RANK AGAIN", C_WHITE, 1);
    text_center(160, 164, "ESC: BACK TO THE TITLE", C_GREY, 1);
  } else if (G.state == ST_VICTORY) {
    text_center(160, 50, "DAWN BREAKS!", C_YELLOW, 3);
    text_center(160, 82, "THE COMPOUND IS SAFE", C_WHITE, 2);
    text_center(160, 164, "ENTER: BACK TO THE TITLE", C_WHITE, 1);
  } else {
    text_center(160, 50, "RANK UP!", C_YELLOW, 4);
    SDL_snprintf(buf, sizeof(buf), "YOU ARE NOW A %s", ranks[G.rank + 1].name);
    text_center(160, 90, buf, C_WHITE, 1);
    text_center(160, 102, ranks[G.rank + 1].blurb, C_CYAN, 1);
    text_center(160, 164, "ENTER: NEXT NIGHT", C_WHITE, 1);
  }
  SDL_snprintf(buf, sizeof(buf), "ACCURACY %d%%   SPEED %d WPM", acc, (int)(avg + 0.5f));
  text_center(160, 124, buf, C_GREY, 1);
  SDL_snprintf(buf, sizeof(buf), "SCORE %d", G.score);
  text_center(160, 136, buf, C_CYAN, 1);
}

static void update_status(void) {
  char buf[96];
  const Rank *R = &ranks[G.rank];
  if (G.state == ST_TITLE) SDL_snprintf(buf, sizeof(buf), "Home Row Horde. Press Enter to start.");
  else if (G.state == ST_PLAY && G.paused) SDL_snprintf(buf, sizeof(buf), "Paused. Press Enter to keep playing.");
  else if (G.state == ST_GAMEOVER) SDL_snprintf(buf, sizeof(buf), "The wall has fallen. Press Enter to try again.");
  else if (G.state == ST_VICTORY) SDL_snprintf(buf, sizeof(buf), "Dawn breaks. You win! Score %d.", G.score);
  else if (G.state == ST_RANKUP) SDL_snprintf(buf, sizeof(buf), "Rank up! Press Enter for rank %d.", G.rank + 2);
  else if (G.sub == SUB_BOSS) SDL_snprintf(buf, sizeof(buf), "Boss battle: hit the arrow keys on the beat.");
  else {
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
  else {
    draw_play();
    if (G.state != ST_PLAY) draw_banner();
    else if (G.paused) {
      overlay(170);
      text_center(160, 70, "PAUSED", C_YELLOW, 4);
      text_center(160, 124, "ENTER OR ESC: KEEP PLAYING", C_WHITE, 1);
      text_center(160, 138, "Q: QUIT TO THE TITLE", C_GREY, 1);
      char buf[48];
      SDL_snprintf(buf, sizeof(buf), "< > BEAT TIMING: %+d MS", A.offset_ms);
      text_center(160, 166, buf, C_CYAN, 1);
      text_center(160, 180, "ARROWS LAND BEFORE THE BEAT? PRESS >", C_GREY, 1);
      text_center(160, 191, "ARROWS LAND AFTER THE BEAT? PRESS <", C_GREY, 1);
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
  switch (G.state) {
    case ST_TITLE:
      if (key == SDLK_UP) { G.sel = (G.sel + NRANKS - 1) % NRANKS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key == SDLK_DOWN) { G.sel = (G.sel + 1) % NRANKS; sfx(WAVE_SQUARE, 660, 0, 0.04f, 0.08f, 0); }
      else if (key >= SDLK_1 && key < SDLK_1 + NRANKS) G.sel = (int)(key - SDLK_1);
      else if (enter) new_game(G.sel);
      break;
    case ST_PLAY:
      if (G.paused) {
        if (enter || key == SDLK_ESCAPE) set_paused(false);
        else if (key == SDLK_Q) go_title();
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
      } else if (G.sub == SUB_TYPING) {
        if (key >= SDLK_A && key <= SDLK_Z) type_char((char)('A' + (key - SDLK_A)));
        else if (key == SDLK_SPACE) type_char(' ');
        else if (key == SDLK_BACKSPACE) G.target = -1;
      }
      break;
    case ST_RANKUP:
      if (enter && G.state_t > 0.5f) start_rank(G.rank + 1);
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
  s_bolt[0] = SPRITE(bolt, "RgYG");       // oak arrow, plain iron tip
  s_bolt[1] = SPRITE(bolt, NULL);         // fire arrow
  s_bolt[2] = SPRITE(bolt, "oGRWYW");     // silver bolt
  s_bolt[3] = SPRITE(bolt, "oCRWYCWC");   // storm bolt
  s_tomb = SPRITE(tombstone, NULL); s_heart = SPRITE(heart, NULL); s_note = SPRITE(note_arrow, NULL);
  build_background();

  audio_init(T.on);
  offset_load();
  go_title();
#ifndef __EMSCRIPTEN__
  if (getenv("HRH_RANK")) new_game(atoi(getenv("HRH_RANK")) - 1);
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
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) key_down(event->key.key);
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
