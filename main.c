/*** headers ***/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // for control operations

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

// for error handling purpose. prints an error message and exits.
void die(const char *s){
    perror(s); // prints a descriptive error message
    exit(1);
}

void disableRawMode(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;

    tcgetattr(STDIN_FILENO, &raw);

    // disables ctrl-s/ctrl-q operations, ctrl-m and newline operations, and oher misc flags
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    // disables "\n" and "\r\n" translations in the output
    raw.c_oflag &= ~(OPOST);

    // disables echo, canonical mode, ctrl-c/ctrl-z operations, and ctrl-v/ctrl-o operations
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    raw.c_cflag &= ~(CS8);

    // read() timeout
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorKeyRead(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/*** input ***/

// key mappings
void editorProcessKeyPress() {
    char c = editorKeyRead();

    switch (c){
        // reads every character until ctrl-q is pressed (exits the program)
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

/*** initialization ***/

int main(){
    enableRawMode();
    while (1){
        editorProcessKeyPress();
    }
    return 0;
}