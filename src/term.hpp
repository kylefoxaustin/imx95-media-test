// SPDX-License-Identifier: BSD-3-Clause
#pragma once

namespace imx95 {

// RAII raw-terminal mode for the live run dashboard: disables canonical mode
// and echo and puts stdin in non-blocking mode, restoring everything on
// destruction (including via the normal unwind after a caught SIGINT).
class RawMode {
public:
    RawMode();
    ~RawMode();
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;
    bool active() const { return active_; }

private:
    bool active_ = false;
};

// Non-blocking single-byte read from stdin. Returns the byte, or -1 if none
// is available. Only meaningful while a RawMode is in scope.
int term_getch();

// ANSI helpers.
void term_clear();
void term_home();
void term_hide_cursor();
void term_show_cursor();
void term_clear_to_eol();

} // namespace imx95
