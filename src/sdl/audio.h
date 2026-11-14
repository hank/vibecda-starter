// Home Row Horde: chiptune synth, the two songs, and sound effects. All audio is
// generated in code and pushed to one SDL audio stream from the main thread, so
// the song position (used to judge dance notes) comes straight from the stream.
#ifndef HRH_AUDIO_H
#define HRH_AUDIO_H

#include <SDL3/SDL.h>
#include <math.h>

#define SR 44100
enum { SONG_NONE, SONG_TYPING, SONG_DANCE, SONG_CALIBRATE };
enum { WAVE_SQUARE, WAVE_NOISE, WAVE_TRI };

// The dance song: 120 BPM, one step per eighth note, two count-in bars and then
// an eight-bar tune. The dance chart is derived from this melody.
#define DANCE_BPM 120
#define DANCE_STEP_SEC (60.0 / DANCE_BPM / 2.0)
#define DANCE_COUNT_IN 16
#define DANCE_STEPS 64
static const Uint8 dance_lead[DANCE_STEPS] = {
  69,72,76,72,69,72,76,81,  65,69,72,69,65,69,72,77,
  67,71,74,71,67,71,74,79,  64,68,71,68,76,74,72,71,
  76, 0,76,74,72, 0,69, 0,  72, 0,72,71,69, 0,65, 0,
  74, 0,74,72,71, 0,67, 0,  76, 0,68, 0,71, 0,76, 0,
};
static const Uint8 dance_bass[8] = { 45, 41, 43, 40, 45, 41, 43, 40 };

// The typing theme: slow, sparse, and a little uneasy. 90 BPM, four bars.
#define TYPING_BPM 90
static const Uint8 typing_lead[32] = {
  69, 0, 0,64, 0, 0,63, 0,  64, 0, 0, 0, 0, 0, 0, 0,
  69, 0, 0,64, 0, 0,65, 0,  64, 0,62, 0,60, 0,59, 0,
};
static const Uint8 typing_bass[32] = {
  45, 0, 0, 0,52, 0, 0, 0,  45, 0, 0, 0,48, 0, 0, 0,
  41, 0, 0, 0,48, 0, 0, 0,  40, 0, 0, 0,47, 0,44, 0,
};

typedef struct { bool on; int wave; float freq, slide, vol, dur, t, delay, phase; } Sfx;

static struct {
  SDL_AudioStream *stream;
  bool fake;                 // no device: advance the clock from frame time instead
  Sint64 generated;          // samples generated so far
  double clock;              // smoothed playback position, in seconds of samples
  double latency;            // estimated delay between that position and the speakers
  int offset_ms;             // the player's own adjustment, from calibration or the pause screen
  Uint64 pump_ns;            // when the clock was last advanced, for timing key presses between frames
  int song, step;
  Sint64 song_start;         // sample index where the current song began
  double step_len, step_pos; // in samples
  float lead_f, lead_ph, lead_env, lead_vol;
  float bass_f, bass_ph, bass_env;
  float kick_t, kick_ph, snare_env, hat_env;
  Uint32 noise;
  Sfx sfx[10];
} A;

static float midi_hz(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }
static float noise1(void) {
  A.noise ^= A.noise << 13; A.noise ^= A.noise >> 17; A.noise ^= A.noise << 5;
  return (float)(A.noise & 0xFFFF) / 32768.0f - 1.0f;
}

static void audio_step(void) {
  int s = A.step;
  if (A.song == SONG_TYPING) {
    int p = s % 32;
    if (typing_lead[p]) { A.lead_f = midi_hz(typing_lead[p]); A.lead_env = 1; A.lead_vol = 0.07f; }
    if (typing_bass[p]) { A.bass_f = midi_hz(typing_bass[p]); A.bass_env = 1; }
    if (p % 8 == 0) A.kick_t = 0;
  } else if (A.song == SONG_CALIBRATE) {
    // A bare 4/4 click: kick and tick on every beat, a higher tick on beat one.
    if (s % 2 == 0) {
      A.kick_t = 0; A.hat_env = 1;
      A.lead_f = midi_hz(s % 8 == 0 ? 88 : 81); A.lead_env = 0.6f; A.lead_vol = 0.12f;
    }
  } else if (A.song == SONG_DANCE) {
    if (s < DANCE_COUNT_IN) {
      // Count-in: a tick on every beat, pitched up for the final "3, 2, 1".
      if (s % 2 == 0) {
        A.hat_env = 1;
        A.lead_f = midi_hz(s >= 8 ? (s == 14 ? 88 : 81) : 69); A.lead_env = 0.6f; A.lead_vol = 0.10f;
      }
      if (s % 8 == 0) A.kick_t = 0;
    } else {
      int p = (s - DANCE_COUNT_IN) % DANCE_STEPS;
      if (dance_lead[p]) { A.lead_f = midi_hz(dance_lead[p]); A.lead_env = 1; A.lead_vol = 0.11f; }
      A.bass_f = midi_hz(dance_bass[p / 8] + (p % 2 ? 12 : 0)); A.bass_env = 1;
      if (p % 4 == 0) A.kick_t = 0;
      if (p % 4 == 2) A.snare_env = 1;
      A.hat_env = (p % 2) ? 0.5f : 1;
    }
  }
}

static void audio_song(int song) {
  A.song = song; A.step = 0; A.step_pos = 0; A.song_start = A.generated;
  A.step_len = SR * 60.0 / (song == SONG_TYPING ? TYPING_BPM : DANCE_BPM) / 2.0;
  A.lead_env = A.bass_env = A.snare_env = A.hat_env = 0; A.kick_t = 1;
  if (song) audio_step();
}

static void sfx(int wave, float freq, float slide, float dur, float vol, float delay) {
  for (int i = 0; i < (int)SDL_arraysize(A.sfx); i++)
    if (!A.sfx[i].on) {
      A.sfx[i] = (Sfx){ true, wave, freq, slide, vol, dur, 0, delay, 0 };
      return;
    }
}

static float audio_sample(void) {
  const float inv = 1.0f / SR;
  float out = 0;
  if (A.song) {
    A.step_pos += 1;
    if (A.step_pos >= A.step_len) { A.step_pos -= A.step_len; A.step++; audio_step(); }
    // lead: 25% pulse
    A.lead_ph += A.lead_f * inv; A.lead_ph -= SDL_floorf(A.lead_ph);
    out += (A.lead_ph < 0.25f ? 1.0f : -1.0f) * A.lead_env * A.lead_vol;
    A.lead_env *= 0.99988f;
    // bass: triangle
    A.bass_ph += A.bass_f * inv; A.bass_ph -= SDL_floorf(A.bass_ph);
    out += (4.0f * SDL_fabsf(A.bass_ph - 0.5f) - 1.0f) * A.bass_env * 0.22f;
    A.bass_env *= 0.99990f;
    // drums
    if (A.kick_t < 0.16f) {
      A.kick_ph += (40.0f + 130.0f * expf(-A.kick_t * 28.0f)) * inv;
      out += sinf(A.kick_ph * 6.2831853f) * (1.0f - A.kick_t / 0.16f) * 0.30f;
      A.kick_t += inv;
    }
    float n = noise1();
    out += n * A.snare_env * 0.12f; A.snare_env *= 0.9996f;
    out += n * A.hat_env * 0.035f;  A.hat_env *= 0.9975f;
  }
  for (int i = 0; i < (int)SDL_arraysize(A.sfx); i++) {
    Sfx *s = &A.sfx[i];
    if (!s->on) continue;
    if (s->delay > 0) { s->delay -= inv; continue; }
    float amp = (1.0f - s->t / s->dur) * s->vol, v;
    s->phase += s->freq * inv; s->phase -= SDL_floorf(s->phase);
    if (s->wave == WAVE_NOISE) v = noise1();
    else if (s->wave == WAVE_TRI) v = 4.0f * SDL_fabsf(s->phase - 0.5f) - 1.0f;
    else v = s->phase < 0.5f ? 1.0f : -1.0f;
    out += v * amp;
    s->freq += s->slide * inv; if (s->freq < 20) s->freq = 20;
    s->t += inv; if (s->t >= s->dur) s->on = false;
  }
  return out > 1 ? 1 : out < -1 ? -1 : out;
}

static void audio_init(bool fake) {
  A.noise = 0x1234567; A.kick_t = 1;
  A.fake = fake;
  if (!fake) {
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, SR };
    A.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (A.stream) {
      // The device pulls audio a buffer at a time. On average the stream position
      // runs half a buffer ahead of what is being mixed, and roughly one more
      // buffer sits between the mixer and the speakers.
      SDL_AudioSpec dev; int frames = 0;
      A.latency = 0.05;
      if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(A.stream), &dev, &frames) && frames > 0 && dev.freq > 0)
        A.latency = 1.5 * frames / dev.freq + 0.01;
      SDL_Log("Audio buffer %d frames, estimated latency %.0f ms", frames, A.latency * 1000);
      SDL_ResumeAudioStreamDevice(A.stream);
    } else A.fake = true;
  }
}

// Call once per frame: keeps about a tenth of a second of audio queued.
static void audio_pump(float dt) {
  float buf[512];
  if (A.fake) {
    int n = (int)(dt * SR);
    for (int i = 0; i < n; i++) audio_sample();
    A.generated += n;
    return;
  }
  int queued = SDL_GetAudioStreamQueued(A.stream) / (int)sizeof(float);
  while (queued < SR / 10) {
    for (int i = 0; i < 512; i++) buf[i] = audio_sample();
    SDL_PutAudioStreamData(A.stream, buf, sizeof(buf));
    A.generated += 512; queued += 512;
  }
  // The raw position moves in buffer-sized jumps. Advance a steady clock with
  // frame time and pull it gently toward the raw position so it cannot drift.
  double raw = (double)(A.generated - SDL_GetAudioStreamQueued(A.stream) / (int)sizeof(float)) / SR;
  A.clock += dt;
  double err = raw - A.clock;
  if (err > 0.15 || err < -0.15) A.clock = raw; else A.clock += err * 0.05;
  A.pump_ns = SDL_GetTicksNS();
}

// Seconds into the current song as it is heard, before the player's own offset.
// Pass the timestamp of a key press (or 0 for "this frame") so that presses are
// timed to when they happened rather than to the frame that noticed them.
static double audio_raw_time_at(Uint64 ns) {
  if (A.fake) return (double)(A.generated - A.song_start) / SR;
  double t = A.clock - (double)A.song_start / SR - A.latency;
  if (ns && A.pump_ns) {
    double d = ((double)ns - (double)A.pump_ns) / 1e9;
    t += d > 0.1 ? 0.1 : d < -0.1 ? -0.1 : d;
  }
  return t;
}
static double audio_song_time_at(Uint64 ns) { return audio_raw_time_at(ns) - (A.fake ? 0 : A.offset_ms / 1000.0); }
static double audio_song_time(void) { return audio_song_time_at(0); }

#endif
