// hello
#include "utah.h"
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

struct termios Tab::_orig_termios;

void Tab::add(std::string s) {
    buf += s;
}

void Tab::add_line(std::string s, int width) {
    buf += s;
    buf += width - s.size() > 0 ? std::string(width - s.size(), ' ') : "";
}

void Tab::show() {
    write(STDOUT_FILENO, TAB_CLEAR, sizeof(TAB_CLEAR) - 1);
    write(STDOUT_FILENO, buf.c_str(), buf.size());
    buf.clear();
}

void Tab::log(std::string s) {
    write(STDOUT_FILENO, TAB_ALT_OFF, sizeof(TAB_ALT_OFF) - 1);
    fprintf(stderr, "[LOG] %s", s.c_str());
    write(STDOUT_FILENO, TAB_ALT_ON, sizeof(TAB_ALT_ON) - 1);
}

int Tab::get_window_size(int *cols, int *rows) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        _die("getWindowSize");
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }

    return -1;
}

// instant, will result in nothing if no immediate keypress
void Tab::get_key(char *c) {
    int nread;
    if ((nread = read(STDIN_FILENO, c, 1)) != 1) {
         if (nread == -1 && errno != EAGAIN)
             _die("getIfKey");
    }
}


void Tab::_enable_raw() {
    if (tcgetattr(STDIN_FILENO, &_orig_termios) == -1)
        _die("tcgetattr");

    std::atexit(_disable_raw);

    struct termios raw = _orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // mess with this for faster read times

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        _die("tcsetattr");
}

void Tab::_disable_raw(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &_orig_termios) == -1)
        _die("tcsetattr");
}

void Tab::_die(const char *str) {
    _disable_raw();
    write(STDOUT_FILENO, TAB_ALT_OFF, sizeof(TAB_ALT_OFF) - 1);

    perror(str);
    exit(1);
}
