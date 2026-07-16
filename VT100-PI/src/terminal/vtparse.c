#include "terminal/vtparse.h"

// ---- collected-state helpers --------------------------------------------
static void do_clear(vtparse_t *p) {
    p->num_params = 0;
    p->num_intermediate = 0;
    p->prefix = 0;
    p->cur_param = 0;
    p->param_started = 0;
}

static void push_param(vtparse_t *p) {
    if (p->num_params < VT_MAX_PARAMS)
        p->params[p->num_params++] = p->param_started ? p->cur_param : -1;
    p->cur_param = 0;
    p->param_started = 0;
}

static void do_param(vtparse_t *p, uint8_t ch) {
    if (ch == ';' || ch == ':') {           // ':' subparam treated as separator
        push_param(p);
    } else {                                 // 0x30-0x39 digit
        if (!p->param_started) { p->cur_param = 0; p->param_started = 1; }
        p->cur_param = p->cur_param * 10 + (ch - '0');
        if (p->cur_param > 65535) p->cur_param = 65535;
    }
}

static void do_collect(vtparse_t *p, uint8_t ch) {
    if (ch >= 0x3c && ch <= 0x3f)            // private marker '<' '=' '>' '?'
        p->prefix = ch;
    else if (p->num_intermediate < VT_MAX_INTERMEDIATE)
        p->intermediate[p->num_intermediate++] = ch;
}

static void emit(vtparse_t *p, vt_action_t a, uint8_t ch) { p->cb(p, a, ch); }

// ---- state transitions with entry/exit actions --------------------------
static void set_state(vtparse_t *p, vt_state_t ns) {
    if (p->state == ns) return;
    // exit actions
    if (p->state == VT_OSC_STRING)       emit(p, VT_ACTION_OSC_END, 0);
    else if (p->state == VT_DCS_PASSTHROUGH) emit(p, VT_ACTION_UNHOOK, 0);
    p->state = ns;
    // entry actions
    switch (ns) {
        case VT_ESCAPE: case VT_CSI_ENTRY: case VT_DCS_ENTRY: do_clear(p); break;
        case VT_OSC_STRING:      emit(p, VT_ACTION_OSC_START, 0); break;
        case VT_DCS_PASSTHROUGH: emit(p, VT_ACTION_HOOK, 0);      break;
        default: break;
    }
}

static void csi_dispatch(vtparse_t *p, uint8_t ch) {
    push_param(p);
    emit(p, VT_ACTION_CSI_DISPATCH, ch);
    set_state(p, VT_GROUND);
}
static void esc_dispatch(vtparse_t *p, uint8_t ch) {
    emit(p, VT_ACTION_ESC_DISPATCH, ch);
    set_state(p, VT_GROUND);
}

static int is_c0_exec(uint8_t b) {
    return (b <= 0x17) || b == 0x19 || (b >= 0x1c && b <= 0x1f);
}

void vtparse_init(vtparse_t *p, vtparse_cb_t cb, void *user) {
    p->state = VT_GROUND;
    p->cb = cb;
    p->user = user;
    do_clear(p);
}

void vtparse_byte(vtparse_t *p, uint8_t b) {
    // "anywhere" transitions.
    if (b == 0x1b) { set_state(p, VT_ESCAPE); return; }
    if (b == 0x18 || b == 0x1a) { emit(p, VT_ACTION_EXECUTE, b); set_state(p, VT_GROUND); return; }

    switch (p->state) {
    case VT_GROUND:
        if (is_c0_exec(b) || b == 0x0a || b == 0x0d || b == 0x08 || b == 0x09 || b == 0x07)
            emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x20)                  // 0x20-0xff printable (CP437)
            emit(p, VT_ACTION_PRINT, b);
        break;

    case VT_ESCAPE:
        if (is_c0_exec(b) || b == 0x0a || b == 0x0d || b == 0x08 || b == 0x09 || b == 0x07)
            emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x20 && b <= 0x2f) { do_collect(p, b); set_state(p, VT_ESCAPE_INTERMEDIATE); }
        else if (b == 0x5b) set_state(p, VT_CSI_ENTRY);              // '['
        else if (b == 0x5d) set_state(p, VT_OSC_STRING);            // ']'
        else if (b == 0x50) set_state(p, VT_DCS_ENTRY);            // 'P'
        else if (b == 0x58 || b == 0x5e || b == 0x5f) set_state(p, VT_SOS_PM_APC_STRING);
        else if (b >= 0x30 && b <= 0x7e) esc_dispatch(p, b);
        break;

    case VT_ESCAPE_INTERMEDIATE:
        if (is_c0_exec(b)) emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x20 && b <= 0x2f) do_collect(p, b);
        else if (b >= 0x30 && b <= 0x7e) esc_dispatch(p, b);
        break;

    case VT_CSI_ENTRY:
        if (is_c0_exec(b)) emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x40 && b <= 0x7e) csi_dispatch(p, b);
        else if ((b >= 0x30 && b <= 0x39) || b == 0x3b || b == 0x3a) { do_param(p, b); set_state(p, VT_CSI_PARAM); }
        else if (b >= 0x3c && b <= 0x3f) { do_collect(p, b); set_state(p, VT_CSI_PARAM); }
        else if (b >= 0x20 && b <= 0x2f) { do_collect(p, b); set_state(p, VT_CSI_INTERMEDIATE); }
        break;

    case VT_CSI_PARAM:
        if (is_c0_exec(b)) emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x40 && b <= 0x7e) csi_dispatch(p, b);
        else if ((b >= 0x30 && b <= 0x39) || b == 0x3b || b == 0x3a) do_param(p, b);
        else if (b >= 0x3c && b <= 0x3f) set_state(p, VT_CSI_IGNORE);
        else if (b >= 0x20 && b <= 0x2f) { do_collect(p, b); set_state(p, VT_CSI_INTERMEDIATE); }
        break;

    case VT_CSI_INTERMEDIATE:
        if (is_c0_exec(b)) emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x40 && b <= 0x7e) csi_dispatch(p, b);
        else if (b >= 0x20 && b <= 0x2f) do_collect(p, b);
        else if (b >= 0x30 && b <= 0x3f) set_state(p, VT_CSI_IGNORE);
        break;

    case VT_CSI_IGNORE:
        if (is_c0_exec(b)) emit(p, VT_ACTION_EXECUTE, b);
        else if (b >= 0x40 && b <= 0x7e) set_state(p, VT_GROUND);
        break;

    // ---- DCS (parsed and consumed; dispatcher may ignore) ----
    case VT_DCS_ENTRY:
        if (b >= 0x40 && b <= 0x7e) set_state(p, VT_DCS_PASSTHROUGH);
        else if ((b >= 0x30 && b <= 0x39) || b == 0x3b || b == 0x3a) { do_param(p, b); set_state(p, VT_DCS_PARAM); }
        else if (b >= 0x3c && b <= 0x3f) { do_collect(p, b); set_state(p, VT_DCS_PARAM); }
        else if (b >= 0x20 && b <= 0x2f) { do_collect(p, b); set_state(p, VT_DCS_INTERMEDIATE); }
        break;
    case VT_DCS_PARAM:
        if (b >= 0x40 && b <= 0x7e) set_state(p, VT_DCS_PASSTHROUGH);
        else if ((b >= 0x30 && b <= 0x39) || b == 0x3b || b == 0x3a) do_param(p, b);
        else if (b >= 0x3c && b <= 0x3f) set_state(p, VT_DCS_IGNORE);
        else if (b >= 0x20 && b <= 0x2f) { do_collect(p, b); set_state(p, VT_DCS_INTERMEDIATE); }
        break;
    case VT_DCS_INTERMEDIATE:
        if (b >= 0x40 && b <= 0x7e) set_state(p, VT_DCS_PASSTHROUGH);
        else if (b >= 0x20 && b <= 0x2f) do_collect(p, b);
        else if (b >= 0x30 && b <= 0x3f) set_state(p, VT_DCS_IGNORE);
        break;
    case VT_DCS_PASSTHROUGH:
        if (b < 0x20 && !is_c0_exec(b)) { /* ignore */ }
        else if (b <= 0x7e) emit(p, VT_ACTION_PUT, b);
        break;
    case VT_DCS_IGNORE:
        break;

    // ---- OSC / string ----
    case VT_OSC_STRING:
        if (b == 0x07) set_state(p, VT_GROUND);           // BEL terminator
        else if (b >= 0x20) emit(p, VT_ACTION_OSC_PUT, b);
        break;
    case VT_SOS_PM_APC_STRING:
        break;                                            // consumed, ignored
    }
}
