// SPDX-License-Identifier: BSD-3-Clause
#include "term.hpp"

#include <cstdio>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace imx95 {

namespace {
struct termios g_saved;
int g_saved_flags = 0;
}

RawMode::RawMode() {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return;

    struct termios raw = g_saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;

    g_saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_saved_flags | O_NONBLOCK);

    term_hide_cursor();
    active_ = true;
}

RawMode::~RawMode() {
    if (!active_) return;
    term_show_cursor();
    fcntl(STDIN_FILENO, F_SETFL, g_saved_flags);
    tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    active_ = false;
}

int term_getch() {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? static_cast<int>(c) : -1;
}

void term_clear()         { std::fputs("\033[2J\033[H", stdout); }
void term_home()          { std::fputs("\033[H", stdout); }
void term_hide_cursor()   { std::fputs("\033[?25l", stdout); std::fflush(stdout); }
void term_show_cursor()   { std::fputs("\033[?25h", stdout); std::fflush(stdout); }
void term_clear_to_eol()  { std::fputs("\033[K", stdout); }

} // namespace imx95
