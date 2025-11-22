/*** headers ***/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // for control operations
#define TILDE_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

/*** data ***/

struct editorConfig{
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

// for error handling purpose. prints an error message and exits.
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4); // clears the terminal screen
    write(STDOUT_FILENO, "\x1b[H", 3); // moves cursor to home position (top left) of the terminal

    perror(s); // prints a descriptive error message
    exit(1);
}

void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

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

int editorKeyRead(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b'){
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '['){
            if (seq[1] > '0' && seq[1] <= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'E': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O'){
            switch (seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    printf("\r\n");

    while (i < sizeof(buf) - 1){
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return 1;
    
    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        // moving the cursor to the bottom right of the screen to calculate the number of rows if ioctl fails to work
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

// since c doesn't have dynamic buffer, i had to make a struct to achieve that
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab -> b, ab -> len + len);

    if (new == NULL) return;
    memcpy(&new[ab -> len], s, len);
    ab -> b = new;
    ab -> len += len;
}

void abFree(struct abuf *ab){
    free (ab -> b);
}

/*** input ***/

void editorMoveCursor(int key){
    switch(key){
        case ARROW_LEFT:
            if (E.cx != 0){
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != 0){
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != 0){
                E.cy++;
            }
            break;
    }
}

void editorProcessKeypress() {
    int c = editorKeyRead();

    switch (c){
        // reads every character until ctrl-q is pressed (exits the program)
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); // clears the terminal screen
            write(STDOUT_FILENO, "\x1b[H", 3); // moves cursor to home position (top left) of the terminal
            
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        
        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP: case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--){
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;
        case ARROW_UP: case ARROW_LEFT: case ARROW_DOWN: case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** output ***/

void editorDrawRows(struct abuf *ab){
    int y;
    for (y = 0; y < E.screenrows; y++){
        if (y == E.screenrows / 3) {
            char welcome[50];
            int welcomelen = snprintf(welcome, sizeof(welcome),"Tilde Editor -- version %s", TILDE_VERSION);
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            // centering the welcome message
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding --;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1); // starts every row with a tilde
        }

        abAppend(ab, "\x1b[K", 3); // erases part of the current line
        if (y < E.screenrows - 1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorScreenRefresh(){
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3); // moves cursor to home position (top left) of the terminal

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25H", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** initialization ***/
void initEditor(){
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.cx = 0;
    E.cy = 0;
}

int main(){
    enableRawMode();
    initEditor();

    while (1){
        editorScreenRefresh();
        editorProcessKeypress();
    }
    return 0;
}