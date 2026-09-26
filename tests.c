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

static void test_home_end_navigation(void) {
    editor_state state = {0};

    check(append_row(&state, "hello", 5) == 0, "append Home/End test row");
    state.cursor_x = 3;

    move_cursor_home(&state);
    check(state.cursor_x == 0, "Home moves to the beginning of the line");

    move_cursor_end(&state);
    check(state.cursor_x == 5, "End moves to the end of the line");

    free_rows(&state);
}

static void test_page_navigation(void) {
    editor_state state = {
        .cursor_y = 0,
        .cursor_x = 4,
        .screen_rows = 4,
    };

    check(append_row(&state, "first", 5) == 0, "append first page row");
    check(append_row(&state, "hello", 5) == 0, "append second page row");
    check(append_row(&state, "two", 3) == 0, "append third page row");
    check(append_row(&state, "three", 5) == 0, "append fourth page row");
    check(append_row(&state, "end", 3) == 0, "append fifth page row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 3, "Page Down moves by the text viewport height");
    check(state.cursor_x == 4, "Page Down preserves the requested column when possible");

    move_cursor_page_up(&state);
    check(state.cursor_y == 0, "Page Up moves by the text viewport height");
    check(state.cursor_x == 4, "Page Up restores the requested column when possible");

    state.cursor_y = 1;
    state.cursor_x = 4;
    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down moves to the final row");
    check(state.cursor_x == 3, "Page Down clamps to a shorter destination row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down clamps at the last row");

    free_rows(&state);
}

static void test_search(void) {
    editor_state state = {0};
    int match_row;
    int match_col;

    check(append_row(&state, "alpha beta", 10) == 0, "append first search row");
    check(append_row(&state, "gamma alpha", 11) == 0, "append second search row");

    check(find_next_match(&state, "alpha", 0, 0, &match_row, &match_col) == 0,
          "find first search match");
    check(match_row == 0 && match_col == 0, "first search match position");

    check(find_next_match(&state, "alpha", 0, 1, &match_row, &match_col) == 0,
          "find next search match");
    check(match_row == 1 && match_col == 6, "next search match position");

    check(find_next_match(&state, "alpha", 1, 7, &match_row, &match_col) == 0,
          "search wraps around at end of file");
    check(match_row == 0 && match_col == 0, "wrapped search match position");

    check(find_next_match(&state, "missing", 0, 0, &match_row, &match_col) == -1,
          "search reports a missing match");

    check(find_next_match(&state, "", 0, 0, &match_row, &match_col) == -1,
          "empty search query is rejected");

    check(find_next_match(&state, "beta", 0, 6, &match_row, &match_col) == 0,
          "search includes a match at the starting column");
    check(match_row == 0 && match_col == 6, "exact starting-column match position");

    check(find_next_match(&state, "beta", 0, 7, &match_row, &match_col) == 0,
          "search wraps within the starting row");
    check(match_row == 0 && match_col == 6, "same-row wrapped match position");

    check(find_next_match(&state, "alpha", -1, 0, &match_row, &match_col) == -1,
          "negative starting row is rejected");
    check(find_next_match(&state, "alpha", 0, 99, &match_row, &match_col) == -1,
          "starting column past the row is rejected");

    free_rows(&state);
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
    test_home_end_navigation();
    test_page_navigation();
    test_search();
    test_load_save();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("all tests passed");
    return 0;
}
