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

#include <dst/dst.h>

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
static const DstAbstractType control_td = {":ui.control", NULL, NULL};
static const DstAbstractType window_td = {":ui.window", NULL, NULL};
static const DstAbstractType button_td = {":ui.button", NULL, NULL};
static const DstAbstractType box_td = {":ui.box", NULL, NULL};
static const DstAbstractType checkbox_td = {":ui.checkbox", NULL, NULL};
static const DstAbstractType entry_td = {":ui.entry", NULL, NULL};
static const DstAbstractType label_td = {":ui.label", NULL, NULL};
static const DstAbstractType tab_td = {":ui.tab", NULL, NULL};
static const DstAbstractType group_td = {":ui.group", NULL, NULL};
static const DstAbstractType spinbox_td = {":ui.spinbox", NULL, NULL};
static const DstAbstractType slider_td = {":ui.slider", NULL, NULL};
static const DstAbstractType progress_bar_td = {":ui.progress-bar", NULL, NULL};
static const DstAbstractType separator_td = {":ui.separator", NULL, NULL};
static const DstAbstractType combobox_td = {":ui.combobox", NULL, NULL};
static const DstAbstractType editable_combobox_td = {":ui.editable-combobox", NULL, NULL};
static const DstAbstractType radio_buttons_td = {":ui.radio-buttons", NULL, NULL};
static const DstAbstractType date_time_picker_td = {":ui.date-time-picker", NULL, NULL};
static const DstAbstractType multiline_entry_td = {":ui.multiline-entry", NULL, NULL};
static const DstAbstractType menu_item_td = {":ui.menu-item", NULL, NULL};
static const DstAbstractType menu_td = {":ui.menu", NULL, NULL};

/* Helpers */

#define DST_ARG_UITYPE(DEST, A, N, AT) do { \
    DST_CHECKABSTRACT(A, N, AT); \
    memcpy(&(DEST), dst_unwrap_abstract((A).v[(N)]), sizeof(DEST)); \
} while (0)

/* Cast a Dst into a uiControl structure. Returns a pointer to
 * the uiControl, or NULL if cast fails. */
static uiControl *to_control(Dst x) {
    if (!dst_checktype(x, DST_ABSTRACT))
        return NULL;
    UIControlWrapper *abst = dst_unwrap_abstract(x);
    if (abst->flags & UI_FLAG_DESTROYED)
        return NULL;
    const DstAbstractType *at = dst_abstract_type(abst);
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

#define DST_ARG_CONTROL(DEST, A, N) do {\
    DST_CHECK((A), (N), DST_ABSTRACT); \
    DEST = to_control((A).v[(N)]); \
    if (NULL == DEST) DST_THROW((A), "expected alive ui control"); \
} while (0);

/* Wrap a pointer to a uiXxx object into an abstract */
static Dst dst_ui_handle_to_control(void *handle, const DstAbstractType *atype) {
    UIControlWrapper *abst = dst_abstract(atype, sizeof(UIControlWrapper));
    abst->control = handle;
    abst->flags = 0;
    return dst_wrap_abstract(abst);
}

/* Convert a function or cfunction to a handle for
 * libui callbacks */
static void *dst_ui_to_handler_data(Dst handler) {
    const Dst *tup = dst_tuple_n(&handler, 1);
    dst_gcroot(dst_wrap_tuple(tup));
    return (void *)tup;
}

/* Get the function or cfunction from a libui callback
 * handle data pointer */
static Dst dst_ui_from_handler_data(void *data) {
    const Dst *tup = (const Dst *)data;
    return tup[0];
}

/* Generic handler */
static int dst_ui_handler(void *data) {
    Dst funcv = dst_ui_from_handler_data(data);
    /* Tuple should already be GC root */
    if (dst_checktype(funcv, DST_FUNCTION)) {
        Dst in, out;
        DstFunction *func = dst_unwrap_function(funcv);
        DstFiber *fiber = dst_fiber(func, 64);
        in = dst_wrap_nil();
        dst_gcroot(dst_wrap_fiber(fiber));
        /* handle status? We should eventually expose
         * a function to call on ui handler errors. */
        int status = dst_continue(fiber, in, &out);
        dst_gcunroot(dst_wrap_fiber(fiber));
        return status;
    } else if (dst_checktype(funcv, DST_CFUNCTION)) {
        Dst ret;
        DstCFunction cfunc = dst_unwrap_cfunction(funcv);
        DstArgs args;
        args.n = 0;
        args.v = NULL;
        args.ret = &ret;
        return cfunc(args);
    } else {
        printf("called invalid handler\n");
        return 1;
    }
}

static void dst_ui_handler_void(void *data) {
    dst_ui_handler(data);
}

/* Global state */

static DST_THREAD_LOCAL int inited = 0;
#define ASSERT_INITED(args) do {\
    if (!inited) { \
        const char *initerr; \
        uiInitOptions o = {0}; \
        if ((initerr = uiInit(&o)) != NULL) { \
            Dst err = dst_cstringv(initerr); \
            uiFreeInitError(initerr); \
            DST_THROWV(args, err);\
        } \
        inited = 1; \
    } \
} while (0)

static int dst_ui_init(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN_NIL(args);
}

static int dst_ui_quit(DstArgs args) {
    ASSERT_INITED(args);
    uiQuit();
    DST_RETURN_NIL(args);
}

static int dst_ui_uninit(DstArgs args) {
    ASSERT_INITED(args);
    uiUninit();
    DST_RETURN_NIL(args);
}

static int dst_ui_main(DstArgs args) {
    ASSERT_INITED(args);
    uiMain();
    DST_RETURN_NIL(args);
}

static int dst_ui_mainstep(DstArgs args) {
    ASSERT_INITED(args);
    int32_t step = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(step, args, 0);
    uiMainStep(step);
    DST_RETURN_NIL(args);
}

static int dst_ui_mainsteps(DstArgs args) {
    ASSERT_INITED(args);
    uiMainSteps();
    DST_RETURN_NIL(args);
}

static int dst_ui_queue_main(DstArgs args) {
    ASSERT_INITED(args);
    DST_FIXARITY(args, 1);
    DST_CHECKMANY(args, 0, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[0]);
    uiQueueMain(dst_ui_handler_void, handle);
    DST_RETURN_NIL(args);
}

static int dst_ui_on_should_quit(DstArgs args) {
    ASSERT_INITED(args);
    DST_FIXARITY(args, 1);
    DST_CHECKMANY(args, 0, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[0]);
    uiOnShouldQuit(dst_ui_handler, handle);
    DST_RETURN_NIL(args);
}

static int dst_ui_timer(DstArgs args) {
    int32_t milliseconds;
    DST_FIXARITY(args, 2);
    DST_ARG_INTEGER(milliseconds, args, 0);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiTimer(milliseconds, dst_ui_handler, handle);
    DST_RETURN_NIL(args);
}

static int dst_ui_open_file(DstArgs args) {
    uiWindow *window;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    char *str = uiOpenFile(window);
    if (str)
        DST_RETURN_CSTRING(args, str);
    DST_RETURN_NIL(args);
}

static int dst_ui_save_file(DstArgs args) {
    uiWindow *window;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    char *str = uiSaveFile(window);
    if (str)
        DST_RETURN_CSTRING(args, str);
    DST_RETURN_NIL(args);
}

static int dst_ui_message_box(DstArgs args) {
    uiWindow *window;
    const uint8_t *title, *description;
    DST_ARG_UITYPE(window, args, 0, &window_td);
    DST_ARG_STRING(title, args, 1);
    DST_ARG_STRING(description, args, 2);
    uiMsgBox(window, (const char *)title, (const char *)description);
    DST_RETURN_NIL(args);
}

static int dst_ui_message_box_error(DstArgs args) {
    uiWindow *window;
    const uint8_t *title, *description;
    DST_ARG_UITYPE(window, args, 0, &window_td);
    DST_ARG_STRING(title, args, 1);
    DST_ARG_STRING(description, args, 2);
    uiMsgBoxError(window, (const char *)title, (const char *)description);
    DST_RETURN_NIL(args);
}

/* Generic controls */

static int dst_ui_destroy(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    uiControlDestroy(c);
    DST_RETURN_NIL(args);
}

static int dst_ui_parent(DstArgs args) {
    uiControl *c, *d;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_CONTROL(c, args, 0);
    if (args.n == 2) {
        DST_ARG_CONTROL(d, args, 1);
        uiControlSetParent(c, d);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN(args, dst_ui_handle_to_control(uiControlParent(c), &control_td));
}

static int dst_ui_top_level(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    DST_RETURN_BOOLEAN(args, uiControlToplevel(c));
}

static int dst_ui_visible(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    DST_RETURN_BOOLEAN(args, uiControlVisible(c));
}

static int dst_ui_enabled(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    DST_RETURN_BOOLEAN(args, uiControlEnabled(c));
}

static int dst_ui_show(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    uiControlShow(c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_hide(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    uiControlHide(c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_enable(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    uiControlEnable(c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_disable(DstArgs args) {
    uiControl *c;
    DST_ARG_CONTROL(c, args, 0);
    uiControlDisable(c);
    DST_RETURN(args, args.v[0]);
}

/* Window */

static int onClosing(uiWindow *w, void *data) {
  uiQuit();
  return 1;
}

static int dst_ui_window(DstArgs args) {
    ASSERT_INITED(args);
    const char *title = "";
    int32_t width = 800;
    int32_t height = 600;
    int menuBar = 0;
    DST_MAXARITY(args, 4);
    if (args.n >= 1) {
        const uint8_t *title_;
        DST_ARG_STRING(title_, args, 0);
        title = (const char *)title_;
    }
    if (args.n >= 2)
        DST_ARG_INTEGER(width, args, 1);
    if (args.n >= 3)
        DST_ARG_INTEGER(height, args, 2);
    if (args.n >= 4)
        DST_ARG_BOOLEAN(menuBar, args, 3);
    uiWindow *window = uiNewWindow(title, width, height, menuBar);
    if (NULL == window) {
        DST_THROW(args, "could not create window");
    }
    uiWindowOnClosing(window, onClosing, NULL);
    DST_RETURN(args, dst_ui_handle_to_control(window, &window_td));
}

static int dst_ui_window_title(DstArgs args) {
    uiWindow *window = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        const uint8_t *newTitle;
        DST_ARG_STRING(newTitle, args, 1);
        uiWindowSetTitle(window, (const char *)newTitle);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiWindowTitle(window));
}

static int dst_ui_window_content_size(DstArgs args) {
    uiWindow *window = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 3);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 1) {
        int w = 0, h = 0;
        uiWindowContentSize(window, &w, &h);
        Dst *tup = dst_tuple_begin(2);
        tup[0] = dst_wrap_integer(w);
        tup[1] = dst_wrap_integer(h);
        DST_RETURN_TUPLE(args, dst_tuple_end(tup));
    } else if (args.n == 2) {
        int32_t wh;
        DST_ARG_INTEGER(wh, args, 1);
        uiWindowSetContentSize(window, wh, wh);
    } else {
        int32_t w, h;
        DST_ARG_INTEGER(w, args, 1);
        DST_ARG_INTEGER(h, args, 2);
        uiWindowSetContentSize(window, w, h);
    }
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_window_fullscreen(DstArgs args) {
    uiWindow *window = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int full = 0;
        DST_ARG_BOOLEAN(full, args, 1);
        uiWindowSetFullscreen(window, full);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiWindowFullscreen(window));
}

static int window_closing_handler(uiWindow *window, void *data) {
    (void) window;
    return dst_ui_handler(data);
}

static void window_content_size_changed_handler(uiWindow *window, void *data) {
    (void) window;
    dst_ui_handler(data);
}

static int dst_ui_window_on_closing(DstArgs args) {
    uiWindow *window = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    uiWindowOnClosing(window, window_closing_handler,
            dst_ui_to_handler_data(args.v[1]));
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_window_on_content_size_changed(DstArgs args) {
    uiWindow *window = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    uiWindowOnContentSizeChanged(window, window_content_size_changed_handler,
            dst_ui_to_handler_data(args.v[1]));
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_window_set_child(DstArgs args) {
    uiControl *c = NULL;
    uiWindow *window = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    DST_ARG_CONTROL(c, args, 1);
    uiWindowSetChild(window, c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_window_borderless(DstArgs args) {
    uiWindow *window = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int borders = 0;
        DST_ARG_BOOLEAN(borders, args, 1);
        uiWindowSetBorderless(window, borders);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiWindowBorderless(window));
}

static int dst_ui_window_margined(DstArgs args) {
    uiWindow *window = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(window, args, 0, &window_td);
    if (args.n == 2) {
        int margined = 0;
        DST_ARG_BOOLEAN(margined, args, 1);
        uiWindowSetMargined(window, margined);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiWindowMargined(window));
}

/* Button */
static int dst_ui_button(DstArgs args) {
    ASSERT_INITED(args);
    const uint8_t *text;
    ASSERT_INITED(args);
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(text, args, 0);
    uiButton *button = uiNewButton((const char *)text);
    DST_RETURN(args, dst_ui_handle_to_control(button, &button_td));
}

static int dst_ui_button_text(DstArgs args) {
    uiButton *button = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(button, args, 0, &button_td);
    if (args.n == 2) {
        const uint8_t *newText;
        DST_ARG_STRING(newText, args, 1);
        uiButtonSetText(button, (const char *)newText);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiButtonText(button));
}

static void button_click_handler(uiButton *button, void *data) {
    (void)button;
    dst_ui_handler(data);
}

static int dst_ui_button_on_clicked(DstArgs args) {
    uiButton *button = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(button, args, 0, &button_td);
    uiButtonOnClicked(button, button_click_handler,
            dst_ui_to_handler_data(args.v[1]));
    DST_RETURN(args, args.v[0]);
}

/* Box */

static int dst_ui_horizontal_box(DstArgs args) {
    ASSERT_INITED(args);
    uiBox *box = uiNewHorizontalBox();
    DST_RETURN(args, dst_ui_handle_to_control(box, &box_td));
}

static int dst_ui_vertical_box(DstArgs args) {
    ASSERT_INITED(args);
    uiBox *box = uiNewVerticalBox();
    DST_RETURN(args, dst_ui_handle_to_control(box, &box_td));
}

static int dst_ui_box_append(DstArgs args) {
    uiControl *c;
    uiBox *box;
    int stretchy = 0;
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3);
    DST_ARG_UITYPE(box, args, 0, &box_td);
    DST_ARG_CONTROL(c, args, 1);
    if (args.n == 3) {
        DST_ARG_BOOLEAN(stretchy, args, 2);
    }
    uiBoxAppend(box, c, stretchy);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_box_delete(DstArgs args) {
    uiBox *box;
    int32_t index;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(box, args, 0, &box_td);
    DST_ARG_INTEGER(index, args, 1);
    uiBoxDelete(box, index);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_box_padded(DstArgs args) {
    uiBox *box = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(box, args, 0, &box_td);
    if (args.n == 2) {
        int padded = 0;
        DST_ARG_BOOLEAN(padded, args, 1);
        uiBoxSetPadded(box, padded);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiBoxPadded(box));
}

/* Checkbox */

static int dst_ui_checkbox(DstArgs args) {
    ASSERT_INITED(args);
    const uint8_t *text;
    uiCheckbox *cbox;
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(text, args, 0);
    cbox = uiNewCheckbox((const char *)text);
    DST_RETURN(args, dst_ui_handle_to_control(cbox, &checkbox_td));
}

static int dst_ui_checkbox_text(DstArgs args) {
    uiCheckbox *cbox;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    if (args.n == 2) {
        const uint8_t *text;
        DST_ARG_STRING(text, args, 1);
        uiCheckboxSetText(cbox, (const char *)text);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiCheckboxText(cbox));
}

static int dst_ui_checkbox_checked(DstArgs args) {
    uiCheckbox *cbox;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    if (args.n == 2) {
        int checked;
        DST_ARG_BOOLEAN(checked, args, 1);
        uiCheckboxSetChecked(cbox, checked);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiCheckboxChecked(cbox));
}

static void on_toggled_handler(uiCheckbox *c, void *data) {
    (void) c;
    dst_ui_handler(data);
}

static int dst_ui_checkbox_on_toggled(DstArgs args) {
    uiCheckbox *cbox;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &checkbox_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiCheckboxOnToggled(cbox, on_toggled_handler, handle);
    DST_RETURN(args, args.v[0]);
}

/* Entry */

static int dst_ui_entry(DstArgs args) {
    ASSERT_INITED(args);
    DST_FIXARITY(args, 0);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewEntry(), &entry_td));
}

static int dst_ui_password_entry(DstArgs args) {
    ASSERT_INITED(args);
    DST_FIXARITY(args, 0);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewPasswordEntry(), &entry_td));
}

static int dst_ui_search_entry(DstArgs args) {
    ASSERT_INITED(args);
    DST_FIXARITY(args, 0);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewSearchEntry(), &entry_td));
}

static int dst_ui_entry_text(DstArgs args) {
    uiEntry *entry;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(entry, args, 0, &entry_td);
    if (args.n == 2) {
        const uint8_t *text;
        DST_ARG_STRING(text, args, 1);
        uiEntrySetText(entry, (const char *)text);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiEntryText(entry));
}

static int dst_ui_entry_read_only(DstArgs args) {
    uiEntry *entry;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(entry, args, 0, &entry_td);
    if (args.n == 2) {
        int readonly;
        DST_ARG_BOOLEAN(readonly, args, 1);
        uiEntrySetReadOnly(entry, readonly);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiEntryReadOnly(entry));
}

static void on_entry_changed(uiEntry *e, void *data) {
    (void) e;
    dst_ui_handler(data);
}

static int dst_ui_entry_on_changed(DstArgs args) {
    uiEntry *entry;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(entry, args, 0, &entry_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiEntryOnChanged(entry, on_entry_changed, handle);
    DST_RETURN(args, args.v[0]);
}

/* Label */

static int dst_ui_label(DstArgs args) {
    const uint8_t *text;
    ASSERT_INITED(args);
    DST_ARG_STRING(text, args, 0);
    uiLabel *label = uiNewLabel((const char *)text);
    DST_RETURN(args, dst_ui_handle_to_control(label, &label_td));
}

static int dst_ui_label_text(DstArgs args) {
    uiLabel *label;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(label, args, 0, &label_td);
    if (args.n == 2) {
        const uint8_t *text;
        DST_ARG_STRING(text, args, 1);
        uiLabelSetText(label, (const char *)text);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiLabelText(label));
}

/* Tab */

static int dst_ui_tab(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewTab(), &tab_td));
}

static int dst_ui_tab_margined(DstArgs args) {
    uiTab *tab = NULL;
    int32_t page;
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3);
    DST_ARG_UITYPE(tab, args, 0, &tab_td);
    DST_ARG_INTEGER(page, args, 1);
    if (args.n == 3) {
        int margined = 0;
        DST_ARG_BOOLEAN(margined, args, 2);
        uiTabSetMargined(tab, page, margined);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiTabMargined(tab, page));
}

static int dst_ui_tab_num_pages(DstArgs args) {
    uiTab *tab = NULL;
    DST_ARG_UITYPE(tab, args, 0, &tab_td);
    DST_RETURN_INTEGER(args, uiTabNumPages(tab));
}

static int dst_ui_tab_append(DstArgs args) {
    uiTab *tab;
    uiControl *c;
    const uint8_t *name;
    DST_ARG_UITYPE(tab, args, 0, &tab_td);
    DST_ARG_STRING(name, args, 1);
    DST_ARG_CONTROL(c, args, 2);
    uiTabAppend(tab, (const char *)name, c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_tab_insert_at(DstArgs args) {
    uiTab *tab;
    uiControl *c;
    const uint8_t *name;
    int32_t at;
    DST_ARG_UITYPE(tab, args, 0, &tab_td);
    DST_ARG_STRING(name, args, 1);
    DST_ARG_INTEGER(at, args, 2);
    DST_ARG_CONTROL(c, args, 3);
    uiTabInsertAt(tab, (const char *)name, at, c);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_tab_delete(DstArgs args) {
    uiTab *tab;
    int32_t at;
    DST_ARG_UITYPE(tab, args, 0, &tab_td);
    DST_ARG_INTEGER(at, args, 1);
    uiTabDelete(tab, at);
    DST_RETURN(args, args.v[0]);
}

/* Group */

static int dst_ui_group(DstArgs args) {
    const uint8_t *title;
    ASSERT_INITED(args);
    DST_ARG_STRING(title, args, 0);
    uiGroup *group = uiNewGroup((const char *)title);
    DST_RETURN(args, dst_ui_handle_to_control(group, &group_td));
}

static int dst_ui_group_title(DstArgs args) {
    uiGroup *group;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(group, args, 0, &group_td);
    if (args.n == 2) {
        const uint8_t *title;
        DST_ARG_STRING(title, args, 1);
        uiGroupSetTitle(group, (const char *)title);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiGroupTitle(group));
}

static int dst_ui_group_margined(DstArgs args) {
    uiGroup *group = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(group, args, 0, &group_td);
    if (args.n == 2) {
        int margined = 0;
        DST_ARG_BOOLEAN(margined, args, 1);
        uiGroupSetMargined(group, margined);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiGroupMargined(group));
}

static int dst_ui_group_set_child(DstArgs args) {
    uiGroup *group = NULL;
    uiControl *c;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(group, args, 0, &group_td);
    DST_ARG_CONTROL(c, args, 1);
    uiGroupSetChild(group, c);
    DST_RETURN_BOOLEAN(args, uiGroupMargined(group));
}

/* Spinbox */

static int dst_ui_spinbox(DstArgs args) {
    uiSpinbox *spinbox = NULL;
    int32_t min, max;
    DST_FIXARITY(args, 2);
    DST_ARG_INTEGER(min, args, 0);
    DST_ARG_INTEGER(max, args, 1);
    spinbox = uiNewSpinbox(min, max);
    DST_RETURN(args, dst_ui_handle_to_control(spinbox, &spinbox_td));
}

static int dst_ui_spinbox_value(DstArgs args) {
    uiSpinbox *spinbox = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(spinbox, args, 0, &spinbox_td);
    if (args.n == 2) {
        int32_t value;
        DST_ARG_INTEGER(value, args, 1);
        uiSpinboxSetValue(spinbox, value);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_INTEGER(args, uiSpinboxValue(spinbox));
}

static void spinbox_on_changed(uiSpinbox *sb, void *data) {
    (void) sb;
    dst_ui_handler(data);
}

static int dst_ui_spinbox_on_changed(DstArgs args) {
    uiSpinbox *spinbox = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(spinbox, args, 0, &spinbox_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiSpinboxOnChanged(spinbox, spinbox_on_changed, handle);
    DST_RETURN(args, args.v[0]);
}

/* Slider */

static int dst_ui_slider(DstArgs args) {
    int32_t min, max;
    ASSERT_INITED(args);
    DST_ARG_INTEGER(min, args, 0);
    DST_ARG_INTEGER(max, args, 1);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewSlider(min, max), &slider_td));
}

static int dst_ui_slider_value(DstArgs args) {
    uiSlider *slider = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(slider, args, 0, &slider_td);
    if (args.n == 2) {
        int32_t value;
        DST_ARG_INTEGER(value, args, 1);
        uiSliderSetValue(slider, value);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_INTEGER(args, uiSliderValue(slider));
}

static void slider_on_changed(uiSlider *s, void *data) {
    (void) s;
    dst_ui_handler(data);
}

static int dst_ui_slider_on_changed(DstArgs args) {
    uiSlider *slider = NULL;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(slider, args, 0, &slider_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiSliderOnChanged(slider, slider_on_changed, handle);
    DST_RETURN(args, args.v[0]);
}

/* Progress Bar */

static int dst_ui_progress_bar(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewProgressBar(), &progress_bar_td));
}

static int dst_ui_progress_bar_value(DstArgs args) {
    uiProgressBar *bar = NULL;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(bar, args, 0, &progress_bar_td);
    if (args.n == 2) {
        int32_t value;
        DST_ARG_INTEGER(value, args, 1);
        uiProgressBarSetValue(bar, value);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_INTEGER(args, uiProgressBarValue(bar));
}

/* Separator */

static int dst_ui_horizontal_separator(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewHorizontalSeparator(), &separator_td));
}

static int dst_ui_vertical_separator(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewVerticalSeparator(), &separator_td));
}

/* Combobox */

static int dst_ui_combobox(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewCombobox(), &combobox_td));
}

static int dst_ui_combobox_selected(DstArgs args) {
    uiCombobox *cbox;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &combobox_td);
    if (args.n == 2) {
        int selected = 0;
        DST_ARG_BOOLEAN(selected, args, 1);
        uiComboboxSetSelected(cbox, selected);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_INTEGER(args, uiComboboxSelected(cbox));
}

static int dst_ui_combobox_append(DstArgs args) {
    uiCombobox *cbox;
    const uint8_t *text;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &combobox_td);
    DST_ARG_STRING(text, args, 1);
    uiComboboxAppend(cbox, (const char *)text);
    DST_RETURN(args, args.v[0]);
}

static void combobox_on_selected(uiCombobox *c, void *data) {
    (void) c;
    dst_ui_handler(data);
}

static int dst_ui_combobox_on_selected(DstArgs args) {
    uiCombobox *cbox;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &combobox_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiComboboxOnSelected(cbox, combobox_on_selected, handle);
    DST_RETURN(args, args.v[0]);
}

/* Editable Combobox */

static int dst_ui_editable_combobox(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewEditableCombobox(), &editable_combobox_td));
}

static int dst_ui_editable_combobox_text(DstArgs args) {
    uiEditableCombobox *cbox;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    if (args.n == 2) {
        const uint8_t *text;
        DST_ARG_STRING(text, args, 1);
        uiEditableComboboxSetText(cbox, (const char *)text);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiEditableComboboxText(cbox));
}

static int dst_ui_editable_combobox_append(DstArgs args) {
    uiEditableCombobox *cbox;
    const uint8_t *text;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    DST_ARG_STRING(text, args, 1);
    uiEditableComboboxAppend(cbox, (const char *)text);
    DST_RETURN(args, args.v[0]);
}

static void editable_combobox_on_changed(uiEditableCombobox *c, void *data) {
    (void) c;
    dst_ui_handler(data);
}

static int dst_ui_editable_combobox_on_changed(DstArgs args) {
    uiEditableCombobox *cbox;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(cbox, args, 0, &editable_combobox_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiEditableComboboxOnChanged(cbox, editable_combobox_on_changed, handle);
    DST_RETURN(args, args.v[0]);
}

/* Radio buttons */

static int dst_ui_radio_buttons(DstArgs args) {
    ASSERT_INITED(args);
    DST_RETURN(args, dst_ui_handle_to_control(uiNewRadioButtons(), &radio_buttons_td));
}

static int dst_ui_radio_buttons_selected(DstArgs args) {
    uiRadioButtons *rb;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    if (args.n == 2) {
        int selected = 0;
        DST_ARG_BOOLEAN(selected, args, 1);
        uiRadioButtonsSetSelected(rb, selected);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_INTEGER(args, uiRadioButtonsSelected(rb));
}

static int dst_ui_radio_buttons_append(DstArgs args) {
    uiRadioButtons *rb;
    const uint8_t *text;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    DST_ARG_STRING(text, args, 1);
    uiRadioButtonsAppend(rb, (const char *)text);
    DST_RETURN(args, args.v[0]);
}

static void radio_buttons_on_selected(uiRadioButtons *rb, void *data) {
    (void) rb;
    dst_ui_handler(data);
}

static int dst_ui_radio_buttons_on_selected(DstArgs args) {
    uiRadioButtons *rb;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(rb, args, 0, &radio_buttons_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiRadioButtonsOnSelected(rb, radio_buttons_on_selected, handle);
    DST_RETURN(args, args.v[0]);
}

/* Multiline Entry */

static int dst_ui_multiline_entry(DstArgs args) {
    int nowrap = 0;
    DST_MAXARITY(args, 1);
    ASSERT_INITED(args);
    if (args.n == 1)
        DST_ARG_BOOLEAN(nowrap, args, 0);
    DST_RETURN(args, dst_ui_handle_to_control(
                nowrap ? uiNewNonWrappingMultilineEntry() : uiNewMultilineEntry(),
                &multiline_entry_td));
}

static int dst_ui_multiline_entry_text(DstArgs args) {
    uiMultilineEntry *me;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    if (args.n == 2) {
        const uint8_t *text;
        DST_ARG_STRING(text, args, 1);
        uiMultilineEntrySetText(me, (const char *)text);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_CSTRING(args, uiMultilineEntryText(me));
}

static int dst_ui_multiline_entry_read_only(DstArgs args) {
    uiMultilineEntry *me;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    if (args.n == 2) {
        int selected;
        DST_ARG_BOOLEAN(selected, args, 1);
        uiMultilineEntrySetReadOnly(me, selected);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiMultilineEntryReadOnly(me));
}

static int dst_ui_multiline_entry_append(DstArgs args) {
    uiMultilineEntry *me;
    const uint8_t *text;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    DST_ARG_STRING(text, args, 1);
    uiMultilineEntryAppend(me, (const char *)text);
    DST_RETURN(args, args.v[0]);
}

static void multiline_entry_on_changed(uiMultilineEntry *e, void *data) {
    (void) e;
    dst_ui_handler(data);
}

static int dst_ui_multiline_entry_on_changed(DstArgs args) {
    uiMultilineEntry *me;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(me, args, 0, &multiline_entry_td);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiMultilineEntryOnChanged(me, multiline_entry_on_changed, handle);
    DST_RETURN(args, args.v[0]);
}

/* Menu Item */

static int dst_ui_menu_item_enable(DstArgs args) {
    uiMenuItem *mi;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(mi, args, 0, &menu_item_td);
    uiMenuItemEnable(mi);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_menu_item_disable(DstArgs args) {
    uiMenuItem *mi;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(mi, args, 0, &menu_item_td);
    uiMenuItemDisable(mi);
    DST_RETURN(args, args.v[0]);
}

static int dst_ui_menu_item_checked(DstArgs args) {
    uiMenuItem *mi;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_UITYPE(mi, args, 0, &menu_item_td);
    if (args.n == 2) {
        int checked;
        DST_ARG_BOOLEAN(checked, args, 1);
        uiMenuItemSetChecked(mi, checked);
        DST_RETURN(args, args.v[0]);
    }
    DST_RETURN_BOOLEAN(args, uiMenuItemChecked(mi));
}

static void menu_item_on_clicked(uiMenuItem *sender, uiWindow *window, void *data) {
    (void) sender;
    (void) window;
    dst_ui_handler(data);
}

static int dst_ui_menu_item_on_clicked(DstArgs args) {
    uiMenuItem *mi;
    DST_FIXARITY(args, 2);
    DST_CHECKMANY(args, 1, DST_TFLAG_CALLABLE);
    DST_ARG_UITYPE(mi, args, 0, &menu_item_td);
    void *handle = dst_ui_to_handler_data(args.v[1]);
    uiMenuItemOnClicked(mi, menu_item_on_clicked, handle);
    DST_RETURN(args, args.v[0]);
}

/* Menu */

static int dst_ui_menu(DstArgs args) {
    const uint8_t *name;
    ASSERT_INITED(args);
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(name, args, 0);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiNewMenu((const char *)name),
                &menu_td));
}

static int dst_ui_menu_append_item(DstArgs args) {
    const uint8_t *name;
    uiMenu *menu;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    DST_ARG_STRING(name, args, 1);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiMenuAppendItem(menu, (const char *)name),
                &menu_item_td));
}

static int dst_ui_menu_append_check_item(DstArgs args) {
    const uint8_t *name;
    uiMenu *menu;
    DST_FIXARITY(args, 2);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    DST_ARG_STRING(name, args, 1);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiMenuAppendCheckItem(menu, (const char *)name),
                &menu_item_td));
}

static int dst_ui_menu_append_quit_item(DstArgs args) {
    uiMenu *menu;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiMenuAppendQuitItem(menu),
                &menu_item_td));
}

static int dst_ui_menu_append_about_item(DstArgs args) {
    uiMenu *menu;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiMenuAppendAboutItem(menu),
                &menu_item_td));
}

static int dst_ui_menu_append_preferences_item(DstArgs args) {
    uiMenu *menu;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    DST_RETURN(args, dst_ui_handle_to_control(
                uiMenuAppendPreferencesItem(menu),
                &menu_item_td));
}

static int dst_ui_menu_append_separator(DstArgs args) {
    uiMenu *menu;
    DST_FIXARITY(args, 1);
    DST_ARG_UITYPE(menu, args, 0, &menu_td);
    uiMenuAppendSeparator(menu);
    DST_RETURN(args, args.v[0]);
}

/*****************************************************************************/

static const DstReg cfuns[] = {
    {"init", dst_ui_init},
    {"quit", dst_ui_quit},
    {"uninit", dst_ui_uninit},
    {"main", dst_ui_main},
    {"main-step", dst_ui_mainstep},
    {"main-steps", dst_ui_mainsteps},
    {"queue-main", dst_ui_queue_main},
    {"on-should-quit", dst_ui_on_should_quit},
    {"timer", dst_ui_timer},
    {"save-file", dst_ui_save_file},
    {"open-file", dst_ui_open_file},
    {"message-box", dst_ui_message_box},
    {"message-box-error", dst_ui_message_box_error},

    /* Controls */
    {"destroy", dst_ui_destroy},
    {"parent", dst_ui_parent},
    {"top-level", dst_ui_top_level},
    {"visible", dst_ui_visible},
    {"enabled", dst_ui_enabled},
    {"show", dst_ui_show},
    {"hide", dst_ui_hide},
    {"enable", dst_ui_enable},
    {"disable", dst_ui_disable},

    /* Window */
    {"window", dst_ui_window},
    {"window.title", dst_ui_window_title},
    {"window.content-size", dst_ui_window_content_size},
    {"window.fullscreen", dst_ui_window_fullscreen},
    {"window.on-content-size-changed", dst_ui_window_on_content_size_changed},
    {"window.on-closing", dst_ui_window_on_closing},
    {"window.set-child", dst_ui_window_set_child},
    {"window.borderless", dst_ui_window_borderless},
    {"window.margined", dst_ui_window_margined},

    /* Button */
    {"button", dst_ui_button},
    {"button.text", dst_ui_button_text},
    {"button.on-clicked", dst_ui_button_on_clicked},

    /* Box */
    {"vertical-box", dst_ui_vertical_box},
    {"horizontal-box", dst_ui_horizontal_box},
    {"box.padded", dst_ui_box_padded},
    {"box.append", dst_ui_box_append},
    {"box.delete", dst_ui_box_delete},

    /* Check box */
    {"checkbox", dst_ui_checkbox},
    {"checkbox.on-toggled", dst_ui_checkbox_on_toggled},
    {"checkbox.text", dst_ui_checkbox_text},
    {"checkbox.checked", dst_ui_checkbox_checked},

    /* Entry */
    {"entry", dst_ui_entry},
    {"password-entry", dst_ui_password_entry},
    {"search-entry", dst_ui_search_entry},
    {"entry.text", dst_ui_entry_text},
    {"entry.read-only", dst_ui_entry_read_only},
    {"entry.on-changed", dst_ui_entry_on_changed},

    /* Label */
    {"label", dst_ui_label},
    {"label.text", dst_ui_label_text},

    /* Tab */
    {"tab", dst_ui_tab},
    {"tab.margined", dst_ui_tab_margined},
    {"tab.num-pages", dst_ui_tab_num_pages},
    {"tab.append", dst_ui_tab_append},
    {"tab.insert-at", dst_ui_tab_insert_at},
    {"tab.delete", dst_ui_tab_delete},

    /* Group */
    {"group", dst_ui_group},
    {"group.title", dst_ui_group_title},
    {"group.margined", dst_ui_group_margined},
    {"group.set-child", dst_ui_group_set_child},

    /* Spinbox */
    {"spinbox", dst_ui_spinbox},
    {"spinbox.value", dst_ui_spinbox_value},
    {"spinbox.on-changed", dst_ui_spinbox_on_changed},

    /* Slider */
    {"slider", dst_ui_slider},
    {"slider.value", dst_ui_slider_value},
    {"slider.on-changed", dst_ui_slider_on_changed},

    /* Progress Bar */
    {"progress-bar", dst_ui_progress_bar},
    {"progress-bar.value", dst_ui_progress_bar_value},

    /* Separator */
    {"horizontal-separator", dst_ui_horizontal_separator},
    {"vertical-separator", dst_ui_vertical_separator},

    /* Combobox */
    {"combobox", dst_ui_combobox},
    {"combobox.append", dst_ui_combobox_append},
    {"combobox.selected", dst_ui_combobox_selected},
    {"combobox.on-selected", dst_ui_combobox_on_selected},

    /* Editable Combobox */
    {"editable-combobox", dst_ui_editable_combobox},
    {"editable-combobox.text", dst_ui_editable_combobox_text},
    {"editable-combobox.append", dst_ui_editable_combobox_append},
    {"editable-combobox.on-changed", dst_ui_editable_combobox_on_changed},

    /* Radio Buttons */
    {"radio-buttons", dst_ui_radio_buttons},
    {"radio-buttons.append", dst_ui_radio_buttons_append},
    {"radio-buttons.selected", dst_ui_radio_buttons_selected},
    {"radio-buttons.on-selected", dst_ui_radio_buttons_on_selected},

    /* Multiline Entry */
    {"multiline-entry", dst_ui_multiline_entry},
    {"multiline-entry.text", dst_ui_multiline_entry_text},
    {"multiline-entry.read-only", dst_ui_multiline_entry_read_only},
    {"multiline-entry.append", dst_ui_multiline_entry_append},
    {"multiline-entry.on-changed", dst_ui_multiline_entry_on_changed},

    /* Menu Item */
    {"menu-item.enable", dst_ui_menu_item_enable},
    {"menu-item.disable", dst_ui_menu_item_disable},
    {"menu-item.checked", dst_ui_menu_item_checked},
    {"menu-item.on-clicked", dst_ui_menu_item_on_clicked},

    /* Menu */
    {"menu", dst_ui_menu},
    {"menu.append-item", dst_ui_menu_append_item},
    {"menu.append-check-item", dst_ui_menu_append_check_item},
    {"menu.append-quit-item", dst_ui_menu_append_quit_item},
    {"menu.append-about-item", dst_ui_menu_append_about_item},
    {"menu.append-preferences-item", dst_ui_menu_append_preferences_item},
    {"menu.append-separator", dst_ui_menu_append_separator},

    {NULL, NULL}
};

DST_MODULE_ENTRY(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}
