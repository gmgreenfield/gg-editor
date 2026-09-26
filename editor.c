#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/signal.h>
#endif

#define CTRL_KEY(k) ((k) & 0x1f)
#define KEY_RESIZE 1004

static volatile sig_atomic_t resize_pending;

struct termios original;

enum editor_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct {
    char *chars;
    size_t length;
} editor_row;

typedef struct {
    int cursor_x;
    int cursor_y;
    int screen_rows;
    int screen_cols;
    int dirty;
    int final_newline;
    const char *filename;
    editor_row *file_rows;
    size_t file_row_count;
    size_t file_row_capacity;
    size_t row_offset;
    size_t col_offset;
    const char *status_message;
} editor_state;

int find_next_match(const editor_state *s, const char *search_term, int start_row, int start_col,
                    int *match_row, int *match_col) {
    if (s == NULL || search_term == NULL || search_term[0] == '\0' || match_row == NULL ||
        match_col == NULL || start_row < 0 || start_col < 0 ||
        (size_t)start_row >= s->file_row_count ||
        (size_t)start_col > s->file_rows[start_row].length) {
        return -1;
    }

    for (int search_pass = 0; search_pass < 2; search_pass++) {
        size_t first_row;
        size_t last_row;

        if (search_pass == 0) {
            first_row = (size_t)start_row;
            last_row = s->file_row_count;
        } else {
            first_row = 0;
            last_row = (size_t)start_row + 1;
        }

        for (size_t row_index = first_row; row_index < last_row; row_index++) {
            size_t column = 0;

            if (search_pass == 0 && row_index == (size_t)start_row) {
                column = (size_t)start_col;
            }

            const editor_row *row = &s->file_rows[row_index];
            const char *match = strstr(row->chars + column, search_term);

            if (match != NULL) {
                *match_row = (int)row_index;
                *match_col = (int)(match - row->chars);
                return 0;
            }
        }
    }

    return -1;
}

void handle_resize(int signal_number) {
    (void)signal_number;
    resize_pending = 1;
}

void move_cursor_page_up(editor_state *s) {
    if (s == NULL || s->file_row_count == 0 || s->screen_rows < 2) {
        return;
    }

    size_t page_height = (size_t)(s->screen_rows - 1);
    size_t current_row = (size_t)s->cursor_y;
    size_t target_row;

    if (current_row > page_height) {
        target_row = current_row - page_height;
    } else {
        target_row = 0;
    }

    s->cursor_y = (int)target_row;

    if ((size_t)s->cursor_x > s->file_rows[target_row].length) {
        s->cursor_x = (int)s->file_rows[target_row].length;
    }
}

void move_cursor_page_down(editor_state *s) {
    if (s == NULL || s->file_row_count == 0 || s->screen_rows < 2) {
        return;
    }

    size_t page_height = (size_t)(s->screen_rows - 1);
    size_t last_row = s->file_row_count - 1;
    size_t current_row = (size_t)s->cursor_y;
    size_t target_row;

    if (page_height > last_row - current_row) {
        target_row = last_row;
    } else {
        target_row = current_row + page_height;
    }

    s->cursor_y = (int)target_row;

    if ((size_t)s->cursor_x > s->file_rows[target_row].length) {
        s->cursor_x = (int)s->file_rows[target_row].length;
    }
}

void move_cursor_home(editor_state *s) { s->cursor_x = 0; }

void move_cursor_end(editor_state *s) {
    if (s->cursor_y >= 0 && (size_t)s->cursor_y < s->file_row_count) {
        s->cursor_x = (int)s->file_rows[s->cursor_y].length;
    }
}

void scroll_cursor(editor_state *s) {
    size_t text_rows = (size_t)(s->screen_rows - 1);
    size_t text_cols = (size_t)s->screen_cols;

    if ((size_t)s->cursor_y < s->row_offset) {
        s->row_offset = (size_t)s->cursor_y;
    } else if ((size_t)s->cursor_y >= s->row_offset + text_rows) {
        s->row_offset = (size_t)s->cursor_y - text_rows + 1;
    }

    if ((size_t)s->cursor_x < s->col_offset) {
        s->col_offset = (size_t)s->cursor_x;
    } else if ((size_t)s->cursor_x >= s->col_offset + text_cols) {
        s->col_offset = (size_t)s->cursor_x - text_cols + 1;
    }
}

void draw_rows(const editor_state *s) {
    for (int i = 0; i < s->screen_rows - 1; i++) {
        size_t file_row = s->row_offset + (size_t)i;
        if (file_row < s->file_row_count) {
            const editor_row *row = &s->file_rows[file_row];
            size_t start = s->col_offset;
            size_t length = 0;

            if (start < row->length) {
                length = row->length - start;
                if (length > (size_t)s->screen_cols) {
                    length = s->screen_cols;
                }
            }

            for (size_t j = 0; j < length; j++) {
                putchar(row->chars[start + j]);
            }
        } else {
            putchar('~');
        }

        if ((int)i < s->screen_rows - 1) {
            printf("\r\n");
        }
    }
}

void draw_status_bar(const editor_state *s) {
    printf("\x1b[7m");

    char status[256];
    int status_length = 0;

    if (s->status_message != NULL) {
        status_length = snprintf(status, sizeof(status), "%s", s->status_message);
    } else {
        status_length = snprintf(status, sizeof(status), "%s | %zu lines | %d:%d",
                                 s->filename != NULL ? s->filename : "[No Name]", s->file_row_count,
                                 s->cursor_y + 1, s->cursor_x + 1);
    }

    if (status_length < 0) {
        status_length = 0;
    }

    if (status_length > (int)sizeof(status) - 1) {
        status_length = (int)sizeof(status) - 1;
    }

    if (status_length > s->screen_cols) {
        status_length = s->screen_cols;
    }

    fwrite(status, 1, (size_t)status_length, stdout);

    for (int i = status_length; i < s->screen_cols; i++) {
        putchar(' ');
    }

    printf("\x1b[m");
}

void refresh_screen(const editor_state *s) {
    printf("\x1b[?25l\x1b[2J\x1b[H");
    draw_rows(s);
    draw_status_bar(s);
    printf("\x1b[%d;%dH", s->cursor_y - (int)s->row_offset + 1,
           s->cursor_x - (int)s->col_offset + 1);
    printf("\x1b[?25h");
    fflush(stdout);
}

int get_window_size(int *rows, int *cols) {
    struct winsize dims;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &dims) == -1) {
        return -1;
    }

    *rows = dims.ws_row;
    *cols = dims.ws_col;
    return 0;
}

void restore_original(void) {
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
        perror("tcsetattr");
    }
}

int enable_raw_mode(void) {
    if (!tcgetattr(STDIN_FILENO, &original)) {
        if (atexit(restore_original) != 0) {
            fprintf(stderr, "Failed to register terminal restoration.\n");
            return -1;
        }
    } else {
        perror("tcgetattr");
        return -1;
    }

    struct termios raw = original;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

ssize_t read_byte(unsigned char *byte) {
    while (1) {
        ssize_t bytes_read = read(STDIN_FILENO, byte, 1);
        if (bytes_read == -1 && errno == EINTR) {
            continue;
        } else if (bytes_read == -1) {
            return -1;
        } else if (bytes_read == 0) {
            return 0;
        } else if (bytes_read == 1) {
            return 1;
        }
    }
}

int read_key(void) {
    unsigned char key;

    while (1) {
        if (resize_pending) {
            return KEY_RESIZE;
        }

        ssize_t bytes_read = read_byte(&key);
        if (bytes_read == -1) {
            perror("read");
            return -1;
        } else if (bytes_read == 0) {
            continue;
        } else if (bytes_read == 1) {
            if (key == '\x1b') {
                unsigned char seq[3];

                ssize_t s0 = read_byte(&seq[0]);
                if (s0 == -1) {
                    perror("read");
                    return -1;
                }
                if (s0 == 0) {
                    return '\x1b';
                }

                ssize_t s1 = read_byte(&seq[1]);
                if (s1 == -1) {
                    perror("read");
                    return -1;
                }
                if (s1 == 0) {
                    return '\x1b';
                }

                if (seq[0] == '[') {
                    switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME;
                    case 'F':
                        return END;
                    case '5':
                        if (read_byte(&seq[2]) == 1 && seq[2] == '~') {
                            return PAGE_UP;
                        }
                        return '\x1b';
                    case '6':
                        if (read_byte(&seq[2]) == 1 && seq[2] == '~') {
                            return PAGE_DOWN;
                        }
                        return '\x1b';
                    default:
                        return '\x1b';
                    }
                }
            }
            return key;
        }
    }
}

int insert_row(editor_state *state, size_t index, const char *chars, size_t length) {
    if (index > state->file_row_count) {
        return -1;
    }

    if (length == SIZE_MAX) {
        return -1;
    }

    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, chars, length);
    copy[length] = '\0';

    if (state->file_row_count == state->file_row_capacity) {
        size_t new_capacity;
        if (state->file_row_capacity == 0) {
            new_capacity = 8;
        } else {
            if (state->file_row_capacity > SIZE_MAX / 2) {
                free(copy);
                return -1;
            }

            new_capacity = state->file_row_capacity * 2;
        }

        if (new_capacity > SIZE_MAX / sizeof(*state->file_rows)) {
            free(copy);
            return -1;
        }

        editor_row *new_rows = realloc(state->file_rows, new_capacity * sizeof(*new_rows));

        if (new_rows == NULL) {
            free(copy);
            return -1;
        }

        state->file_row_capacity = new_capacity;
        state->file_rows = new_rows;
    }

    size_t rows_to_move = state->file_row_count - index;

    memmove(&state->file_rows[index + 1], &state->file_rows[index],
            rows_to_move * sizeof(*state->file_rows));

    editor_row *row = &state->file_rows[index];

    row->chars = copy;
    row->length = length;

    state->file_row_count++;

    return 0;
}

int append_row(editor_state *state, const char *chars, size_t length) {
    return insert_row(state, state->file_row_count, chars, length);
}

int load_file(editor_state *s) {
    if (s->filename == NULL) {
        return 0;
    }

    FILE *stream;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    stream = fopen(s->filename, "r");
    if (stream == NULL) {
        if (errno == ENOENT) {
            return 0;
        } else {
            perror(s->filename);
            return -1;
        }
    }

    while ((nread = getline(&line, &len, stream)) != -1) {
        s->final_newline = nread > 0 && line[nread - 1] == '\n';

        while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
            nread--;
        }

        if (append_row(s, line, (size_t)nread) == -1) {
            fprintf(stderr, "Failed to append row.\n");
            free(line);
            if (fclose(stream) == EOF) {
                perror("fclose");
            }
            return -1;
        }
    }

    if (ferror(stream)) {
        perror("getline");
        free(line);
        fclose(stream);
        return -1;
    }

    free(line);
    if (fclose(stream) == EOF) {
        perror("fclose");
        return -1;
    }

    return 0;
}

int save_file(const editor_state *s) {
    if (s->filename == NULL)
        return 0;

    FILE *fd;
    fd = fopen(s->filename, "w");
    if (fd == NULL) {
        perror(s->filename);
        return -1;
    }

    for (size_t i = 0; i < s->file_row_count; i++) {
        const editor_row *row = &s->file_rows[i];

        size_t written = fwrite(row->chars, 1, row->length, fd);

        if (written != row->length) {
            if (ferror(fd)) {
                perror("fwrite");
            } else {
                fprintf(stderr, "fwrite: short write\n");
            }

            fclose(fd);
            return -1;
        }

        if (i + 1 < s->file_row_count) {
            if (fputc('\n', fd) == EOF) {
                perror("fputc");
                fclose(fd);
                return -1;
            }
        }
    }

    if (s->final_newline) {
        if (fputc('\n', fd) == EOF) {
            perror("fputc");
            fclose(fd);
            return -1;
        }
    }

    if (fclose(fd) == EOF) {
        perror("fclose");
        return -1;
    }

    return 0;
}

int insert_char(editor_state *s, int key) {
    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    char *new_chars = realloc(row->chars, row->length + 2);

    if (new_chars == NULL) {
        return -1;
    }

    row->chars = new_chars;

    size_t position = (size_t)s->cursor_x;

    memmove(&row->chars[position + 1], &row->chars[position], row->length - position + 1);

    row->chars[position] = (char)key;
    row->length++;
    s->cursor_x++;
    s->dirty = 1;

    return 0;
}

int delete_char(editor_state *s) {
    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    if (s->cursor_x == 0) {
        if (s->cursor_y == 0) {
            return 0;
        }

        size_t row_index = (size_t)s->cursor_y;
        editor_row *previous = &s->file_rows[row_index - 1];
        size_t previous_length = previous->length;
        size_t current_length = row->length;

        if (previous_length > SIZE_MAX - current_length) {
            return -1;
        }

        size_t combined_length = previous_length + current_length;
        if (combined_length == SIZE_MAX) {
            return -1;
        }

        char *new_chars = realloc(previous->chars, combined_length + 1);
        if (new_chars == NULL) {
            return -1;
        }

        previous->chars = new_chars;
        memcpy(&previous->chars[previous_length], row->chars, current_length + 1);
        previous->length = combined_length;

        free(row->chars);
        memmove(row, &s->file_rows[row_index + 1],
                (s->file_row_count - row_index - 1) * sizeof(*s->file_rows));

        s->file_row_count--;
        s->cursor_y--;
        s->cursor_x = (int)previous_length;
        s->dirty = 1;

        return 0;
    }

    size_t position = (size_t)s->cursor_x;
    memmove(&row->chars[position - 1], &row->chars[position], row->length - position + 1);

    row->length--;
    s->cursor_x--;
    s->dirty = 1;

    return 0;
}

int insert_newline(editor_state *s) {
    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    size_t position = (size_t)s->cursor_x;
    size_t tail_length = row->length - position;

    if (insert_row(s, (size_t)s->cursor_y + 1, &row->chars[position], tail_length) == -1) {
        return -1;
    }

    row = &s->file_rows[s->cursor_y];

    row->length = position;
    row->chars[position] = '\0';

    s->cursor_y++;
    s->cursor_x = 0;
    s->dirty = 1;

    return 0;
}

void free_rows(editor_state *s) {
    for (size_t i = 0; i < s->file_row_count; i++) {
        free(s->file_rows[i].chars);
    }

    free(s->file_rows);

    s->file_rows = NULL;
    s->file_row_count = 0;
    s->file_row_capacity = 0;
}

int search_prompt(editor_state *s) {
    char query[256] = {0};
    char status[300];
    size_t length = 0;

    while (1) {
        snprintf(status, sizeof(status), "Search: %s", query);
        s->status_message = status;

        scroll_cursor(s);
        refresh_screen(s);

        int key = read_key();

        if (key == '\x1b') {
            s->status_message = NULL;
            return 0;
        }

        if (key == '\r' || key == '\n') {
            int match_row;
            int match_col;

            if (find_next_match(s, query, s->cursor_y, s->cursor_x, &match_row, &match_col) == 0) {
                s->cursor_y = match_row;
                s->cursor_x = match_col;
            } else {
                s->status_message = "Not found";
                refresh_screen(s);
                read_key();
            }

            s->status_message = NULL;
            return 0;
        }

        if (key == 127 || key == CTRL_KEY('h')) {
            if (length > 0) {
                query[--length] = '\0';
            }
            continue;
        }

        if (key >= 32 && key <= 126 && length < sizeof(query) - 1) {
            query[length++] = (char)key;
            query[length] = '\0';
        }

        if (key == -1) {
            s->status_message = NULL;
            return -1;
        }
    }
}

int main(int argc, char **argv) {
    editor_state p = {0};
    int key;
    int exit_status = EXIT_SUCCESS;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [filename]\n", argv[0]);
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (argc == 2)
        p.filename = argv[1];

    if (load_file(&p) == -1) {
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (p.file_row_count == 0) {
        if (append_row(&p, "", 0) == -1) {
            fprintf(stderr, "Failed to create initial row.\n");
            exit_status = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (enable_raw_mode() == -1) {
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (signal(SIGWINCH, handle_resize) == SIG_ERR) {
        perror("signal");
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
        perror("ioctl");
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (p.screen_rows < 2) {
        p.screen_rows = 2;
    }

    if (p.screen_cols < 1) {
        p.screen_cols = 1;
    }

    while (1) {
        if (resize_pending) {
            resize_pending = 0;

            if (get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
                perror("ioctl");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }

            if (p.screen_rows < 2) {
                p.screen_rows = 2;
            }

            if (p.screen_cols < 1) {
                p.screen_cols = 1;
            }
        }

        scroll_cursor(&p);
        refresh_screen(&p);
        key = read_key();

        if (key == KEY_RESIZE) {
            continue;
        }

        if (key == -1) {
            exit_status = EXIT_FAILURE;
            goto cleanup;
        }

        if (key == CTRL_KEY('q')) {
            if (p.dirty == 0) {
                break;
            } else {
                p.status_message = "Unsaved changes - press Ctrl-Q again to quit.";
                scroll_cursor(&p);
                refresh_screen(&p);
                if ((key = read_key()) == -1) {
                    exit_status = EXIT_FAILURE;
                    goto cleanup;
                }
                p.status_message = NULL;
                if (key == CTRL_KEY('q')) {
                    break;
                } else {
                    continue;
                }
            }
        }

        switch (key) {
        case ARROW_LEFT:
            if (p.cursor_x > 0)
                p.cursor_x--;
            break;
        case ARROW_RIGHT:
            if ((size_t)p.cursor_y < p.file_row_count &&
                (size_t)p.cursor_x < p.file_rows[p.cursor_y].length) {
                p.cursor_x++;
            }
            break;
        case ARROW_UP:
            if (p.cursor_y > 0) {
                p.cursor_y--;
                if ((size_t)p.cursor_x > p.file_rows[p.cursor_y].length) {
                    p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                }
            }
            break;
        case ARROW_DOWN:
            if ((size_t)(p.cursor_y + 1) < p.file_row_count) {
                p.cursor_y++;
                if ((size_t)p.cursor_x > p.file_rows[p.cursor_y].length) {
                    p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                }
            }
            break;
        case '\r':
        case '\n':
            if (insert_newline(&p) == -1) {
                fprintf(stderr, "Failed to insert newline.\n");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case CTRL_KEY('s'):
            if (save_file(&p) == -1) {
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            if (p.filename != NULL) {
                p.dirty = 0;
            }
            break;
        case 127:
        case CTRL_KEY('h'):
            if (delete_char(&p) == -1) {
                fprintf(stderr, "Failed to delete character.\n");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case CTRL_KEY('f'):
            if (search_prompt(&p) == -1) {
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case HOME:
            move_cursor_home(&p);
            break;
        case END:
            move_cursor_end(&p);
            break;
        case PAGE_UP:
            move_cursor_page_up(&p);
            break;
        case PAGE_DOWN:
            move_cursor_page_down(&p);
            break;
        default:
            if (key >= 32 && key <= 126) {
                if (insert_char(&p, key) == -1) {
                    fprintf(stderr, "Failed to insert character.\n");
                    exit_status = EXIT_FAILURE;
                    goto cleanup;
                }
            }
            break;
        }
    }

cleanup:
    free_rows(&p);
    return exit_status;
}
