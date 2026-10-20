#include "ansi_input.h"

#define CTRL_KEY(ch) ((ch) & 0x1f)

enum {
  KEY_STATE_NORMAL,
  KEY_STATE_ESC,
  KEY_STATE_ESC_BRACKET,
  KEY_STATE_ESC_BRACKET_NUM,
};

static uint8_t key_state;
static uint8_t pending_key;
static uint32_t esc_start;

void ansi_input_reset(void) {
  key_state = KEY_STATE_NORMAL;
  pending_key = 0;
  esc_start = 0;
}

uint8_t ansi_input_process(uint8_t ch, uint32_t now_ms) {
  switch (key_state) {
    case KEY_STATE_NORMAL:
      if (ch == 0x1b) {
        key_state = KEY_STATE_ESC;
        esc_start = now_ms;
        return 0;
      }
      if (ch == 0x7f || ch == 0x08)
        return CTRL_KEY('H');
      return ch;

    case KEY_STATE_ESC:
      if (ch == 0) {
        if ((uint32_t)(now_ms - esc_start) >= ANSI_INPUT_ESC_GRACE_MS) {
          key_state = KEY_STATE_NORMAL;
          return 0x1b;
        }
        return 0;
      }
      if (ch == '[') {
        key_state = KEY_STATE_ESC_BRACKET;
        return 0;
      }
      key_state = KEY_STATE_NORMAL;
      return ch;

    case KEY_STATE_ESC_BRACKET:
      if (ch == 0)
        return 0;
      switch (ch) {
        case 'A':
          key_state = KEY_STATE_NORMAL;
          return CTRL_KEY('E');
        case 'B':
          key_state = KEY_STATE_NORMAL;
          return CTRL_KEY('X');
        case 'C':
          key_state = KEY_STATE_NORMAL;
          return CTRL_KEY('D');
        case 'D':
          key_state = KEY_STATE_NORMAL;
          return CTRL_KEY('S');
        case '2':
          pending_key = CTRL_KEY('O');
          key_state = KEY_STATE_ESC_BRACKET_NUM;
          return 0;
        case '3':
          pending_key = CTRL_KEY('G');
          key_state = KEY_STATE_ESC_BRACKET_NUM;
          return 0;
        case '5':
          pending_key = CTRL_KEY('R');
          key_state = KEY_STATE_ESC_BRACKET_NUM;
          return 0;
        case '6':
          pending_key = CTRL_KEY('V');
          key_state = KEY_STATE_ESC_BRACKET_NUM;
          return 0;
        default:
          key_state = KEY_STATE_NORMAL;
          return 0;
      }

    case KEY_STATE_ESC_BRACKET_NUM:
      if (ch == 0)
        return 0;
      key_state = KEY_STATE_NORMAL;
      if (ch == '~') {
        uint8_t result = pending_key;
        pending_key = 0;
        return result;
      }
      pending_key = 0;
      return 0;
  }

  ansi_input_reset();
  return 0;
}
