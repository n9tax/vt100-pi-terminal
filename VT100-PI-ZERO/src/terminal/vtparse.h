// DEC/ANSI escape-sequence parser: the canonical Paul Williams state machine
// (https://vt100.net/emu/dec_ansi_parser). It converts a byte stream into a
// sequence of well-defined actions (print/execute/dispatch/...) and collects
// parameters and intermediate characters. The dispatcher (vt100.c) supplies a
// callback and interprets the actions against the screen model.
#ifndef VTPARSE_H
#define VTPARSE_H

#include <stdint.h>

#define VT_MAX_PARAMS        16
#define VT_MAX_INTERMEDIATE  2

typedef enum {
    VT_ACTION_NONE = 0,
    VT_ACTION_PRINT,
    VT_ACTION_EXECUTE,       // C0/C1 control
    VT_ACTION_ESC_DISPATCH,  // final of an ESC sequence
    VT_ACTION_CSI_DISPATCH,  // final of a CSI sequence
    VT_ACTION_HOOK,          // DCS start
    VT_ACTION_PUT,           // DCS data byte
    VT_ACTION_UNHOOK,        // DCS end
    VT_ACTION_OSC_START,
    VT_ACTION_OSC_PUT,
    VT_ACTION_OSC_END,
} vt_action_t;

typedef enum {
    VT_GROUND = 0,
    VT_ESCAPE, VT_ESCAPE_INTERMEDIATE,
    VT_CSI_ENTRY, VT_CSI_PARAM, VT_CSI_INTERMEDIATE, VT_CSI_IGNORE,
    VT_DCS_ENTRY, VT_DCS_PARAM, VT_DCS_INTERMEDIATE, VT_DCS_PASSTHROUGH, VT_DCS_IGNORE,
    VT_OSC_STRING, VT_SOS_PM_APC_STRING,
} vt_state_t;

struct vtparse;
typedef void (*vtparse_cb_t)(struct vtparse *p, vt_action_t action, uint8_t ch);

typedef struct vtparse {
    vt_state_t state;
    vtparse_cb_t cb;
    void       *user;

    int      params[VT_MAX_PARAMS];   // -1 = default (unspecified)
    int      num_params;

    uint8_t  intermediate[VT_MAX_INTERMEDIATE];
    int      num_intermediate;

    uint8_t  prefix;                  // private marker '<' '=' '>' '?', else 0
    int      cur_param;               // building the current parameter
    int      param_started;          // a digit has been seen for cur_param
} vtparse_t;

void vtparse_init(vtparse_t *p, vtparse_cb_t cb, void *user);
void vtparse_byte(vtparse_t *p, uint8_t b);

#endif // VTPARSE_H
