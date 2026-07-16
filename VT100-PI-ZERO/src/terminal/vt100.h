// VT100 dispatcher: wires the escape-sequence parser to the screen model and
// generates the terminal's replies (DSR/DA). Also holds the input-affecting
// modes the keyboard layer needs (cursor-key and keypad application modes).
#ifndef VT100_H
#define VT100_H

#include <stdint.h>

void vt100_init(void);
void vt100_feed(uint8_t c);          // feed one received byte

// Input modes queried by the keyboard layer.
int vt100_cursor_keys_app(void);     // DECCKM: arrows send SS3 not CSI
int vt100_keypad_app(void);          // DECKPAM/DECKPNM
int vt100_newline_mode(void);        // LNM: Return sends CR+LF

// Returns (and clears) 1 if a BEL was received since the last call.
int vt100_take_bell(void);

#endif // VT100_H
