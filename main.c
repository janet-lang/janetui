/*
* Copyright (c) 2017 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <janet/janet.h>

#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"

/* Types */
#define UI_FLAG_DESTROYED 1
typedef struct {
    uiControl *control;
    uint32_t flags;
} UIControlWrapper;
static const JanetAbstractType control_td = {":ui.control", NULL, NULL};
static const JanetAbstractType window_td = {":ui.window", NULL, NULL};
static const JanetAbstractType button_td = {":ui.button", NULL, NULL};
static const JanetAbstractType box_td = {":ui.box", NULL, NULL};
static const JanetAbstractType checkbox_td = {":ui.checkbox", NULL, NULL};
static const JanetAbstractType entry_td = {":ui.entry", NULL, NULL};
static const JanetAbstractType label_td = {":ui.label", NULL, NULL};
static const JanetAbstractType tab_td = {":ui.tab", NULL, NULL};
static const JanetAbstractType group_td = {":ui.group", NULL, NULL};
static const JanetAbstractType spinbox_td = {":ui.spinbox", NULL, NULL};
static const JanetAbstractType slider_td = {":ui.slider", NULL, NULL};
static const JanetAbstractType progress_bar_td = {":ui.progress-bar", NULL, NULL};
static const JanetAbstractType separator_td = {":ui.separator", NULL, NULL};
static const JanetAbstractType combobox_td = {":ui.combobox", NULL, NULL};
static const JanetAbstractType editable_combobox_td = {":ui.editable-combobox", NULL, NULL};
static const JanetAbstractType radio_buttons_td = {":ui.radio-buttons", NULL, NULL};
static const JanetAbstractType date_time_picker_td = {":ui.date-time-picker", NULL, NULL};
static const JanetAbstractType multiline_entry_td = {":ui.multiline-entry", NULL, NULL};
static const JanetAbstractType menu_item_td = {":ui.menu-item", NULL, NULL};
static const JanetAbstractType menu_td = {":ui.menu", NULL, NULL};

/* Helpers */

#define JANET_ARG_UITYPE(DEST, A, N, AT) do { \
    JANET_CHECKABSTRACT(A, N, AT); \
    memcpy(&(DEST), janet_unwrap_abstract((A).v[(N)]), sizeof(DEST)); \
} while (0)

/* Cast a Janet into a uiControl structure. Returns a pointer to
 * the uiControl, or NULL if cast fails. */
static uiControl *to_control(Janet x) {
    if (!janet_checktype(x, JANET_ABSTRACT))
        return NULL;
    UIControlWrapper *abst = janet_unwrap_abstract(x);
    if (abst->flags & UI_FLAG_DESTROYED)
        return NULL;
    const JanetAbstractType *at = janet_abstract_type(abst);
    if (at == &control_td ||
        at == &window_td ||
        at == &button_td ||
        at == &box_td ||
        at == &checkbox_td ||
        at == &entry_td ||
        at == &label_td ||
        at == &tab_td ||
        at == &group_td ||
        at == &spinbox_td ||
        at == &slider_td ||
        at == &progress_bar_td ||
        at == &separator_td ||
        at == &combobox_td ||
        at == &editable_combobox_td ||
        at == &radio_buttons_td ||
        at == &date_time_picker_td ||
        at == &multiline_entry_td ||
        at == &menu_item_td ||
        at == &menu_td) return uiControl(abst->control);
    return NULL;
}

#define JANET_ARG_CONTROL(DEST, A, N) do {\
    JANET_CHECK((A), (N), JANET_ABSTRACT); \
    DEST = to_control((A).v[(N)]); \
    if (NULL == DEST) JANET_THROW((A), "expected alive ui control"); \
} while (0);

/* Wrap a pointer to a uiXxx object into an abstract */
static Janet janet_ui_handle_to_control(void *handle, const JanetAbstractType *atype) {
    UIControlWrapper *abst = janet_abstract(atype, sizeof(UIControlWrapper));
    abst->control = handle;
    abst->flags = 0;
    return janet_wrap_abstract(abst);
}

/* Convert a function or cfunction to a handle for
 * libui callbacks */
static void *janet_ui_to_handler_data(Janet handler) {
    const Janet *tup = janet_tuple_n(&handler, 1);
    janet_gcroot(janet_wrap_tuple(tup));
    return (void *)tup;
}

/* Get the function or cfunction from a libui callback
 * handle data pointer */
static Janet janet_ui_from_handler_data(void *data) {
    const Janet *tup = (const Janet *)data;
    return tup[0];
}

/* Generic handler */
static int janet_ui_handler(void *data) {
    Janet funcv = janet_ui_from_handler_data(data);
    /* Tuple should already be GC root */
    if (janet_checktype(funcv, JANET_FUNCTION)) {
        Janet in, out;
        JanetFunction *func = janet_unwrap_function(funcv);
        JanetFiber *fiber = janet_fiber(func, 64);
        in = janet_wrap_nil();
        janet_gcroot(janet_wrap_fiber(fiber));
        /* handle status? We should eventually expose
         * a function to call on ui handler errors. */
        int status = janet_continue(fiber, in, &out);
        janet_gcunroot(janet_wrap_fiber(fiber));
        if (status) {
            janet_puts(janet_formatc("janet error: %S\n", janet_to_string(out)));
        }
        return status;
    } else if (janet_checktype(funcv, JANET_CFUNCTION)) {
        Janet ret;
        JanetCFunction cfunc = janet_unwrap_cfunction(funcv);
        JanetArgs args;
        args.n = 0;
        args.v = NULL;
        args.ret = &ret;
        return cfunc(args);
    } else {
        printf("called invalid handler\n");
        return 1;
    }
}

static void janet_ui_handler_void(void *data) {
    janet_ui_handler(data);
}

/* Global state */

static JANET_THREAD_LOCAL int inited = 0;
#define ASSERT_INITED(args) do {\
    if (!inited) { \
        const char *initerr; \
        uiInitOptions o = {0}; \
        if ((initerr = uiInit(&o)) != NULL) { \
            Janet err = janet_cstringv(initerr); \
            uiFreeInitError(initerr); \
            JANET_THROWV(args, err);\
        } \
        inited = 1; \
    } \
} while (0)

static int janet_ui_init(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN_NIL(args);
}

static int janet_ui_quit(JanetArgs args) {
    ASSERT_INITED(args);
    uiQuit();
    JANET_RETURN_NIL(args);
}

static int janet_ui_uninit(JanetArgs args) {
    ASSERT_INITED(args);
    uiUninit();
    JANET_RETURN_NIL(args);
}

static int janet_ui_main(JanetArgs args) {
    ASSERT_INITED(args);
    uiMain();
    JANET_RETURN_NIL(args);
}

static int janet_ui_mainstep(JanetArgs args) {
    ASSERT_INITED(args);
    int32_t step = 0;
    JANET_FIXARITY(args, 1);
    JANET_ARG_INTEGER(step, args, 0);
    uiMainStep(step);
    JANET_RETURN_NIL(args);
}

static int janet_ui_mainsteps(JanetArgs args) {
    ASSERT_INITED(args);
    uiMainSteps();
    JANET_RETURN_NIL(args);
}

static int janet_ui_queue_main(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 1);
    JANET_CHECKMANY(args, 0, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[0]);
    uiQueueMain(janet_ui_handler_void, handle);
    JANET_RETURN_NIL(args);
}

static int janet_ui_on_should_quit(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 1);
    JANET_CHECKMANY(args, 0, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[0]);
    uiOnShouldQuit(janet_ui_handler, handle);
    JANET_RETURN_NIL(args);
}

static int janet_ui_timer(JanetArgs args) {
    int32_t milliseconds;
    JANET_FIXARITY(args, 2);
    JANET_ARG_INTEGER(milliseconds, args, 0);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiTimer(milliseconds, janet_ui_handler, handle);
    JANET_RETURN_NIL(args);
}

static int janet_ui_open_file(JanetArgs args) {
    uiWindow *window;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    char *str = uiOpenFile(window);
    if (str)
        JANET_RETURN_CSTRING(args, str);
    JANET_RETURN_NIL(args);
}

static int janet_ui_save_file(JanetArgs args) {
    uiWindow *window;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    char *str = uiSaveFile(window);
    if (str)
        JANET_RETURN_CSTRING(args, str);
    JANET_RETURN_NIL(args);
}

static int janet_ui_message_box(JanetArgs args) {
    uiWindow *window;
    const uint8_t *title, *description;
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    JANET_ARG_STRING(title, args, 1);
    JANET_ARG_STRING(description, args, 2);
    uiMsgBox(window, (const char *)title, (const char *)description);
    JANET_RETURN_NIL(args);
}

static int janet_ui_message_box_error(JanetArgs args) {
    uiWindow *window;
    const uint8_t *title, *description;
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    JANET_ARG_STRING(title, args, 1);
    JANET_ARG_STRING(description, args, 2);
    uiMsgBoxError(window, (const char *)title, (const char *)description);
    JANET_RETURN_NIL(args);
}

/* Generic controls */

static int janet_ui_destroy(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    uiControlDestroy(c);
    JANET_RETURN_NIL(args);
}

static int janet_ui_parent(JanetArgs args) {
    uiControl *c, *d;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_CONTROL(c, args, 0);
    if (args.n == 2) {
        JANET_ARG_CONTROL(d, args, 1);
        uiControlSetParent(c, d);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN(args, janet_ui_handle_to_control(uiControlParent(c), &control_td));
}

static int janet_ui_top_level(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    JANET_RETURN_BOOLEAN(args, uiControlToplevel(c));
}

static int janet_ui_visible(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    JANET_RETURN_BOOLEAN(args, uiControlVisible(c));
}

static int janet_ui_enabled(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    JANET_RETURN_BOOLEAN(args, uiControlEnabled(c));
}

static int janet_ui_show(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    uiControlShow(c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_hide(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    uiControlHide(c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_enable(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    uiControlEnable(c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_disable(JanetArgs args) {
    uiControl *c;
    JANET_ARG_CONTROL(c, args, 0);
    uiControlDisable(c);
    JANET_RETURN(args, args.v[0]);
}

/* Window */

static int onClosing(uiWindow *w, void *data) {
  uiQuit();
  return 1;
}

static int janet_ui_window(JanetArgs args) {
    ASSERT_INITED(args);
    const char *title = "";
    int32_t width = 800;
    int32_t height = 600;
    int menuBar = 0;
    JANET_MAXARITY(args, 4);
    if (args.n >= 1) {
        const uint8_t *title_;
        JANET_ARG_STRING(title_, args, 0);
        title = (const char *)title_;
    }
    if (args.n >= 2)
        JANET_ARG_INTEGER(width, args, 1);
    if (args.n >= 3)
        JANET_ARG_INTEGER(height, args, 2);
    if (args.n >= 4)
        JANET_ARG_BOOLEAN(menuBar, args, 3);
    uiWindow *window = uiNewWindow(title, width, height, menuBar);
    if (NULL == window) {
        JANET_THROW(args, "could not create window");
    }
    uiWindowOnClosing(window, onClosing, NULL);
    JANET_RETURN(args, janet_ui_handle_to_control(window, &window_td));
}

static int janet_ui_window_title(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        const uint8_t *newTitle;
        JANET_ARG_STRING(newTitle, args, 1);
        uiWindowSetTitle(window, (const char *)newTitle);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiWindowTitle(window));
}

static int janet_ui_window_content_size(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 3);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 1) {
        int w = 0, h = 0;
        uiWindowContentSize(window, &w, &h);
        Janet *tup = janet_tuple_begin(2);
        tup[0] = janet_wrap_integer(w);
        tup[1] = janet_wrap_integer(h);
        JANET_RETURN_TUPLE(args, janet_tuple_end(tup));
    } else if (args.n == 2) {
        int32_t wh;
        JANET_ARG_INTEGER(wh, args, 1);
        uiWindowSetContentSize(window, wh, wh);
    } else {
        int32_t w, h;
        JANET_ARG_INTEGER(w, args, 1);
        JANET_ARG_INTEGER(h, args, 2);
        uiWindowSetContentSize(window, w, h);
    }
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_window_fullscreen(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int full = 0;
        JANET_ARG_BOOLEAN(full, args, 1);
        uiWindowSetFullscreen(window, full);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiWindowFullscreen(window));
}

static int window_closing_handler(uiWindow *window, void *data) {
    (void) window;
    return janet_ui_handler(data);
}

static void window_content_size_changed_handler(uiWindow *window, void *data) {
    (void) window;
    janet_ui_handler(data);
}

static int janet_ui_window_on_closing(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    uiWindowOnClosing(window, window_closing_handler,
            janet_ui_to_handler_data(args.v[1]));
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_window_on_content_size_changed(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    uiWindowOnContentSizeChanged(window, window_content_size_changed_handler,
            janet_ui_to_handler_data(args.v[1]));
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_window_set_child(JanetArgs args) {
    uiControl *c = NULL;
    uiWindow *window = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    JANET_ARG_CONTROL(c, args, 1);
    uiWindowSetChild(window, c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_window_borderless(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int borders = 0;
        JANET_ARG_BOOLEAN(borders, args, 1);
        uiWindowSetBorderless(window, borders);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiWindowBorderless(window));
}

static int janet_ui_window_margined(JanetArgs args) {
    uiWindow *window = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int margined = 0;
        JANET_ARG_BOOLEAN(margined, args, 1);
        uiWindowSetMargined(window, margined);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiWindowMargined(window));
}

/* Button */
static int janet_ui_button(JanetArgs args) {
    ASSERT_INITED(args);
    const uint8_t *text;
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(text, args, 0);
    uiButton *button = uiNewButton((const char *)text);
    JANET_RETURN(args, janet_ui_handle_to_control(button, &button_td));
}

static int janet_ui_button_text(JanetArgs args) {
    uiButton *button = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(button, args, 0, &button_td);
    if (args.n == 2) {
        const uint8_t *newText;
        JANET_ARG_STRING(newText, args, 1);
        uiButtonSetText(button, (const char *)newText);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiButtonText(button));
}

static void button_click_handler(uiButton *button, void *data) {
    (void)button;
    janet_ui_handler(data);
}

static int janet_ui_button_on_clicked(JanetArgs args) {
    uiButton *button = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(button, args, 0, &button_td);
    uiButtonOnClicked(button, button_click_handler,
            janet_ui_to_handler_data(args.v[1]));
    JANET_RETURN(args, args.v[0]);
}

/* Box */

static int janet_ui_horizontal_box(JanetArgs args) {
    ASSERT_INITED(args);
    uiBox *box = uiNewHorizontalBox();
    JANET_RETURN(args, janet_ui_handle_to_control(box, &box_td));
}

static int janet_ui_vertical_box(JanetArgs args) {
    ASSERT_INITED(args);
    uiBox *box = uiNewVerticalBox();
    JANET_RETURN(args, janet_ui_handle_to_control(box, &box_td));
}

static int janet_ui_box_append(JanetArgs args) {
    uiControl *c;
    uiBox *box;
    int stretchy = 0;
    JANET_MINARITY(args, 2);
    JANET_MAXARITY(args, 3);
    JANET_ARG_UITYPE(box, args, 0, &box_td);
    JANET_ARG_CONTROL(c, args, 1);
    if (args.n == 3) {
        JANET_ARG_BOOLEAN(stretchy, args, 2);
    }
    uiBoxAppend(box, c, stretchy);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_box_delete(JanetArgs args) {
    uiBox *box;
    int32_t index;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(box, args, 0, &box_td);
    JANET_ARG_INTEGER(index, args, 1);
    uiBoxDelete(box, index);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_box_padded(JanetArgs args) {
    uiBox *box = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(box, args, 0, &box_td);
    if (args.n == 2) {
        int padded = 0;
        JANET_ARG_BOOLEAN(padded, args, 1);
        uiBoxSetPadded(box, padded);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiBoxPadded(box));
}

/* Checkbox */

static int janet_ui_checkbox(JanetArgs args) {
    ASSERT_INITED(args);
    const uint8_t *text;
    uiCheckbox *cbox;
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(text, args, 0);
    cbox = uiNewCheckbox((const char *)text);
    JANET_RETURN(args, janet_ui_handle_to_control(cbox, &checkbox_td));
}

static int janet_ui_checkbox_text(JanetArgs args) {
    uiCheckbox *cbox;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    if (args.n == 2) {
        const uint8_t *text;
        JANET_ARG_STRING(text, args, 1);
        uiCheckboxSetText(cbox, (const char *)text);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiCheckboxText(cbox));
}

static int janet_ui_checkbox_checked(JanetArgs args) {
    uiCheckbox *cbox;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    if (args.n == 2) {
        int checked;
        JANET_ARG_BOOLEAN(checked, args, 1);
        uiCheckboxSetChecked(cbox, checked);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiCheckboxChecked(cbox));
}

static void on_toggled_handler(uiCheckbox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static int janet_ui_checkbox_on_toggled(JanetArgs args) {
    uiCheckbox *cbox;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiCheckboxOnToggled(cbox, on_toggled_handler, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Entry */

static int janet_ui_entry(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 0);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewEntry(), &entry_td));
}

static int janet_ui_password_entry(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 0);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewPasswordEntry(), &entry_td));
}

static int janet_ui_search_entry(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 0);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewSearchEntry(), &entry_td));
}

static int janet_ui_entry_text(JanetArgs args) {
    uiEntry *entry;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(entry, args, 0, &entry_td);
    if (args.n == 2) {
        const uint8_t *text;
        JANET_ARG_STRING(text, args, 1);
        uiEntrySetText(entry, (const char *)text);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiEntryText(entry));
}

static int janet_ui_entry_read_only(JanetArgs args) {
    uiEntry *entry;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(entry, args, 0, &entry_td);
    if (args.n == 2) {
        int readonly;
        JANET_ARG_BOOLEAN(readonly, args, 1);
        uiEntrySetReadOnly(entry, readonly);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiEntryReadOnly(entry));
}

static void on_entry_changed(uiEntry *e, void *data) {
    (void) e;
    janet_ui_handler(data);
}

static int janet_ui_entry_on_changed(JanetArgs args) {
    uiEntry *entry;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(entry, args, 0, &entry_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiEntryOnChanged(entry, on_entry_changed, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Label */

static int janet_ui_label(JanetArgs args) {
    const uint8_t *text;
    ASSERT_INITED(args);
    JANET_ARG_STRING(text, args, 0);
    uiLabel *label = uiNewLabel((const char *)text);
    JANET_RETURN(args, janet_ui_handle_to_control(label, &label_td));
}

static int janet_ui_label_text(JanetArgs args) {
    uiLabel *label;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(label, args, 0, &label_td);
    if (args.n == 2) {
        const uint8_t *text;
        JANET_ARG_STRING(text, args, 1);
        uiLabelSetText(label, (const char *)text);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiLabelText(label));
}

/* Tab */

static int janet_ui_tab(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewTab(), &tab_td));
}

static int janet_ui_tab_margined(JanetArgs args) {
    uiTab *tab = NULL;
    int32_t page;
    JANET_MINARITY(args, 2);
    JANET_MAXARITY(args, 3);
    JANET_ARG_UITYPE(tab, args, 0, &tab_td);
    JANET_ARG_INTEGER(page, args, 1);
    if (args.n == 3) {
        int margined = 0;
        JANET_ARG_BOOLEAN(margined, args, 2);
        uiTabSetMargined(tab, page, margined);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiTabMargined(tab, page));
}

static int janet_ui_tab_num_pages(JanetArgs args) {
    uiTab *tab = NULL;
    JANET_ARG_UITYPE(tab, args, 0, &tab_td);
    JANET_RETURN_INTEGER(args, uiTabNumPages(tab));
}

static int janet_ui_tab_append(JanetArgs args) {
    uiTab *tab;
    uiControl *c;
    const uint8_t *name;
    JANET_ARG_UITYPE(tab, args, 0, &tab_td);
    JANET_ARG_STRING(name, args, 1);
    JANET_ARG_CONTROL(c, args, 2);
    uiTabAppend(tab, (const char *)name, c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_tab_insert_at(JanetArgs args) {
    uiTab *tab;
    uiControl *c;
    const uint8_t *name;
    int32_t at;
    JANET_ARG_UITYPE(tab, args, 0, &tab_td);
    JANET_ARG_STRING(name, args, 1);
    JANET_ARG_INTEGER(at, args, 2);
    JANET_ARG_CONTROL(c, args, 3);
    uiTabInsertAt(tab, (const char *)name, at, c);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_tab_delete(JanetArgs args) {
    uiTab *tab;
    int32_t at;
    JANET_ARG_UITYPE(tab, args, 0, &tab_td);
    JANET_ARG_INTEGER(at, args, 1);
    uiTabDelete(tab, at);
    JANET_RETURN(args, args.v[0]);
}

/* Group */

static int janet_ui_group(JanetArgs args) {
    const uint8_t *title;
    ASSERT_INITED(args);
    JANET_ARG_STRING(title, args, 0);
    uiGroup *group = uiNewGroup((const char *)title);
    JANET_RETURN(args, janet_ui_handle_to_control(group, &group_td));
}

static int janet_ui_group_title(JanetArgs args) {
    uiGroup *group;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(group, args, 0, &group_td);
    if (args.n == 2) {
        const uint8_t *title;
        JANET_ARG_STRING(title, args, 1);
        uiGroupSetTitle(group, (const char *)title);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiGroupTitle(group));
}

static int janet_ui_group_margined(JanetArgs args) {
    uiGroup *group = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(group, args, 0, &group_td);
    if (args.n == 2) {
        int margined = 0;
        JANET_ARG_BOOLEAN(margined, args, 1);
        uiGroupSetMargined(group, margined);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiGroupMargined(group));
}

static int janet_ui_group_set_child(JanetArgs args) {
    uiGroup *group = NULL;
    uiControl *c;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(group, args, 0, &group_td);
    JANET_ARG_CONTROL(c, args, 1);
    uiGroupSetChild(group, c);
    JANET_RETURN_BOOLEAN(args, uiGroupMargined(group));
}

/* Spinbox */

static int janet_ui_spinbox(JanetArgs args) {
    uiSpinbox *spinbox = NULL;
    int32_t min, max;
    JANET_FIXARITY(args, 2);
    JANET_ARG_INTEGER(min, args, 0);
    JANET_ARG_INTEGER(max, args, 1);
    spinbox = uiNewSpinbox(min, max);
    JANET_RETURN(args, janet_ui_handle_to_control(spinbox, &spinbox_td));
}

static int janet_ui_spinbox_value(JanetArgs args) {
    uiSpinbox *spinbox = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(spinbox, args, 0, &spinbox_td);
    if (args.n == 2) {
        int32_t value;
        JANET_ARG_INTEGER(value, args, 1);
        uiSpinboxSetValue(spinbox, value);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_INTEGER(args, uiSpinboxValue(spinbox));
}

static void spinbox_on_changed(uiSpinbox *sb, void *data) {
    (void) sb;
    janet_ui_handler(data);
}

static int janet_ui_spinbox_on_changed(JanetArgs args) {
    uiSpinbox *spinbox = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(spinbox, args, 0, &spinbox_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiSpinboxOnChanged(spinbox, spinbox_on_changed, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Slider */

static int janet_ui_slider(JanetArgs args) {
    int32_t min, max;
    ASSERT_INITED(args);
    JANET_ARG_INTEGER(min, args, 0);
    JANET_ARG_INTEGER(max, args, 1);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewSlider(min, max), &slider_td));
}

static int janet_ui_slider_value(JanetArgs args) {
    uiSlider *slider = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(slider, args, 0, &slider_td);
    if (args.n == 2) {
        int32_t value;
        JANET_ARG_INTEGER(value, args, 1);
        uiSliderSetValue(slider, value);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_INTEGER(args, uiSliderValue(slider));
}

static void slider_on_changed(uiSlider *s, void *data) {
    (void) s;
    janet_ui_handler(data);
}

static int janet_ui_slider_on_changed(JanetArgs args) {
    uiSlider *slider = NULL;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(slider, args, 0, &slider_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiSliderOnChanged(slider, slider_on_changed, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Progress Bar */

static int janet_ui_progress_bar(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewProgressBar(), &progress_bar_td));
}

static int janet_ui_progress_bar_value(JanetArgs args) {
    uiProgressBar *bar = NULL;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(bar, args, 0, &progress_bar_td);
    if (args.n == 2) {
        int32_t value;
        JANET_ARG_INTEGER(value, args, 1);
        uiProgressBarSetValue(bar, value);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_INTEGER(args, uiProgressBarValue(bar));
}

/* Separator */

static int janet_ui_horizontal_separator(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewHorizontalSeparator(), &separator_td));
}

static int janet_ui_vertical_separator(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewVerticalSeparator(), &separator_td));
}

/* Combobox */

static int janet_ui_combobox(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewCombobox(), &combobox_td));
}

static int janet_ui_combobox_selected(JanetArgs args) {
    uiCombobox *cbox;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &combobox_td);
    if (args.n == 2) {
        int selected = 0;
        JANET_ARG_BOOLEAN(selected, args, 1);
        uiComboboxSetSelected(cbox, selected);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_INTEGER(args, uiComboboxSelected(cbox));
}

static int janet_ui_combobox_append(JanetArgs args) {
    uiCombobox *cbox;
    const uint8_t *text;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &combobox_td);
    JANET_ARG_STRING(text, args, 1);
    uiComboboxAppend(cbox, (const char *)text);
    JANET_RETURN(args, args.v[0]);
}

static void combobox_on_selected(uiCombobox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static int janet_ui_combobox_on_selected(JanetArgs args) {
    uiCombobox *cbox;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &combobox_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiComboboxOnSelected(cbox, combobox_on_selected, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Editable Combobox */

static int janet_ui_editable_combobox(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewEditableCombobox(), &editable_combobox_td));
}

static int janet_ui_editable_combobox_text(JanetArgs args) {
    uiEditableCombobox *cbox;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    if (args.n == 2) {
        const uint8_t *text;
        JANET_ARG_STRING(text, args, 1);
        uiEditableComboboxSetText(cbox, (const char *)text);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiEditableComboboxText(cbox));
}

static int janet_ui_editable_combobox_append(JanetArgs args) {
    uiEditableCombobox *cbox;
    const uint8_t *text;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    JANET_ARG_STRING(text, args, 1);
    uiEditableComboboxAppend(cbox, (const char *)text);
    JANET_RETURN(args, args.v[0]);
}

static void editable_combobox_on_changed(uiEditableCombobox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static int janet_ui_editable_combobox_on_changed(JanetArgs args) {
    uiEditableCombobox *cbox;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiEditableComboboxOnChanged(cbox, editable_combobox_on_changed, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Radio buttons */

static int janet_ui_radio_buttons(JanetArgs args) {
    ASSERT_INITED(args);
    JANET_RETURN(args, janet_ui_handle_to_control(uiNewRadioButtons(), &radio_buttons_td));
}

static int janet_ui_radio_buttons_selected(JanetArgs args) {
    uiRadioButtons *rb;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    if (args.n == 2) {
        int selected = 0;
        JANET_ARG_BOOLEAN(selected, args, 1);
        uiRadioButtonsSetSelected(rb, selected);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_INTEGER(args, uiRadioButtonsSelected(rb));
}

static int janet_ui_radio_buttons_append(JanetArgs args) {
    uiRadioButtons *rb;
    const uint8_t *text;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    JANET_ARG_STRING(text, args, 1);
    uiRadioButtonsAppend(rb, (const char *)text);
    JANET_RETURN(args, args.v[0]);
}

static void radio_buttons_on_selected(uiRadioButtons *rb, void *data) {
    (void) rb;
    janet_ui_handler(data);
}

static int janet_ui_radio_buttons_on_selected(JanetArgs args) {
    uiRadioButtons *rb;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiRadioButtonsOnSelected(rb, radio_buttons_on_selected, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Multiline Entry */

static int janet_ui_multiline_entry(JanetArgs args) {
    int nowrap = 0;
    JANET_MAXARITY(args, 1);
    ASSERT_INITED(args);
    if (args.n == 1)
        JANET_ARG_BOOLEAN(nowrap, args, 0);
    JANET_RETURN(args, janet_ui_handle_to_control(
                nowrap ? uiNewNonWrappingMultilineEntry() : uiNewMultilineEntry(),
                &multiline_entry_td));
}

static int janet_ui_multiline_entry_text(JanetArgs args) {
    uiMultilineEntry *me;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    if (args.n == 2) {
        const uint8_t *text;
        JANET_ARG_STRING(text, args, 1);
        uiMultilineEntrySetText(me, (const char *)text);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_CSTRING(args, uiMultilineEntryText(me));
}

static int janet_ui_multiline_entry_read_only(JanetArgs args) {
    uiMultilineEntry *me;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    if (args.n == 2) {
        int selected;
        JANET_ARG_BOOLEAN(selected, args, 1);
        uiMultilineEntrySetReadOnly(me, selected);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiMultilineEntryReadOnly(me));
}

static int janet_ui_multiline_entry_append(JanetArgs args) {
    uiMultilineEntry *me;
    const uint8_t *text;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    JANET_ARG_STRING(text, args, 1);
    uiMultilineEntryAppend(me, (const char *)text);
    JANET_RETURN(args, args.v[0]);
}

static void multiline_entry_on_changed(uiMultilineEntry *e, void *data) {
    (void) e;
    janet_ui_handler(data);
}

static int janet_ui_multiline_entry_on_changed(JanetArgs args) {
    uiMultilineEntry *me;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiMultilineEntryOnChanged(me, multiline_entry_on_changed, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Menu Item */

static int janet_ui_menu_item_enable(JanetArgs args) {
    uiMenuItem *mi;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(mi, args, 0, &menu_item_td);
    uiMenuItemEnable(mi);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_menu_item_disable(JanetArgs args) {
    uiMenuItem *mi;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(mi, args, 0, &menu_item_td);
    uiMenuItemDisable(mi);
    JANET_RETURN(args, args.v[0]);
}

static int janet_ui_menu_item_checked(JanetArgs args) {
    uiMenuItem *mi;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_UITYPE(mi, args, 0, &menu_item_td);
    if (args.n == 2) {
        int checked;
        JANET_ARG_BOOLEAN(checked, args, 1);
        uiMenuItemSetChecked(mi, checked);
        JANET_RETURN(args, args.v[0]);
    }
    JANET_RETURN_BOOLEAN(args, uiMenuItemChecked(mi));
}

static void menu_item_on_clicked(uiMenuItem *sender, uiWindow *window, void *data) {
    (void) sender;
    (void) window;
    janet_ui_handler(data);
}

static int janet_ui_menu_item_on_clicked(JanetArgs args) {
    uiMenuItem *mi;
    JANET_FIXARITY(args, 2);
    JANET_CHECKMANY(args, 1, JANET_TFLAG_CALLABLE);
    JANET_ARG_UITYPE(mi, args, 0, &menu_item_td);
    void *handle = janet_ui_to_handler_data(args.v[1]);
    uiMenuItemOnClicked(mi, menu_item_on_clicked, handle);
    JANET_RETURN(args, args.v[0]);
}

/* Menu */

static int janet_ui_menu(JanetArgs args) {
    const uint8_t *name;
    ASSERT_INITED(args);
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(name, args, 0);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiNewMenu((const char *)name),
                &menu_td));
}

static int janet_ui_menu_append_item(JanetArgs args) {
    const uint8_t *name;
    uiMenu *menu;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    JANET_ARG_STRING(name, args, 1);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiMenuAppendItem(menu, (const char *)name),
                &menu_item_td));
}

static int janet_ui_menu_append_check_item(JanetArgs args) {
    const uint8_t *name;
    uiMenu *menu;
    JANET_FIXARITY(args, 2);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    JANET_ARG_STRING(name, args, 1);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiMenuAppendCheckItem(menu, (const char *)name),
                &menu_item_td));
}

static int janet_ui_menu_append_quit_item(JanetArgs args) {
    uiMenu *menu;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiMenuAppendQuitItem(menu),
                &menu_item_td));
}

static int janet_ui_menu_append_about_item(JanetArgs args) {
    uiMenu *menu;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiMenuAppendAboutItem(menu),
                &menu_item_td));
}

static int janet_ui_menu_append_preferences_item(JanetArgs args) {
    uiMenu *menu;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    JANET_RETURN(args, janet_ui_handle_to_control(
                uiMenuAppendPreferencesItem(menu),
                &menu_item_td));
}

static int janet_ui_menu_append_separator(JanetArgs args) {
    uiMenu *menu;
    JANET_FIXARITY(args, 1);
    JANET_ARG_UITYPE(menu, args, 0, &menu_td);
    uiMenuAppendSeparator(menu);
    JANET_RETURN(args, args.v[0]);
}

/*****************************************************************************/

static const JanetReg cfuns[] = {
    {"init", janet_ui_init, NULL},
    {"quit", janet_ui_quit, NULL},
    {"uninit", janet_ui_uninit, NULL},
    {"main", janet_ui_main, NULL},
    {"main-step", janet_ui_mainstep, NULL},
    {"main-steps", janet_ui_mainsteps, NULL},
    {"queue-main", janet_ui_queue_main, NULL},
    {"on-should-quit", janet_ui_on_should_quit, NULL},
    {"timer", janet_ui_timer, NULL},
    {"save-file", janet_ui_save_file, NULL},
    {"open-file", janet_ui_open_file, NULL},
    {"message-box", janet_ui_message_box, NULL},
    {"message-box-error", janet_ui_message_box_error, NULL},

    /* Controls */
    {"destroy", janet_ui_destroy, NULL},
    {"parent", janet_ui_parent, NULL},
    {"top-level", janet_ui_top_level, NULL},
    {"visible", janet_ui_visible, NULL},
    {"enabled", janet_ui_enabled, NULL},
    {"show", janet_ui_show, NULL},
    {"hide", janet_ui_hide, NULL},
    {"enable", janet_ui_enable, NULL},
    {"disable", janet_ui_disable, NULL},

    /* Window */
    {"window", janet_ui_window, NULL},
    {"window/title", janet_ui_window_title, NULL},
    {"window/content-size", janet_ui_window_content_size, NULL},
    {"window/fullscreen", janet_ui_window_fullscreen, NULL},
    {"window/on-content-size-changed", janet_ui_window_on_content_size_changed, NULL},
    {"window/on-closing", janet_ui_window_on_closing, NULL},
    {"window/set-child", janet_ui_window_set_child, NULL},
    {"window/borderless", janet_ui_window_borderless, NULL},
    {"window/margined", janet_ui_window_margined, NULL},

    /* Button */
    {"button", janet_ui_button, NULL},
    {"button/text", janet_ui_button_text, NULL},
    {"button/on-clicked", janet_ui_button_on_clicked, NULL},

    /* Box */
    {"vertical-box", janet_ui_vertical_box, NULL},
    {"horizontal-box", janet_ui_horizontal_box, NULL},
    {"box/padded", janet_ui_box_padded, NULL},
    {"box/append", janet_ui_box_append, NULL},
    {"box/delete", janet_ui_box_delete, NULL},

    /* Check box */
    {"checkbox", janet_ui_checkbox, NULL},
    {"checkbox/on-toggled", janet_ui_checkbox_on_toggled, NULL},
    {"checkbox/text", janet_ui_checkbox_text, NULL},
    {"checkbox/checked", janet_ui_checkbox_checked, NULL},

    /* Entry */
    {"entry", janet_ui_entry, NULL},
    {"password-entry", janet_ui_password_entry, NULL},
    {"search-entry", janet_ui_search_entry, NULL},
    {"entry/text", janet_ui_entry_text, NULL},
    {"entry/read-only", janet_ui_entry_read_only, NULL},
    {"entry/on-changed", janet_ui_entry_on_changed, NULL},

    /* Label */
    {"label", janet_ui_label, NULL},
    {"label/text", janet_ui_label_text, NULL},

    /* Tab */
    {"tab", janet_ui_tab, NULL},
    {"tab/margined", janet_ui_tab_margined, NULL},
    {"tab/num-pages", janet_ui_tab_num_pages, NULL},
    {"tab/append", janet_ui_tab_append, NULL},
    {"tab/insert-at", janet_ui_tab_insert_at, NULL},
    {"tab/delete", janet_ui_tab_delete, NULL},

    /* Group */
    {"group", janet_ui_group, NULL},
    {"group/title", janet_ui_group_title, NULL},
    {"group/margined", janet_ui_group_margined, NULL},
    {"group/set-child", janet_ui_group_set_child, NULL},

    /* Spinbox */
    {"spinbox", janet_ui_spinbox, NULL},
    {"spinbox/value", janet_ui_spinbox_value, NULL},
    {"spinbox/on-changed", janet_ui_spinbox_on_changed, NULL},

    /* Slider */
    {"slider", janet_ui_slider, NULL},
    {"slider/value", janet_ui_slider_value, NULL},
    {"slider/on-changed", janet_ui_slider_on_changed, NULL},

    /* Progress Bar */
    {"progress-bar", janet_ui_progress_bar, NULL},
    {"progress-bar/value", janet_ui_progress_bar_value, NULL},

    /* Separator */
    {"horizontal-separator", janet_ui_horizontal_separator, NULL},
    {"vertical-separator", janet_ui_vertical_separator, NULL},

    /* Combobox */
    {"combobox", janet_ui_combobox, NULL},
    {"combobox/append", janet_ui_combobox_append, NULL},
    {"combobox/selected", janet_ui_combobox_selected, NULL},
    {"combobox/on-selected", janet_ui_combobox_on_selected, NULL},

    /* Editable Combobox */
    {"editable-combobox", janet_ui_editable_combobox, NULL},
    {"editable-combobox/text", janet_ui_editable_combobox_text, NULL},
    {"editable-combobox/append", janet_ui_editable_combobox_append, NULL},
    {"editable-combobox/on-changed", janet_ui_editable_combobox_on_changed, NULL},

    /* Radio Buttons */
    {"radio-buttons", janet_ui_radio_buttons, NULL},
    {"radio-buttons/append", janet_ui_radio_buttons_append, NULL},
    {"radio-buttons/selected", janet_ui_radio_buttons_selected, NULL},
    {"radio-buttons/on-selected", janet_ui_radio_buttons_on_selected, NULL},

    /* Multiline Entry */
    {"multiline-entry", janet_ui_multiline_entry, NULL},
    {"multiline-entry/text", janet_ui_multiline_entry_text, NULL},
    {"multiline-entry/read-only", janet_ui_multiline_entry_read_only, NULL},
    {"multiline-entry/append", janet_ui_multiline_entry_append, NULL},
    {"multiline-entry/on-changed", janet_ui_multiline_entry_on_changed, NULL},

    /* Menu Item */
    {"menu-item/enable", janet_ui_menu_item_enable, NULL},
    {"menu-item/disable", janet_ui_menu_item_disable, NULL},
    {"menu-item/checked", janet_ui_menu_item_checked, NULL},
    {"menu-item/on-clicked", janet_ui_menu_item_on_clicked, NULL},

    /* Menu */
    {"menu", janet_ui_menu, NULL},
    {"menu/append-item", janet_ui_menu_append_item, NULL},
    {"menu/append-check-item", janet_ui_menu_append_check_item, NULL},
    {"menu/append-quit-item", janet_ui_menu_append_quit_item, NULL},
    {"menu/append-about-item", janet_ui_menu_append_about_item, NULL},
    {"menu/append-preferences-item", janet_ui_menu_append_preferences_item, NULL},
    {"menu/append-separator", janet_ui_menu_append_separator, NULL},

    {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, "ui", cfuns);
    return 0;
}
