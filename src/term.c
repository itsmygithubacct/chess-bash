/* Game-facing terminal API over the shared Kitty framebuffer presenter. */
#include "chess_bash.h"
#include "kitty_framebuffer.h"

#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>

static kittyfb_session framebuffer;
static bool framebuffer_active;
static volatile int shutdown_claimed;

static int read_byte_timeout(unsigned char *c, int timeout_ms)
{
    fd_set readfds;
    struct timeval timeout = {timeout_ms / 1000,
                              (timeout_ms % 1000) * 1000};
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) <= 0)
        return 0;
    return read(STDIN_FILENO, c, 1) == 1;
}

bool term_init(int *outW, int *outH)
{
    kittyfb_options options;

    kittyfb_session_init(&framebuffer);
    kittyfb_options_init(&options);
    options.min_width = 480;
    options.min_height = 320;
    options.max_width = 1600;
    options.max_height = 1000;
    if (getenv("CHESS_BASH_SKIP_PROBE")) options.probe_graphics = false;
    if (kittyfb_start(&framebuffer, STDIN_FILENO, STDOUT_FILENO,
                      &options) != 0)
        return false;
    framebuffer_active = true;
    shutdown_claimed = 0;
    *outW = kittyfb_width(&framebuffer);
    *outH = kittyfb_height(&framebuffer);
    return true;
}

bool term_check_resize(int *outW, int *outH)
{
    return framebuffer_active &&
           kittyfb_check_resize(&framebuffer, outW, outH);
}

void term_present(const uint8_t *rgba, int w, int h)
{
    if (framebuffer_active)
        (void)kittyfb_present(&framebuffer, rgba, w, h);
}

static bool claim_shutdown(void)
{
    if (!framebuffer_active) return false;
    return !__sync_lock_test_and_set(&shutdown_claimed, 1);
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    kittyfb_stop(&framebuffer);
    framebuffer_active = false;
}

void term_emergency_restore(void)
{
    if (!claim_shutdown()) return;
    kittyfb_emergency_restore(&framebuffer);
}

int term_poll_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 127 || c == 8) return KEY_BACKSPACE;
    if (c == '\t') return KEY_TAB;
    if (c == 3) { G.quit = true; return -1; }
    if (c == 0x1b) {
        unsigned char seq[2];
        if (!read_byte_timeout(&seq[0], 25)) return KEY_ESC;
        if (seq[0] != '[' && seq[0] != 'O') return KEY_ESC;
        if (!read_byte_timeout(&seq[1], 25)) return KEY_ESC;
        switch (seq[1]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        default:
            while (seq[1] >= '0' && seq[1] <= ';')
                if (!read_byte_timeout(&seq[1], 25)) break;
            return -1;
        }
    }
    return c;
}
