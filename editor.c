#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#define CTRL_KEY(k)     ((k) & 0x1f)

struct termios original;

enum editor_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
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
} editor_state;

void draw_rows(const editor_state *s) {
    for (int i=0; i < s->screen_rows - 1; i++) {
        if((size_t)i < s->file_row_count) {
            size_t row_length = s->file_rows[i].length;
            if(s->file_rows[i].length > (size_t)s->screen_cols) {
                row_length = (size_t)s->screen_cols;
            }
            for (size_t j=0; j < row_length; j++) {
                putchar(s->file_rows[i].chars[j]);
            }
        } else {
            putchar('~');
        }

        if((int)i < s->screen_rows - 1) {
            printf("\r\n");
        }
    }
}

void draw_status_bar(const editor_state *s) {
    printf("\x1b[7m");

    char status[256];
    int status_length = snprintf(
        status,
        sizeof(status),
        "%s | %zu lines | %d:%d",
        s->filename != NULL ? s->filename : "[No Name]",
        s->file_row_count,
        s->cursor_y+1,
        s->cursor_x+1
    );

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

    for (int i=status_length; i < s->screen_cols; i++) {
        putchar(' ');
    }

    printf("\x1b[m");
}

void refresh_screen(const editor_state *s) {
    printf("\x1b[?25l\x1b[2J\x1b[H");
    draw_rows(s);
    draw_status_bar(s);
    printf("\x1b[%d;%dH", s->cursor_y+1, s->cursor_x+1);
    printf("\x1b[?25h");
    fflush(stdout);
}

int get_window_size(int *rows, int *cols) {
    struct winsize dims;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &dims) == -1) {
        return -1;
    }

    *rows = dims.ws_row;
    *cols = dims.ws_col;
    return 0;
}

void restore_original(void) {
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
        perror("tcsetattr");
    }
}

int enable_raw_mode(void) {
    if(!tcgetattr(STDIN_FILENO, &original)) {
        if(atexit(restore_original) != 0) {
            fprintf(stderr, "failed to register terminal restoration\n");
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

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

ssize_t read_byte(unsigned char *byte) {
    while (1) {
        ssize_t bytes_read = read(STDIN_FILENO, byte, 1);
	if(bytes_read == -1 && errno == EINTR) {
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
        ssize_t bytes_read = read_byte(&key);
        if (bytes_read == -1) {
	    perror("read");
	    return -1;
       	} else if (bytes_read == 0) {
            continue;
        } else if (bytes_read == 1) {
            if(key == '\x1b') {
                unsigned char seq[2];

                ssize_t s0 = read_byte(&seq[0]);
                if(s0 == -1) {
                    perror("read");
                    return -1;
                }
                if(s0 == 0) {
                    return '\x1b';
                }

                ssize_t s1 = read_byte(&seq[1]);
                if(s1 == -1) {
                    perror("read");
                    return -1;
                }
                if(s1 == 0) {
                    return '\x1b';
                }

                if(seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A':
                            return ARROW_UP;
                        case 'B':
                            return ARROW_DOWN;
                        case 'C':
                            return ARROW_RIGHT;
                        case 'D':
                            return ARROW_LEFT;
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

    if(length == SIZE_MAX) {
        return -1;
    }

    char *copy = malloc(length + 1);
    if(copy == NULL) {
        return -1;
    }

    memcpy(copy, chars, length);
    copy[length] = '\0';

    if (state->file_row_count == state->file_row_capacity) {
        size_t new_capacity;
        if (state->file_row_capacity == 0) {
            new_capacity = 8;   
        } else {
            if (state->file_row_capacity > SIZE_MAX /2) {
                free(copy);
                return -1;
            }

            new_capacity = state->file_row_capacity * 2;
        }

        if (new_capacity > SIZE_MAX / sizeof(*state->file_rows)) {
            free(copy);
            return -1;
        }

        editor_row *new_rows = realloc(
            state->file_rows,
            new_capacity * sizeof(*new_rows)
        );

        if(new_rows == NULL) {
            free(copy);
            return -1;
        }

        state->file_row_capacity = new_capacity;
        state->file_rows = new_rows;
    }

    size_t rows_to_move = state->file_row_count - index;

    memmove(
        &state->file_rows[index + 1],
        &state->file_rows[index],
        rows_to_move * sizeof(*state->file_rows)
    );

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
    if(s->filename == NULL) {
        return 0;
    }
    
    FILE *stream;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    stream = fopen(s->filename, "r");
    if(stream == NULL) {
        if(errno == ENOENT) {
            return 0;
        } else {
            perror(s->filename);
            return -1;
        }
    }

    while ((nread = getline(&line, &len, stream)) != -1) {
	s->final_newline = 
		nread > 0 && line[nread - 1] == '\n';

        while(nread > 0 &&
            (line[nread-1] == '\n' || line[nread-1] == '\r')) {
            nread--;
        }
        
        if(append_row(s, line, (size_t)nread) == -1) {
            fprintf(stderr, "failed to append row\n");
            free(line);
            if(fclose(stream) == EOF) {
                perror("fclose");
            }
            return -1;
        }
    }

    if(ferror(stream)) {
        perror("getline");
        free(line);
        fclose(stream);
        return -1;
    }

    free(line);
    if(fclose(stream) == EOF) {
        perror("fclose");
        return -1;
    }
    
    return 0;
}

int save_file(const editor_state *s) {
    if(s->filename == NULL)
        return 0;

    FILE *fd;
    fd = fopen(s->filename, "w");
    if(fd == NULL) {
        perror(s->filename);
        return -1;
    }

    for(size_t i = 0; i < s->file_row_count; i++) {
        const editor_row *row = &s->file_rows[i];

        size_t written = fwrite(
            row->chars,
            1,
            row->length,
            fd
        );

        if (written != row->length) {
            if(ferror(fd)) {
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

    if(s->final_newline) {
        if(fputc('\n', fd) == EOF) {
            perror("fputc");
            fclose(fd);
            return -1;
         }
     }

    if(fclose(fd) == EOF) {
        perror("fclose");
        return -1;
    }
    
    return 0;
}

int insert_char(editor_state *s, int key) {
    if(s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if(s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    char *new_chars = realloc(row->chars, row->length + 2);

    if (new_chars == NULL) {
        return -1;
    }

    row->chars = new_chars;

    size_t position = (size_t)s->cursor_x;

    memmove(
        &row->chars[position+1],
        &row->chars[position],
        row->length - position + 1
    );

    row->chars[position] = (char)key;
    row->length++;
    s->cursor_x++;
    s->dirty = 1;

    return 0;
}

int delete_char(editor_state *s) {
    if (s->cursor_y < 0 ||
        (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 ||
        (size_t)s->cursor_x > row->length) {
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
        memcpy(
            &previous->chars[previous_length],
            row->chars,
            current_length + 1
        );
        previous->length = combined_length;

        free(row->chars);
        memmove(
            row,
            &s->file_rows[row_index + 1],
            (s->file_row_count - row_index - 1) *
                sizeof(*s->file_rows)
        );

        s->file_row_count--;
        s->cursor_y--;
        s->cursor_x = (int)previous_length;
        s->dirty = 1;

        return 0;
    }

    size_t position = (size_t)s->cursor_x;
    memmove(
        &row->chars[position - 1],
        &row->chars[position],
        row->length - position + 1
    );

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

    if (insert_row(s, (size_t)s->cursor_y + 1, &row->chars[position],
                  tail_length) == -1) {
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

int main(int argc, char **argv) {
    editor_state p = {0};
    int key;
    int exit_status = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [filename]\n", argv[0]);
        exit_status = 1;
        goto cleanup;
    }

    if(argc == 2)
        p.filename = argv[1];

    if(load_file(&p) == -1) {
        exit_status = 1;
        goto cleanup;
    }

    if (p.file_row_count == 0) {
        if (append_row(&p, "", 0) == -1) {
            fprintf(stderr, "failed to create initial row\n");
            exit_status = 1;
            goto cleanup;
        }
    }

    if(enable_raw_mode() == -1) {
        exit_status = 1;
        goto cleanup;
    }

    if(get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
        perror("ioctl");
        exit_status = 1;
        goto cleanup;
    }

    while(1) {
        refresh_screen(&p);
        key = read_key();

        if(key == -1) {
            exit_status = 1;
            goto cleanup;
        }

        if(key == CTRL_KEY('q'))
            break;

        switch(key) {
            case ARROW_LEFT:
                if(p.cursor_x > 0)
                    p.cursor_x--;
                break;
            case ARROW_RIGHT:
                if((size_t)p.cursor_y < p.file_row_count &&
                    (size_t)p.cursor_x < p.file_rows[p.cursor_y].length &&
                    p.cursor_x < p.screen_cols-1) {
                    p.cursor_x++;
                }
                break;
            case ARROW_UP:
                if (p.cursor_y > 0) {
                    p.cursor_y--;
                    if((size_t)p.cursor_x >
                        p.file_rows[p.cursor_y].length) {
                        p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                    }
                }
                break;
            case ARROW_DOWN:
                if((size_t)(p.cursor_y + 1) < p.file_row_count &&
                    p.cursor_y + 1 < p.screen_rows - 1) {
                    p.cursor_y++;
                    if((size_t)p.cursor_x > p.file_rows[p.cursor_y].length) {
                        p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                    }
                }
                break;
            case '\r':
            case '\n':
                if(insert_newline(&p) == -1) {
                    fprintf(stderr, "failed to insert newline\n");
                    exit_status = 1;
                    goto cleanup;
                }
                break;
            case CTRL_KEY('s'):
                if(save_file(&p) == -1) {
                    exit_status = 1;
                    goto cleanup;
                }
                if(p.filename != NULL) {
                    p.dirty = 0;
                }
                break;
            case 127:
            case CTRL_KEY('h'):
                if (delete_char(&p) == -1) {
                    fprintf(stderr, "failed to delete character.\n");
                    exit_status = 1;
                    goto cleanup;
                }
                break;
            default:
                if (key >=32 && key <= 126) {
                    if (insert_char(&p, key) == -1) {
                        fprintf(stderr, "failed to insert character.\n");
                        exit_status = 1;
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
