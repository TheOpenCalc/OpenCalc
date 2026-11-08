#ifndef TEST_STUB_TOOLS_H
#define TEST_STUB_TOOLS_H
#include <stdint.h>
int scan_text_char(void);
int scan_keypad_nowait(void);
int scan_keypad(void);
void pyhost_send_char(int c);
void pyhost_send_key(int k);
void pyhost_drain(void);
#endif
