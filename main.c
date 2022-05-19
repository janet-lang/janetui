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
static const JanetAbstractType control_td = {"ui/control", JANET_ATEND_NAME};
static const JanetAbstractType window_td = {"ui/window", JANET_ATEND_NAME};
static const JanetAbstractType button_td = {"ui/button", JANET_ATEND_NAME};
static const JanetAbstractType box_td = {"ui/box", JANET_ATEND_NAME};
static const JanetAbstractType checkbox_td = {"ui/checkbox", JANET_ATEND_NAME};
static const JanetAbstractType entry_td = {"ui/entry", JANET_ATEND_NAME};
static const JanetAbstractType label_td = {"ui/label", JANET_ATEND_NAME};
static const JanetAbstractType tab_td = {"ui/tab", JANET_ATEND_NAME};
static const JanetAbstractType group_td = {"ui/group", JANET_ATEND_NAME};
static const JanetAbstractType spinbox_td = {"ui/spinbox", JANET_ATEND_NAME};
static const JanetAbstractType slider_td = {"ui/slider", JANET_ATEND_NAME};
static const JanetAbstractType progress_bar_td = {"ui/progress-bar", JANET_ATEND_NAME};
static const JanetAbstractType separator_td = {"ui/separator", JANET_ATEND_NAME};
static const JanetAbstractType combobox_td = {"ui/combobox", JANET_ATEND_NAME};
static const JanetAbstractType editable_combobox_td = {"ui/editable-combobox", JANET_ATEND_NAME};
static const JanetAbstractType radio_buttons_td = {"ui/radio-buttons", JANET_ATEND_NAME};
static const JanetAbstractType date_time_picker_td = {"ui/date-time-picker", JANET_ATEND_NAME};
static const JanetAbstractType multiline_entry_td = {"ui/multiline-entry", JANET_ATEND_NAME};
static const JanetAbstractType menu_item_td = {"ui/menu-item", JANET_ATEND_NAME};
static const JanetAbstractType menu_td = {"ui/menu", JANET_ATEND_NAME};

/* Helpers */

static void *janet_getuitype(const Janet *argv, int32_t n, const JanetAbstractType *at) {
    UIControlWrapper *uicw = janet_getabstract(argv, n, at);
    if (uicw->flags & UI_FLAG_DESTROYED) {
        janet_panic("ui control already destoryed");
    }
    return uicw->control;
}

/* Cast a Janet into a uiControl structure. Returns a pointer to
 * the uiControl, or panics if cast fails. */
static uiControl *janet_getcontrol(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checktype(x, JANET_ABSTRACT)) {
        janet_panicf("expected ui control, got %v", x);
    }
    UIControlWrapper *abst = janet_unwrap_abstract(x);
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
            at == &menu_td) {
        if (abst->flags & UI_FLAG_DESTROYED) {
            janet_panic("ui control already destoryed");
        }
        return uiControl(abst->control);
    }
    janet_panicf("expected ui control, got %v", x);
    return NULL;
}

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
        janet_call(janet_unwrap_function(funcv), 0, NULL);
    } else if (janet_checktype(funcv, JANET_CFUNCTION)) {
        JanetCFunction cfunc = janet_unwrap_cfunction(funcv);
        cfunc(0, NULL);
    } else {
        printf("called invalid handler\n");
    }
    return 1;
}

static void janet_ui_handler_void(void *data) {
    janet_ui_handler(data);
}

static void assert_callable(const Janet *argv, int32_t n) {
    if (!janet_checktypes(argv[n], JANET_TFLAG_CALLABLE)) {
        janet_panic_type(argv[n], n, JANET_TFLAG_CALLABLE);
    }
}

/* Global state */

static JANET_THREAD_LOCAL int inited = 0;
static void assert_inited(void) {
    if (!inited) {
        const char *initerr;
        uiInitOptions o = {0};
        if ((initerr = uiInit(&o)) != NULL) {
            Janet err = janet_cstringv(initerr);
            uiFreeInitError(initerr);
            janet_panicv(err);
        }
        inited = 1;
    }
}

static Janet janet_ui_init(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_wrap_nil();
}

static Janet janet_ui_quit(int32_t argc, Janet *argv) {
    assert_inited();
    uiQuit();
    return janet_wrap_nil();
}

static Janet janet_ui_uninit(int32_t argc, Janet *argv) {
    assert_inited();
    uiUninit();
    return janet_wrap_nil();
}

static Janet janet_ui_main(int32_t argc, Janet *argv) {
    assert_inited();
    uiMain();
    return janet_wrap_nil();
}

static Janet janet_ui_mainstep(int32_t argc, Janet *argv) {
    assert_inited();
    janet_fixarity(argc, 1);
    int32_t step = janet_getinteger(argv, 0);
    uiMainStep(step);
    return janet_wrap_nil();
}

static Janet janet_ui_mainsteps(int32_t argc, Janet *argv) {
    assert_inited();
    uiMainSteps();
    return janet_wrap_nil();
}

static Janet janet_ui_queue_main(int32_t argc, Janet *argv) {
    assert_inited();
    janet_fixarity(argc, 1);
    assert_callable(argv, 0);
    void *handle = janet_ui_to_handler_data(argv[0]);
    uiQueueMain(janet_ui_handler_void, handle);
    return janet_wrap_nil();
}

static Janet janet_ui_on_should_quit(int32_t argc, Janet *argv) {
    assert_inited();
    janet_fixarity(argc, 1);
    assert_callable(argv, 0);
    void *handle = janet_ui_to_handler_data(argv[0]);
    uiOnShouldQuit(janet_ui_handler, handle);
    return janet_wrap_nil();
}

static Janet janet_ui_timer(int32_t argc, Janet *argv) {
    int32_t milliseconds;
    janet_fixarity(argc, 2);
    milliseconds = janet_getinteger(argv, 0);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiTimer(milliseconds, janet_ui_handler, handle);
    return janet_wrap_nil();
}

static Janet janet_ui_open_file(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    char *str = uiOpenFile(window);
    if (NULL != str) return janet_cstringv(str);
    return janet_wrap_nil();
}

static Janet janet_ui_save_file(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    char *str = uiSaveFile(window);
    if (NULL != str) return janet_cstringv(str);
    return janet_wrap_nil();
}

static Janet janet_ui_message_box(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    const uint8_t *title = janet_getstring(argv, 1);
    const uint8_t *description = janet_getstring(argv, 2);
    uiMsgBox(window, (const char *)title, (const char *)description);
    return janet_wrap_nil();
}

static Janet janet_ui_message_box_error(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    const uint8_t *title = janet_getstring(argv, 1);
    const uint8_t *description = janet_getstring(argv, 2);
    uiMsgBoxError(window, (const char *)title, (const char *)description);
    return janet_wrap_nil();
}

/* Generic controls */

static Janet janet_ui_destroy(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    uiControlDestroy(c);
    return janet_wrap_nil();
}

static Janet janet_ui_parent(int32_t argc, Janet *argv) {
    uiControl *c, *d;
    janet_arity(argc, 1, 2);
    c = janet_getcontrol(argv, 0);
    if (argc == 2) {
        d = janet_getcontrol(argv, 1);
        uiControlSetParent(c, d);
        return argv[0];
    }
    return janet_ui_handle_to_control(uiControlParent(c), &control_td);
}

static Janet janet_ui_top_level(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    return janet_wrap_boolean(uiControlToplevel(c));
}

static Janet janet_ui_visible(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    return janet_wrap_boolean(uiControlVisible(c));
}

static Janet janet_ui_enabled(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    return janet_wrap_boolean(uiControlEnabled(c));
}

static Janet janet_ui_show(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    uiControlShow(c);
    return argv[0];
}

static Janet janet_ui_hide(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    uiControlHide(c);
    return argv[0];
}

static Janet janet_ui_enable(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    uiControlEnable(c);
    return argv[0];
}

static Janet janet_ui_disable(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiControl *c = janet_getcontrol(argv, 0);
    uiControlDisable(c);
    return argv[0];
}

/* Window */

static int onClosing(uiWindow *w, void *data) {
  uiQuit();
  return 1;
}

static Janet janet_ui_window(int32_t argc, Janet *argv) {
    assert_inited();
    const char *title = "";
    int32_t width = 800;
    int32_t height = 600;
    int menuBar = 0;
    janet_arity(argc, 0, 4);
    if (argc >= 1) title = (const char *) janet_getstring(argv, 0);
    if (argc >= 2) width = janet_getinteger(argv, 1);
    if (argc >= 3) height = janet_getinteger(argv, 2);
    if (argc >= 4) menuBar = janet_getboolean(argv, 3);
    uiWindow *window = uiNewWindow(title, width, height, menuBar);
    if (NULL == window) janet_panic("could not create windows");
    uiWindowOnClosing(window, onClosing, NULL);
    return janet_ui_handle_to_control(window, &window_td);
}

static Janet janet_ui_window_title(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    if (argc == 2) {
        const uint8_t *newTitle = janet_getstring(argv, 1);
        uiWindowSetTitle(window, (const char *)newTitle);
        return argv[0];
    }
    return janet_cstringv(uiWindowTitle(window));
}

static Janet janet_ui_window_content_size(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    if (argc == 1) {
        int w = 0, h = 0;
        uiWindowContentSize(window, &w, &h);
        Janet *tup = janet_tuple_begin(2);
        tup[0] = janet_wrap_integer(w);
        tup[1] = janet_wrap_integer(h);
        return janet_wrap_tuple(janet_tuple_end(tup));
    } else if (argc == 2) {
        int32_t wh = janet_getinteger(argv, 1);
        uiWindowSetContentSize(window, wh, wh);
    } else {
        int32_t w = janet_getinteger(argv, 1);
        int32_t h = janet_getinteger(argv, 2);
        uiWindowSetContentSize(window, w, h);
    }
    return argv[0];
}

static Janet janet_ui_window_fullscreen(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    if (argc == 2) {
        int full = janet_getboolean(argv, 1);
        uiWindowSetFullscreen(window, full);
        return argv[0];
    }
    return janet_wrap_boolean(uiWindowFullscreen(window));
}

static int window_closing_handler(uiWindow *window, void *data) {
    (void) window;
    return janet_ui_handler(data);
}

static void window_content_size_changed_handler(uiWindow *window, void *data) {
    (void) window;
    janet_ui_handler(data);
}

static Janet janet_ui_window_on_closing(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    uiWindowOnClosing(window, window_closing_handler,
            janet_ui_to_handler_data(argv[1]));
    return argv[0];
}

static Janet janet_ui_window_on_content_size_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    uiWindowOnContentSizeChanged(window, window_content_size_changed_handler,
            janet_ui_to_handler_data(argv[1]));
    return argv[0];
}

static Janet janet_ui_window_set_child(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    uiControl *c = janet_getcontrol(argv, 1);
    uiWindowSetChild(window, c);
    return argv[0];
}

static Janet janet_ui_window_borderless(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    if (argc == 2) {
        int borders = janet_getboolean(argv, 1);
        uiWindowSetBorderless(window, borders);
        return argv[0];
    }
    return janet_wrap_boolean(uiWindowBorderless(window));
}

static Janet janet_ui_window_margined(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiWindow *window = janet_getuitype(argv, 0, &window_td);
    if (argc == 2) {
        int margined = janet_getboolean(argv, 1);
        uiWindowSetMargined(window, margined);
        return argv[0];
    }
    return janet_wrap_boolean(uiWindowMargined(window));
}

/* Button */
static Janet janet_ui_button(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    assert_inited();
    const uint8_t *text = janet_getstring(argv, 0);
    uiButton *button = uiNewButton((const char *)text);
    return janet_ui_handle_to_control(button, &button_td);
}

static Janet janet_ui_button_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiButton *button = janet_getuitype(argv, 0, &button_td);
    if (argc == 2) {
        const uint8_t *newText = janet_getstring(argv, 1);
        uiButtonSetText(button, (const char *)newText);
        return argv[0];
    }
    return janet_cstringv(uiButtonText(button));
}

static void button_click_handler(uiButton *button, void *data) {
    (void)button;
    janet_ui_handler(data);
}

static Janet janet_ui_button_on_clicked(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiButton *button = janet_getuitype(argv, 0, &button_td);
    uiButtonOnClicked(button, button_click_handler,
            janet_ui_to_handler_data(argv[1]));
    return argv[0];
}

/* Box */

static Janet janet_ui_horizontal_box(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    uiBox *box = uiNewHorizontalBox();
    return janet_ui_handle_to_control(box, &box_td);
}

static Janet janet_ui_vertical_box(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    uiBox *box = uiNewVerticalBox();
    return janet_ui_handle_to_control(box, &box_td);
}

static Janet janet_ui_box_append(int32_t argc, Janet *argv) {
    int stretchy = 0;
    janet_arity(argc, 2, 3);
    uiBox *box = janet_getuitype(argv, 0, &box_td);
    uiControl *c = janet_getcontrol(argv, 1);
    if (argc == 3) stretchy = janet_getboolean(argv, 2);
    uiBoxAppend(box, c, stretchy);
    return argv[0];
}

static Janet janet_ui_box_delete(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiBox *box = janet_getuitype(argv, 0, &box_td);
    int32_t index = janet_getinteger(argv, 1);
    uiBoxDelete(box, index);
    return argv[0];
}

static Janet janet_ui_box_padded(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiBox *box = janet_getuitype(argv, 0, &box_td);
    if (argc == 2) {
        int padded = janet_getboolean(argv, 1);
        uiBoxSetPadded(box, padded);
        return argv[0];
    }
    return janet_wrap_boolean(uiBoxPadded(box));
}

/* Checkbox */

static Janet janet_ui_checkbox(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    assert_inited();
    const uint8_t *text = janet_getstring(argv, 0);
    uiCheckbox *cbox = uiNewCheckbox((const char *)text);
    return janet_ui_handle_to_control(cbox, &checkbox_td);
}

static Janet janet_ui_checkbox_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiCheckbox *cbox = janet_getuitype(argv, 0, &checkbox_td);
    if (argc == 2) {
        const uint8_t *text = janet_getstring(argv, 1);
        uiCheckboxSetText(cbox, (const char *)text);
        return argv[0];
    }
    return janet_cstringv(uiCheckboxText(cbox));
}

static Janet janet_ui_checkbox_checked(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiCheckbox *cbox = janet_getuitype(argv, 0, &checkbox_td);
    if (argc == 2) {
        int checked = janet_getboolean(argv, 1);
        uiCheckboxSetChecked(cbox, checked);
        return argv[0];
    }
    return janet_wrap_boolean(uiCheckboxChecked(cbox));
}

static void on_toggled_handler(uiCheckbox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static Janet janet_ui_checkbox_on_toggled(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiCheckbox *cbox = janet_getuitype(argv, 0, &checkbox_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiCheckboxOnToggled(cbox, on_toggled_handler, handle);
    return argv[0];
}

/* Entry */

static Janet janet_ui_entry(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    return janet_ui_handle_to_control(uiNewEntry(), &entry_td);
}

static Janet janet_ui_password_entry(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    return janet_ui_handle_to_control(uiNewPasswordEntry(), &entry_td);
}

static Janet janet_ui_search_entry(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    return janet_ui_handle_to_control(uiNewSearchEntry(), &entry_td);
}

static Janet janet_ui_entry_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiEntry *entry = janet_getuitype(argv, 0, &entry_td);
    if (argc == 2) {
        const uint8_t *text = janet_getstring(argv, 1);
        uiEntrySetText(entry, (const char *)text);
        return argv[0];
    }
    return janet_cstringv(uiEntryText(entry));
}

static Janet janet_ui_entry_read_only(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiEntry *entry = janet_getuitype(argv, 0, &entry_td);
    if (argc == 2) {
        int readonly = janet_getboolean(argv, 1);
        uiEntrySetReadOnly(entry, readonly);
        return argv[0];
    }
    return janet_wrap_boolean(uiEntryReadOnly(entry));
}

static void on_entry_changed(uiEntry *e, void *data) {
    (void) e;
    janet_ui_handler(data);
}

static Janet janet_ui_entry_on_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiEntry *entry = janet_getuitype(argv, 0, &entry_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiEntryOnChanged(entry, on_entry_changed, handle);
    return argv[0];
}

/* Label */

static Janet janet_ui_label(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    assert_inited();
    const uint8_t *text = janet_getstring(argv, 0);
    uiLabel *label = uiNewLabel((const char *)text);
    return janet_ui_handle_to_control(label, &label_td);
}

static Janet janet_ui_label_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiLabel *label = janet_getuitype(argv, 0, &label_td);
    if (argc == 2) {
        const uint8_t *text = janet_getstring(argv, 1);
        uiLabelSetText(label, (const char *)text);
        return argv[0];
    }
    return janet_cstringv(uiLabelText(label));
}

/* Janet */

static Janet janet_ui_tab(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    return janet_ui_handle_to_control(uiNewTab(), &tab_td);
}

static Janet janet_ui_tab_margined(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    uiTab *tab = janet_getuitype(argv, 0, &tab_td);
    int32_t page = janet_getinteger(argv, 1);
    if (argc == 3) {
        int margined = janet_getboolean(argv, 2);
        uiTabSetMargined(tab, page, margined);
        return argv[0];
    }
    return janet_wrap_boolean(uiTabMargined(tab, page));
}

static Janet janet_ui_tab_num_pages(int32_t argc, Janet *argv) {
    uiTab *tab = janet_getuitype(argv, 0, &tab_td);
    return janet_wrap_integer(uiTabNumPages(tab));
}

static Janet janet_ui_tab_append(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    uiTab *tab = janet_getuitype(argv, 0, &tab_td);
    const uint8_t *name = janet_getstring(argv, 1);
    uiControl *c = janet_getcontrol(argv, 2);
    uiTabAppend(tab, (const char *)name, c);
    return argv[0];
}

static Janet janet_ui_tab_insert_at(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 4);
    uiTab *tab = janet_getuitype(argv, 0, &tab_td);
    const uint8_t *name = janet_getstring(argv, 1);
    int32_t at = janet_getinteger(argv, 2);
    uiControl *c = janet_getcontrol(argv, 3);
    uiTabInsertAt(tab, (const char *)name, at, c);
    return argv[0];
}

static Janet janet_ui_tab_delete(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiTab *tab = janet_getuitype(argv, 0, &tab_td);
    int32_t at = janet_getinteger(argv, 1);
    uiTabDelete(tab, at);
    return argv[0];
}

/* Janet */

static Janet janet_ui_group(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    assert_inited();
    const uint8_t *title = janet_getstring(argv, 0);
    uiGroup *group = uiNewGroup((const char *)title);
    return janet_ui_handle_to_control(group, &group_td);
}

static Janet janet_ui_group_title(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiGroup *group = janet_getuitype(argv, 0, &group_td);
    if (argc == 2) {
        const uint8_t *title = janet_getstring(argv, 1);
        uiGroupSetTitle(group, (const char *)title);
        return argv[0];
    }
    return janet_cstringv(uiGroupTitle(group));
}

static Janet janet_ui_group_margined(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiGroup *group = janet_getuitype(argv, 0, &group_td);
    if (argc == 2) {
        int margined = janet_getboolean(argv, 1);
        uiGroupSetMargined(group, margined);
        return argv[0];
    }
    return janet_wrap_boolean(uiGroupMargined(group));
}

static Janet janet_ui_group_set_child(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiGroup *group = janet_getuitype(argv, 0, &group_td);
    uiControl *c = janet_getcontrol(argv, 1);
    uiGroupSetChild(group, c);
    return janet_wrap_boolean(uiGroupMargined(group));
}

/* Janet */

static Janet janet_ui_spinbox(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    assert_inited();
    int32_t min = janet_getinteger(argv, 0);
    int32_t max = janet_getinteger(argv, 1);
    uiSpinbox *spinbox = uiNewSpinbox(min, max);
    return janet_ui_handle_to_control(spinbox, &spinbox_td);
}

static Janet janet_ui_spinbox_value(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiSpinbox *spinbox = janet_getuitype(argv, 0, &spinbox_td);
    if (argc == 2) {
        int32_t value = janet_getinteger(argv, 1);
        uiSpinboxSetValue(spinbox, value);
        return argv[0];
    }
    return janet_wrap_integer(uiSpinboxValue(spinbox));
}

static void spinbox_on_changed(uiSpinbox *sb, void *data) {
    (void) sb;
    janet_ui_handler(data);
}

static Janet janet_ui_spinbox_on_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiSpinbox *spinbox = janet_getuitype(argv, 0, &spinbox_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiSpinboxOnChanged(spinbox, spinbox_on_changed, handle);
    return argv[0];
}

/* Janet */

static Janet janet_ui_slider(int32_t argc, Janet *argv) {
    assert_inited();
    janet_fixarity(argc, 2);
    int32_t min = janet_getinteger(argv, 0);
    int32_t max = janet_getinteger(argv, 1);
    return janet_ui_handle_to_control(uiNewSlider(min, max), &slider_td);
}

static Janet janet_ui_slider_value(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiSlider *slider = janet_getuitype(argv, 0, &slider_td);
    if (argc == 2) {
        int32_t value = janet_getinteger(argv, 1);
        uiSliderSetValue(slider, value);
        return argv[0];
    }
    return janet_wrap_integer(uiSliderValue(slider));
}

static void slider_on_changed(uiSlider *s, void *data) {
    (void) s;
    janet_ui_handler(data);
}

static Janet janet_ui_slider_on_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiSlider *slider = janet_getuitype(argv, 0, &slider_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiSliderOnChanged(slider, slider_on_changed, handle);
    return argv[0];
}

/* Progress Bar */

static Janet janet_ui_progress_bar(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_ui_handle_to_control(uiNewProgressBar(), &progress_bar_td);
}

static Janet janet_ui_progress_bar_value(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiProgressBar *bar = janet_getuitype(argv, 0, &progress_bar_td);
    if (argc == 2) {
        int32_t value = janet_getinteger(argv, 1);
        uiProgressBarSetValue(bar, value);
        return argv[0];
    }
    return janet_wrap_integer(uiProgressBarValue(bar));
}

/* Separator */

static Janet janet_ui_horizontal_separator(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_ui_handle_to_control(uiNewHorizontalSeparator(), &separator_td);
}

static Janet janet_ui_vertical_separator(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_ui_handle_to_control(uiNewVerticalSeparator(), &separator_td);
}

/* Combobox */

static Janet janet_ui_combobox(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_ui_handle_to_control(uiNewCombobox(), &combobox_td);
}

static Janet janet_ui_combobox_selected(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiCombobox *cbox = janet_getuitype(argv, 0, &combobox_td);
    if (argc == 2) {
        int selected = janet_getboolean(argv, 1);
        uiComboboxSetSelected(cbox, selected);
        return argv[0];
    }
    return janet_wrap_integer(uiComboboxSelected(cbox));
}

static Janet janet_ui_combobox_append(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiCombobox *cbox = janet_getuitype(argv, 0, &combobox_td);
    const uint8_t *text = janet_getstring(argv, 1);
    uiComboboxAppend(cbox, (const char *)text);
    return argv[0];
}

static void combobox_on_selected(uiCombobox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static Janet janet_ui_combobox_on_selected(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiCombobox *cbox = janet_getuitype(argv, 0, &combobox_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiComboboxOnSelected(cbox, combobox_on_selected, handle);
    return argv[0];
}

/* Editable Combobox */

static Janet janet_ui_editable_combobox(int32_t argc, Janet *argv) {
    assert_inited();
    return janet_ui_handle_to_control(uiNewEditableCombobox(), &editable_combobox_td);
}

static Janet janet_ui_editable_combobox_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiEditableCombobox *cbox = janet_getuitype(argv, 0, &editable_combobox_td);
    if (argc == 2) {
        const uint8_t *text = janet_getstring(argv, 1);
        uiEditableComboboxSetText(cbox, (const char *)text);
        return argv[0];
    }
    return janet_cstringv(uiEditableComboboxText(cbox));
}

static Janet janet_ui_editable_combobox_append(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiEditableCombobox *cbox = janet_getuitype(argv, 0, &editable_combobox_td);
    const uint8_t *text = janet_getstring(argv, 1);
    uiEditableComboboxAppend(cbox, (const char *)text);
    return argv[0];
}

static void editable_combobox_on_changed(uiEditableCombobox *c, void *data) {
    (void) c;
    janet_ui_handler(data);
}

static Janet janet_ui_editable_combobox_on_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiEditableCombobox *cbox = janet_getuitype(argv, 0, &editable_combobox_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiEditableComboboxOnChanged(cbox, editable_combobox_on_changed, handle);
    return argv[0];
}

/* Radio buttons */

static Janet janet_ui_radio_buttons(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    assert_inited();
    return janet_ui_handle_to_control(uiNewRadioButtons(), &radio_buttons_td);
}

static Janet janet_ui_radio_buttons_selected(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiRadioButtons *rb = janet_getuitype(argv, 0, &radio_buttons_td);
    if (argc == 2) {
        int selected = janet_getboolean(argv, 1);
        uiRadioButtonsSetSelected(rb, selected);
        return argv[0];
    }
    return janet_wrap_integer(uiRadioButtonsSelected(rb));
}

static Janet janet_ui_radio_buttons_append(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiRadioButtons *rb = janet_getuitype(argv, 0, &radio_buttons_td);
    const uint8_t *text = janet_getstring(argv, 1);
    uiRadioButtonsAppend(rb, (const char *)text);
    return argv[0];
}

static void radio_buttons_on_selected(uiRadioButtons *rb, void *data) {
    (void) rb;
    janet_ui_handler(data);
}

static Janet janet_ui_radio_buttons_on_selected(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiRadioButtons *rb = janet_getuitype(argv, 0, &radio_buttons_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiRadioButtonsOnSelected(rb, radio_buttons_on_selected, handle);
    return argv[0];
}

/* Multiline Entry */

static Janet janet_ui_multiline_entry(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    int nowrap = 0;
    assert_inited();
    if (argc == 1) nowrap = janet_getboolean(argv, 0);
    return janet_ui_handle_to_control(
            nowrap ? uiNewNonWrappingMultilineEntry() : uiNewMultilineEntry(),
            &multiline_entry_td);
}

static Janet janet_ui_multiline_entry_text(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiMultilineEntry *me = janet_getuitype(argv, 0, &multiline_entry_td);
    if (argc == 2) {
        const uint8_t *text = janet_getstring(argv, 1);
        uiMultilineEntrySetText(me, (const char *)text);
        return argv[0];
    }
    return janet_cstringv(uiMultilineEntryText(me));
}

static Janet janet_ui_multiline_entry_read_only(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiMultilineEntry *me = janet_getuitype(argv, 0, &multiline_entry_td);
    if (argc == 2) {
        int selected = janet_getboolean(argv, 1);
        uiMultilineEntrySetReadOnly(me, selected);
        return argv[0];
    }
    return janet_wrap_boolean(uiMultilineEntryReadOnly(me));
}

static Janet janet_ui_multiline_entry_append(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiMultilineEntry *me = janet_getuitype(argv, 0, &multiline_entry_td);
    const uint8_t *text = janet_getstring(argv, 1);
    uiMultilineEntryAppend(me, (const char *)text);
    return argv[0];
}

static void multiline_entry_on_changed(uiMultilineEntry *e, void *data) {
    (void) e;
    janet_ui_handler(data);
}

static Janet janet_ui_multiline_entry_on_changed(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiMultilineEntry *me = janet_getuitype(argv, 0, &multiline_entry_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiMultilineEntryOnChanged(me, multiline_entry_on_changed, handle);
    return argv[0];
}

/* Menu Item */

static Janet janet_ui_menu_item_enable(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenuItem *mi = janet_getuitype(argv, 0, &menu_item_td);
    uiMenuItemEnable(mi);
    return argv[0];
}

static Janet janet_ui_menu_item_disable(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenuItem *mi = janet_getuitype(argv, 0, &menu_item_td);
    uiMenuItemDisable(mi);
    return argv[0];
}

static Janet janet_ui_menu_item_checked(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    uiMenuItem *mi = janet_getuitype(argv, 0, &menu_item_td);
    if (argc == 2) {
        int checked = janet_getboolean(argv, 1);
        uiMenuItemSetChecked(mi, checked);
        return argv[0];
    }
    return janet_wrap_boolean(uiMenuItemChecked(mi));
}

static void menu_item_on_clicked(uiMenuItem *sender, uiWindow *window, void *data) {
    (void) sender;
    (void) window;
    janet_ui_handler(data);
}

static Janet janet_ui_menu_item_on_clicked(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiMenuItem *mi = janet_getuitype(argv, 0, &menu_item_td);
    assert_callable(argv, 1);
    void *handle = janet_ui_to_handler_data(argv[1]);
    uiMenuItemOnClicked(mi, menu_item_on_clicked, handle);
    return argv[0];
}

/* Menu */

static Janet janet_ui_menu(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const uint8_t *name = janet_getstring(argv, 0);
    assert_inited();
    return janet_ui_handle_to_control(
            uiNewMenu((const char *)name),
            &menu_td);
}

static Janet janet_ui_menu_append_item(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    const uint8_t *name = janet_getstring(argv, 1);
    return janet_ui_handle_to_control(
            uiMenuAppendItem(menu, (const char *)name),
            &menu_item_td);
}

static Janet janet_ui_menu_append_check_item(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    const uint8_t *name = janet_getstring(argv, 1);
    return janet_ui_handle_to_control(
            uiMenuAppendCheckItem(menu, (const char *)name),
            &menu_item_td);
}

static Janet janet_ui_menu_append_quit_item(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    return janet_ui_handle_to_control(
            uiMenuAppendQuitItem(menu),
            &menu_item_td);
}

static Janet janet_ui_menu_append_about_item(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    return janet_ui_handle_to_control(
            uiMenuAppendAboutItem(menu),
            &menu_item_td);
}

static Janet janet_ui_menu_append_preferences_item(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    return janet_ui_handle_to_control(
            uiMenuAppendPreferencesItem(menu),
            &menu_item_td);
}

static Janet janet_ui_menu_append_separator(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    uiMenu *menu = janet_getuitype(argv, 0, &menu_td);
    uiMenuAppendSeparator(menu);
    return argv[0];
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

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns(env, "ui", cfuns);
}
