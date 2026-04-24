// hello
#pragma once
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define TAB_CLEAR "\x1b[H"
#define TAB_ALT_ON "\x1b[?1049h"
#define TAB_ALT_OFF "\x1b[?1049l"
#define TAB_HIDE_CUR "\x1b[?25l"
#define TAB_SHOW_CUR "\x1b[?25h"

class Tab {
private:
    static struct termios _orig_termios;
    std::string buf;

    static void _enable_raw();
    static void _disable_raw();
    static void _die(const char *str);

public:
    Tab() : buf("") {
        _enable_raw();
        write(STDOUT_FILENO, TAB_ALT_ON, sizeof(TAB_ALT_ON) - 1);
        write(STDOUT_FILENO, TAB_HIDE_CUR, sizeof(TAB_HIDE_CUR) - 1);
        write(STDOUT_FILENO, TAB_CLEAR, sizeof(TAB_CLEAR) - 1);
    }

    ~Tab() {
        write(STDOUT_FILENO, TAB_SHOW_CUR, sizeof(TAB_SHOW_CUR) - 1);
        write(STDOUT_FILENO, TAB_ALT_OFF, sizeof(TAB_ALT_OFF) - 1);
        _disable_raw();
    }

    void add(std::string s);
    void add_line(std::string s, int width);
    void show();
    void log(std::string s);
    int get_window_size(int *cols, int *rows);
    void get_key(char *c);
};
