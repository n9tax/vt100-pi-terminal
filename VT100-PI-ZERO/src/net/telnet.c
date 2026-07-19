#include "net/telnet.h"
#include "config.h"     // TERM_COLS / TERM_ROWS for the NAWS window size

// Telnet command bytes.
#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
// Options we care about.
#define OPT_ECHO 1
#define OPT_SGA  3   // suppress go-ahead
#define OPT_TTYPE 24 // terminal type
#define OPT_NAWS 31  // window size

static telnet_emit_fn  emit_cb;
static telnet_reply_fn reply_cb;

static enum { T_DATA, T_IAC, T_WILL, T_WONT, T_DO, T_DONT,
              T_SB_OPT, T_SB_DATA, T_SB_IAC } st;
static uint8_t sb_opt;   // option being subnegotiated

static void send3(uint8_t a, uint8_t b, uint8_t c) {
    uint8_t p[3] = { a, b, c };
    reply_cb(p, 3);
}

void telnet_init(telnet_emit_fn emit, telnet_reply_fn reply) {
    emit_cb = emit; reply_cb = reply;
    telnet_reset();
}

void telnet_reset(void) { st = T_DATA; }

// Answer a WILL/WONT/DO/DONT. We accept the server echoing and suppress-go-ahead
// (normal for interactive login), advertise we WILL send terminal type + NAWS,
// and refuse everything else.
static void on_will(uint8_t opt) {                 // server WILL <opt>
    if (opt == OPT_ECHO || opt == OPT_SGA) send3(IAC, DO, opt);
    else send3(IAC, DONT, opt);
}
static void on_wont(uint8_t opt) { send3(IAC, DONT, opt); }
static void on_do(uint8_t opt) {                   // server asks us to DO <opt>
    if (opt == OPT_TTYPE || opt == OPT_NAWS || opt == OPT_SGA) send3(IAC, WILL, opt);
    else send3(IAC, WONT, opt);
    if (opt == OPT_NAWS) {                          // send actual window size
        uint8_t naws[] = { IAC, SB, OPT_NAWS,
                           (uint8_t)(TERM_COLS >> 8), (uint8_t)(TERM_COLS & 0xff),
                           (uint8_t)(TERM_ROWS >> 8), (uint8_t)(TERM_ROWS & 0xff),
                           IAC, SE };
        reply_cb(naws, sizeof naws);
    }
}
static void on_dont(uint8_t opt) { send3(IAC, WONT, opt); }

// Server SB TTYPE SEND IAC SE  -> reply with our terminal type.
static void sb_end(void) {
    if (sb_opt == OPT_TTYPE) {
        static const uint8_t ttype[] = {
            IAC, SB, OPT_TTYPE, 0 /*IS*/, 'x','t','e','r','m','-','1','6','c','o','l','o','r',
            IAC, SE
        };
        reply_cb(ttype, sizeof ttype);
    }
}

void telnet_rx(const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        uint8_t b = buf[i];
        switch (st) {
            case T_DATA:
                if (b == IAC) st = T_IAC; else emit_cb(b);
                break;
            case T_IAC:
                switch (b) {
                    case IAC:  emit_cb(IAC); st = T_DATA; break;  // escaped 0xFF
                    case WILL: st = T_WILL; break;
                    case WONT: st = T_WONT; break;
                    case DO:   st = T_DO;   break;
                    case DONT: st = T_DONT; break;
                    case SB:   st = T_SB_OPT; break;
                    default:   st = T_DATA; break;                // GA/NOP/etc.
                }
                break;
            case T_WILL: on_will(b); st = T_DATA; break;
            case T_WONT: on_wont(b); st = T_DATA; break;
            case T_DO:   on_do(b);   st = T_DATA; break;
            case T_DONT: on_dont(b); st = T_DATA; break;
            case T_SB_OPT: sb_opt = b; st = T_SB_DATA; break;     // first SB byte = option
            case T_SB_DATA: if (b == IAC) st = T_SB_IAC;   break; // payload until IAC SE
            case T_SB_IAC:
                if (b == SE)  { sb_end(); st = T_DATA; }
                else          { st = T_SB_DATA; }                 // IAC IAC = literal payload
                break;
        }
    }
}
