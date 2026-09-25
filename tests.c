#define main editor_program_main
#include "editor.c"
#undef main

#include <fcntl.h>

static int failures;

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_editing_operations(void) {
    editor_state state = {0};

    check(append_row(&state, "abc", 3) == 0, "append initial row");

    state.cursor_x = 1;
    check(insert_char(&state, 'X') == 0, "insert character");
    check(strcmp(state.file_rows[0].chars, "aXbc") == 0, "inserted character content");
    check(state.cursor_x == 2, "cursor advances after insertion");

    check(delete_char(&state) == 0, "delete character");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "deleted character content");
    check(state.cursor_x == 1, "cursor retreats after deletion");

    check(insert_newline(&state) == 0, "split row at cursor");
    check(state.file_row_count == 2, "row count after split");
    check(strcmp(state.file_rows[0].chars, "a") == 0, "first row after split");
    check(strcmp(state.file_rows[1].chars, "bc") == 0, "second row after split");

    check(delete_char(&state) == 0, "join rows at column zero");
    check(state.file_row_count == 1, "row count after join");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "joined row content");

    free_rows(&state);
}

static void test_scrolling(void) {
    editor_state state = {
        .cursor_y = 9,
        .cursor_x = 19,
        .screen_rows = 5,
        .screen_cols = 10,
    };

    scroll_cursor(&state);
    check(state.row_offset == 6, "vertical scroll follows cursor");
    check(state.col_offset == 10, "horizontal scroll follows cursor");

    state.cursor_y = 2;
    state.cursor_x = 4;
    scroll_cursor(&state);
    check(state.row_offset == 2, "vertical scroll follows cursor upward");
    check(state.col_offset == 4, "horizontal scroll follows cursor left");
}

static void test_load_save(void) {
    char path[] = "/tmp/gg-editor-test-XXXXXX";
    int fd = mkstemp(path);
    check(fd != -1, "create temporary file");
    if (fd == -1) {
        return;
    }

    const char input[] = "first\nsecond\n";
    check(write(fd, input, sizeof(input) - 1) == (ssize_t)(sizeof(input) - 1),
          "write temporary file");
    check(close(fd) == 0, "close temporary file");

    editor_state state = {.filename = path};
    check(load_file(&state) == 0, "load file");
    check(state.file_row_count == 2, "loaded row count");
    check(state.final_newline != 0, "preserve final newline");
    check(save_file(&state) == 0, "save file");

    fd = open(path, O_RDONLY);
    check(fd != -1, "reopen saved file");
    if (fd != -1) {
        char output[sizeof(input)] = {0};
        ssize_t length = read(fd, output, sizeof(output));
        check(length == (ssize_t)(sizeof(input) - 1), "saved file length");
        check(memcmp(output, input, sizeof(input) - 1) == 0, "saved file content");
        close(fd);
    }

    free_rows(&state);
    unlink(path);
}

int main(void) {
    test_editing_operations();
    test_scrolling();
    test_load_save();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("all tests passed");
    return 0;
}
