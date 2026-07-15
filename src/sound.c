#define _GNU_SOURCE   /* pipe2 */
#include "chess_bash.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#define SR 44100
#define MIX_FRAMES 192
#define MAX_VOICES 16
#define MAX_SFX_VARIANTS 6

typedef struct { int16_t *data; int len; } Sample;
typedef struct {
    const int16_t *data;
    int len;
    float pos, step, vol;
    bool active;
} Voice;

typedef struct {
    const int16_t *data;
    int len;
    double pos;
    float vol;          /* current level, chases target */
    float target;
    float fade_step;    /* per-sample level change while chasing target */
    bool loop;
    bool active;
    int id;
} MusicVoice;

static Sample samples[SOUND_COUNT][MAX_SFX_VARIANTS];
static uint8_t sample_counts[SOUND_COUNT];
static uint8_t last_variants[SOUND_COUNT];
static Sample music_tracks[MUSIC_COUNT];
static Voice voices[MAX_VOICES];
static MusicVoice music_slots[2];
static pthread_mutex_t sound_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mixer;
static atomic_bool running = ATOMIC_VAR_INIT(false);
static int sink_fd = -1;
static pid_t sink_pid = -1;
static uint32_t srng = 0x51a7c0deu;

static void clear_sample(int id, int variant)
{
    if (id < 0 || id >= SOUND_COUNT ||
        variant < 0 || variant >= MAX_SFX_VARIANTS) return;
    free(samples[id][variant].data);
    samples[id][variant].data = NULL;
    samples[id][variant].len = 0;
}

static void clear_sample_bank(int id)
{
    if (id < 0 || id >= SOUND_COUNT) return;
    for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++)
        clear_sample(id, variant);
    sample_counts[id] = 0;
}

static float frand_audio(void)
{
    srng ^= srng << 13;
    srng ^= srng >> 17;
    srng ^= srng << 5;
    return (srng >> 8) * (1.0f / 16777216.0f);
}

static float noise_audio(void) { return frand_audio() * 2.0f - 1.0f; }

static void bake(int id, const float *src, int n, float peak)
{
    float maxv = 1e-6f;
    for (int i = 0; i < n; i++)
        if (fabsf(src[i]) > maxv) maxv = fabsf(src[i]);
    int16_t *out = malloc((size_t)n * sizeof *out);
    if (!out) return;
    float gain = peak / maxv;
    for (int i = 0; i < n; i++) {
        float v = clampf(src[i] * gain, -1.0f, 1.0f);
        int fi = 64, fo = 360;
        if (i < fi) v *= (float)i / fi;
        if (n - i < fo) v *= (float)(n - i) / fo;
        out[i] = (int16_t)(v * 32767.0f);
    }
    clear_sample_bank(id);
    samples[id][0].data = out;
    samples[id][0].len = n;
    sample_counts[id] = 1;
}

static void gen_beep(int id, float f0, float f1, float dur, float peak)
{
    int n = (int)(dur * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float f = f0 + (f1 - f0) * (t / dur);
        ph += 6.2831853f * f / SR;
        float env = expf(-t / (dur * 0.42f));
        s[i] = sinf(ph) * env;
    }
    bake(id, s, n, peak);
    free(s);
}

static void gen_move(void)
{
    int n = (int)(0.115f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float thump = expf(-t / 0.026f);
        float scrape = expf(-t / 0.052f);
        float ph = 6.2831853f * 92.0f * t;
        s[i] = sinf(ph) * 0.72f * thump + noise_audio() * 0.30f * scrape;
    }
    bake(SND_MOVE, s, n, 0.34f);
    free(s);
}

static void gen_capture(void)
{
    /* sword clash, not a bell: noise snap + fast inharmonic steel partials */
    static const float pf[6] = { 2210, 2967, 3743, 4638, 5512, 1493 };
    static const float ptau[6] = { 0.085f, 0.070f, 0.060f, 0.048f, 0.038f, 0.055f };
    static const float pa[6] = { 0.30f, 0.34f, 0.26f, 0.20f, 0.15f, 0.14f };
    static const float pph[6] = { 0.3f, 1.1f, 2.2f, 3.9f, 5.1f, 0.7f };
    int n = (int)(0.34f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float v = noise_audio() * (0.9f * expf(-t / 0.030f) +
                                   0.5f * expf(-t / 0.0045f));
        for (int k = 0; k < 6; k++)
            v += sinf(6.2831853f * pf[k] * t + pph[k]) * pa[k] * expf(-t / ptau[k]);
        v += sinf(6.2831853f * 168.0f * t) * 0.32f * expf(-t / 0.045f);
        s[i] = v;
    }
    bake(SND_CAPTURE, s, n, 0.58f);
    free(s);
}

static void gen_fall(void)
{
    int n = (int)(0.36f * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float low = expf(-t / 0.105f);
        float dirt = expf(-t / 0.130f);
        s[i] = sinf(6.2831853f * 72.0f * t) * 0.82f * low +
               noise_audio() * 0.46f * dirt;
    }
    bake(SND_FALL, s, n, 0.50f);
    free(s);
}

static void add_trumpet_note(float *s, int n, float start, float dur, float freq, float amp)
{
    int a = (int)(start * SR);
    int b = (int)((start + dur) * SR);
    if (a < 0) a = 0;
    if (b > n) b = n;
    float ph = 0;
    for (int i = a; i < b; i++) {
        float t = (float)(i - a) / SR;
        float env = fminf(t / 0.035f, 1.0f) * expf(-t / (dur * 0.95f));
        ph += 6.2831853f * freq / SR;
        float brass = sinf(ph) + 0.48f * sinf(ph * 2.0f) + 0.22f * sinf(ph * 3.0f);
        float bite = brass > 0 ? 1.0f : -0.65f;
        s[i] += tanhf((brass + bite * 0.18f) * 1.7f) * env * amp;
    }
}

static void gen_trumpet(int id, bool victory)
{
    float dur = victory ? 1.40f : 0.82f;
    int n = (int)(dur * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    if (victory) {
        add_trumpet_note(s, n, 0.00f, 0.28f, 392.00f, 0.75f);
        add_trumpet_note(s, n, 0.24f, 0.28f, 493.88f, 0.75f);
        add_trumpet_note(s, n, 0.48f, 0.36f, 587.33f, 0.82f);
        add_trumpet_note(s, n, 0.78f, 0.55f, 783.99f, 0.92f);
    } else {
        add_trumpet_note(s, n, 0.00f, 0.24f, 392.00f, 0.72f);
        add_trumpet_note(s, n, 0.21f, 0.24f, 523.25f, 0.78f);
        add_trumpet_note(s, n, 0.42f, 0.34f, 659.25f, 0.86f);
    }
    bake(id, s, n, victory ? 0.62f : 0.50f);
    free(s);
}

static void gen_check(void)
{
    /* short two-note danger sting: brass hit dropping a tritone */
    float dur = 0.34f;
    int n = (int)(dur * SR);
    float *s = calloc((size_t)n, sizeof *s);
    if (!s) return;
    add_trumpet_note(s, n, 0.00f, 0.14f, 830.61f, 0.85f);
    add_trumpet_note(s, n, 0.12f, 0.20f, 587.33f, 0.90f);
    bake(SND_CHECK, s, n, 0.48f);
    free(s);
}

static void synth_all(void)
{
    gen_beep(SND_SELECT, 760.0f, 1180.0f, 0.075f, 0.18f);
    gen_move();
    gen_capture();
    gen_fall();
    gen_trumpet(SND_START_TRUMPET, false);
    gen_trumpet(SND_WIN_TRUMPET, true);
    gen_check();
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool load_wav_into(Sample *dst, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long len = ftell(f);
    if (len < 44 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return false;
    }
    bool ok = fread(buf, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    if (!ok || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        free(buf);
        return false;
    }

    bool fmt_ok = false;
    uint32_t data_off = 0, data_size = 0;
    for (uint32_t off = 12; off + 8 <= (uint32_t)len;) {
        uint32_t size = le32(buf + off + 4);
        uint32_t body = off + 8;
        if (size > (uint32_t)len - body) break;   /* overflow-safe bound */
        if (!memcmp(buf + off, "fmt ", 4) && size >= 16) {
            uint16_t format = le16(buf + body);
            uint16_t channels = le16(buf + body + 2);
            uint32_t rate = le32(buf + body + 4);
            uint16_t bits = le16(buf + body + 14);
            fmt_ok = format == 1 && channels == 1 && rate == SR && bits == 16;
        } else if (!memcmp(buf + off, "data", 4)) {
            data_off = body;
            data_size = size;
        }
        off = body + size + (size & 1u);
    }

    if (!fmt_ok || !data_off || data_size < 2) {
        free(buf);
        return false;
    }
    data_size &= ~1u;
    int16_t *pcm = malloc(data_size);
    if (!pcm) {
        free(buf);
        return false;
    }
    memcpy(pcm, buf + data_off, data_size);
    free(buf);
    free(dst->data);
    dst->data = pcm;
    dst->len = (int)(data_size / sizeof *pcm);
    return true;
}

static bool load_wav_sample(int id, int variant, const char *path)
{
    if (id < 0 || id >= SOUND_COUNT ||
        variant < 0 || variant >= MAX_SFX_VARIANTS) return false;
    return load_wav_into(&samples[id][variant], path);
}

static void load_sfx_bank(int id, const char *const *paths, int count)
{
    if (id < 0 || id >= SOUND_COUNT || !paths || count < 1) return;
    if (count > MAX_SFX_VARIANTS) count = MAX_SFX_VARIANTS;
    int loaded = 0;
    for (int variant = 0; variant < count; variant++) {
        if (!load_wav_sample(id, variant, asset_path(paths[variant]))) break;
        loaded++;
    }
    if (loaded > 0) sample_counts[id] = (uint8_t)loaded;
}

static void load_sfx_assets(void)
{
    static const char *const select[] = {
        "sfx/select.wav", "sfx/select_v02.wav", "sfx/select_v03.wav"
    };
    static const char *const move[] = {
        "sfx/move_step.wav", "sfx/move_step_v02.wav", "sfx/move_step_v03.wav",
        "sfx/move_step_v04.wav", "sfx/move_step_v05.wav", "sfx/move_step_v06.wav"
    };
    static const char *const capture[] = {
        "sfx/capture_clank.wav", "sfx/capture_clank_v02.wav",
        "sfx/capture_clank_v03.wav", "sfx/capture_clank_v04.wav",
        "sfx/capture_clank_v05.wav"
    };
    static const char *const fall[] = {
        "sfx/fall_thud.wav", "sfx/fall_thud_v02.wav",
        "sfx/fall_thud_v03.wav", "sfx/fall_thud_v04.wav"
    };
    static const char *const start[] = {
        "sfx/start_trumpet.wav", "sfx/start_trumpet_v02.wav"
    };
    static const char *const win[] = {
        "sfx/win_trumpet.wav", "sfx/win_trumpet_v02.wav"
    };
    static const char *const check[] = {
        "sfx/check.wav", "sfx/check_v02.wav", "sfx/check_v03.wav"
    };

    load_sfx_bank(SND_SELECT, select, (int)(sizeof select / sizeof select[0]));
    load_sfx_bank(SND_MOVE, move, (int)(sizeof move / sizeof move[0]));
    load_sfx_bank(SND_CAPTURE, capture, (int)(sizeof capture / sizeof capture[0]));
    load_sfx_bank(SND_FALL, fall, (int)(sizeof fall / sizeof fall[0]));
    load_sfx_bank(SND_START_TRUMPET, start, (int)(sizeof start / sizeof start[0]));
    load_sfx_bank(SND_WIN_TRUMPET, win, (int)(sizeof win / sizeof win[0]));
    load_sfx_bank(SND_CHECK, check, (int)(sizeof check / sizeof check[0]));
}

static void load_music_assets(void)
{
    load_wav_into(&music_tracks[MUS_THINKING], asset_path("music/thinking_loop.wav"));
    load_wav_into(&music_tracks[MUS_BATTLE], asset_path("music/battle_loop.wav"));
    load_wav_into(&music_tracks[MUS_VICTORY], asset_path("music/victory_fanfare.wav"));
}

static bool in_path(const char *name)
{
    const char *path = getenv("PATH");
    if (!path) return false;
    char *copy = strdup(path);
    if (!copy) return false;
    bool found = false;
    for (char *p = copy, *tok; (tok = strsep(&p, ":")) != NULL;) {
        if (!*tok) tok = ".";
        char full[512];
        snprintf(full, sizeof full, "%s/%s", tok, name);
        if (access(full, X_OK) == 0) { found = true; break; }
    }
    free(copy);
    return found;
}

static bool spawn_sink(int idx)
{
    struct Sink { const char *exe; const char *argv[14]; } sinks[] = {
        { "pacat",   { "pacat", "--raw", "--latency-msec=18", "--rate=44100", "--channels=1", "--format=s16le", NULL } },
        { "pw-play", { "pw-play", "--raw", "--rate=44100", "--channels=1", "--format=s16", "-", NULL } },
        { "aplay",   { "aplay", "-q", "-f", "S16_LE", "-r", "44100", "-c", "1", "-B", "30000", "-F", "10000", NULL } },
        { "play",    { "play", "-q", "-t", "s16", "-r", "44100", "-c", "1", "-", NULL } },
    };
    if (idx < 0 || idx >= (int)(sizeof sinks / sizeof sinks[0])) return false;
    if (!in_path(sinks[idx].exe)) return false;
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) return false;
#ifdef F_SETPIPE_SZ
    /* the mixer keeps the pipe fed (silence included), so the pipe runs
     * full and its size IS the audio latency: 64 KiB of mono s16 is ~740ms
     * of lag between a cue and the speaker. One page keeps it under 50ms. */
    fcntl(pipefd[1], F_SETPIPE_SZ, 4096);
#endif
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        /* sink chatter on stdout/stderr would land in the kitty display */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execvp(sinks[idx].exe, (char *const *)sinks[idx].argv);
        _exit(127);
    }
    close(pipefd[0]);
    sink_fd = pipefd[1];
    sink_pid = pid;
    return true;
}

#define SINK_COUNT 4
static int sink_idx = -1;

static void close_sink(void);

/* a sink that dies mid-game (write error / stall) is replaced by the next
 * candidate in the chain instead of leaving the game permanently silent */
static void sink_failover(void)
{
    close_sink();
    for (int i = sink_idx + 1; i < SINK_COUNT; i++) {
        if (spawn_sink(i)) {
            sink_idx = i;
            return;
        }
    }
    sink_idx = SINK_COUNT;   /* out of options */
}

static void close_sink(void)
{
    if (sink_fd >= 0) {
        close(sink_fd);
        sink_fd = -1;
    }
    if (sink_pid > 0) {
        int status;
        waitpid(sink_pid, &status, WNOHANG);
        sink_pid = -1;
    }
}

static void mix_voice(Voice *v, float *mix, int n)
{
    if (!v->active || !v->data || v->len <= 0) return;
    for (int i = 0; i < n; i++) {
        int ip = (int)v->pos;
        if (ip >= v->len) {
            v->active = false;
            break;
        }
        int ip2 = ip + 1 < v->len ? ip + 1 : ip;
        float frac = v->pos - ip;
        float a = v->data[ip] / 32768.0f;
        float b = v->data[ip2] / 32768.0f;
        mix[i] += (a + (b - a) * frac) * v->vol;
        v->pos += v->step;
    }
}

static void mix_music(MusicVoice *m, float *mix, int n)
{
    if (!m->active || !m->data || m->len <= 0) return;
    for (int i = 0; i < n; i++) {
        if (m->vol < m->target) {
            m->vol += m->fade_step;
            if (m->vol > m->target) m->vol = m->target;
        } else if (m->vol > m->target) {
            m->vol -= m->fade_step;
            if (m->vol < m->target) m->vol = m->target;
        }
        if (m->vol <= 0.0f && m->target <= 0.0f) {
            m->active = false;
            return;
        }
        int ip = (int)m->pos;
        if (ip >= m->len) {
            if (!m->loop) {
                m->active = false;
                return;
            }
            m->pos -= m->len;
            ip = (int)m->pos;
        }
        mix[i] += (m->data[ip] / 32768.0f) * m->vol;
        m->pos += 1.0;
    }
}

static void *mixer_main(void *arg)
{
    (void)arg;
    float mix[MIX_FRAMES];
    int16_t out[MIX_FRAMES];
    const useconds_t chunk_us = (useconds_t)((1000000.0 * MIX_FRAMES) / SR);
    while (atomic_load_explicit(&running, memory_order_acquire)) {
        memset(mix, 0, sizeof mix);
        bool has_audio = false;
        pthread_mutex_lock(&sound_lock);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].active) has_audio = true;
            mix_voice(&voices[i], mix, MIX_FRAMES);
        }
        for (int i = 0; i < 2; i++) {
            if (music_slots[i].active) has_audio = true;
            mix_music(&music_slots[i], mix, MIX_FRAMES);
        }
        pthread_mutex_unlock(&sound_lock);
        (void)has_audio;
        /* silence is written too: a continuously fed sink never underruns
         * (no pops) and its blocking write paces this loop for free */
        for (int i = 0; i < MIX_FRAMES; i++) {
            float v = tanhf(mix[i] * 0.9f);
            out[i] = (int16_t)(clampf(v, -1.0f, 1.0f) * 32767.0f);
        }
        if (sink_fd < 0) {
            usleep(chunk_us);
            continue;
        }
        const uint8_t *p = (const uint8_t *)out;
        size_t left = sizeof out;
        int stalls = 0;
        while (left > 0 &&
               atomic_load_explicit(&running, memory_order_acquire)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sink_fd, &wfds);
            struct timeval tv = { 0, 250 * 1000 };
            int rc = select(sink_fd + 1, NULL, &wfds, NULL, &tv);
            if (rc == 0) {
                /* stalled sink: don't let sound_shutdown hang on join */
                if (++stalls >= 8) {
                    sink_failover();
                    break;
                }
                continue;
            }
            if (rc < 0) {
                if (errno == EINTR) continue;
                sink_failover();
                break;
            }
            ssize_t n = write(sink_fd, p, left);
            if (n < 0) {
                if (errno == EINTR) continue;
                sink_failover();
                break;
            }
            p += n;
            left -= (size_t)n;
        }
    }
    return NULL;
}

bool sound_init(void)
{
    signal(SIGPIPE, SIG_IGN);
    synth_all();
    load_sfx_assets();
    memset(last_variants, 0xff, sizeof last_variants);
    load_music_assets();
    for (int i = 0; i < SINK_COUNT && sink_fd < 0; i++)
        if (spawn_sink(i)) sink_idx = i;
    if (sink_fd < 0) return false;
    atomic_store_explicit(&running, true, memory_order_release);
    if (pthread_create(&mixer, NULL, mixer_main, NULL) != 0) {
        atomic_store_explicit(&running, false, memory_order_release);
        close_sink();
        return false;
    }
    return true;
}

void sound_shutdown(void)
{
    if (atomic_exchange_explicit(&running, false, memory_order_acq_rel)) {
        pthread_join(mixer, NULL);
    }
    close_sink();
    memset(voices, 0, sizeof voices);
    memset(music_slots, 0, sizeof music_slots);
    for (int i = 0; i < SOUND_COUNT; i++)
        clear_sample_bank(i);
    for (int i = 0; i < MUSIC_COUNT; i++) {
        free(music_tracks[i].data);
        music_tracks[i].data = NULL;
        music_tracks[i].len = 0;
    }
}

void sound_music_play(int id, float vol, bool loop)
{
    if (id < 0 || id >= MUSIC_COUNT || !music_tracks[id].data ||
        !atomic_load_explicit(&running, memory_order_acquire))
        return;
    pthread_mutex_lock(&sound_lock);
    /* crossfade: everything playing fades out, the new track fades in */
    int slot = 0;
    for (int i = 0; i < 2; i++) {
        if (music_slots[i].active && music_slots[i].id == id &&
            music_slots[i].target > 0.0f) {
            music_slots[i].target = clampf(vol, 0, 1.2f);
            pthread_mutex_unlock(&sound_lock);
            return;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (music_slots[i].active) {
            music_slots[i].target = 0.0f;
            music_slots[i].fade_step = 1.0f / (0.8f * SR);
        } else {
            slot = i;
        }
    }
    if (music_slots[0].active && music_slots[1].active) {
        /* both busy: steal the quieter one */
        slot = music_slots[0].vol <= music_slots[1].vol ? 0 : 1;
    }
    MusicVoice *m = &music_slots[slot];
    m->data = music_tracks[id].data;
    m->len = music_tracks[id].len;
    m->pos = 0.0;
    m->vol = 0.0f;
    m->target = clampf(vol, 0, 1.2f);
    m->fade_step = 1.0f / (0.6f * SR);
    m->loop = loop;
    m->id = id;
    m->active = true;
    pthread_mutex_unlock(&sound_lock);
}

void sound_music_stop(float fade_seconds)
{
    if (!atomic_load_explicit(&running, memory_order_acquire)) return;
    if (fade_seconds < 0.02f) fade_seconds = 0.02f;
    pthread_mutex_lock(&sound_lock);
    for (int i = 0; i < 2; i++) {
        if (!music_slots[i].active) continue;
        music_slots[i].target = 0.0f;
        music_slots[i].fade_step = 1.0f / (fade_seconds * SR);
    }
    pthread_mutex_unlock(&sound_lock);
}

int sound_music_current(void)
{
    int id = -1;
    pthread_mutex_lock(&sound_lock);
    for (int i = 0; i < 2; i++)
        if (music_slots[i].active && music_slots[i].target > 0.0f)
            id = music_slots[i].id;
    pthread_mutex_unlock(&sound_lock);
    return id;
}

void sound_play(int id, float vol, float pitch)
{
    if (id < 0 || id >= SOUND_COUNT || sample_counts[id] == 0 ||
        !samples[id][0].data ||
        !atomic_load_explicit(&running, memory_order_acquire))
        return;
    pthread_mutex_lock(&sound_lock);
    int variant = 0;
    int count = sample_counts[id];
    if (count > 1) {
        variant = (int)(frand_audio() * count);
        if (variant == last_variants[id]) {
            int offset = 1 + (int)(frand_audio() * (count - 1));
            variant = (variant + offset) % count;
        }
    }
    last_variants[id] = (uint8_t)variant;
    const Sample *sample = &samples[id][variant];
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            voices[i].data = sample->data;
            voices[i].len = sample->len;
            voices[i].pos = 0;
            voices[i].step = pitch <= 0 ? 1.0f : pitch;
            voices[i].vol = clampf(vol, 0, 1.5f);
            voices[i].active = true;
            break;
        }
    }
    pthread_mutex_unlock(&sound_lock);
}
