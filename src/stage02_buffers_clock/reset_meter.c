#include <stdio.h>

#include "pico/stdlib.h"
#include "z80sbc/pins.h"
#include "z80sbc/supervisor.h"

enum {
  STEP_PREPARATION,
  STEP_POWER,
  STEP_SAFE_OUTPUTS,
  STEP_FIRST_CLOCK,
  STEP_RESET_CONTROLS = STEP_FIRST_CLOCK + 12,
  STEP_INHIBIT,
  STEP_ADDRESS_PULLS,
  STEP_FINISHED,
};

static unsigned meter_step;
static bool stopped;
static int pending_command;
static bool invalid_line;

static void hold_safe(void) {
  z80_safe_startup();
}

static void print_help(void) {
  printf("\nRESET-HELD MULTIMETER GUIDE - no SRAM loading or CPU execution\n"
         "Fitted: Pico 2 W, Z80 U1, SRAM U2, HCT32 U3, AHCT244 U4.\n"
         "U7 LVC244 / U9 AHCT245 / U10 LVC245 may be absent.\n"
         "Use ONE letter then Enter; CR, LF and CRLF are accepted:\n"
         "  n = acknowledge current readings and advance ONE step\n"
         "  r = repeat current instructions WITHOUT changing outputs\n"
         "  h = help; b = restart guide with CLK LOW\n"
         "  x = STOP immediately, even without Enter; CLK LOW, RESET# LOW\n"
         "Do not paste commands or hold a key. Take each reading first.\n"
         "A mismatch: press x, record the voltage, disconnect USB then 5 V,\n"
         "and correct wiring with ALL power disconnected. Do not advance.\n"
         "Firmware does NOT measure voltages or report hardware PASS.\n"
         "RESET# stays LOW, BUSREQ# HIGH, SRAM read inhibit HIGH throughout.\n"
         "No PWM, instruction injection, data drive, or monitor-input reads.\n"
         "Clock HIGH/LOW states are held indefinitely for your meter.\n");
}

static void print_step(void) {
  if (stopped) {
    printf("\nSTOPPED: CLK LOW; RESET# LOW; SRAM read inhibit HIGH.\n"
           "Use b + Enter only after checking the fault; h + Enter for help.\n");
    return;
  }

  printf("\nSTEP %u/%u - verification=unmeasured\n",
         meter_step + 1, STEP_FINISHED + 1);
  if (meter_step == STEP_PREPARATION) {
    printf("PREPARATION - CLK held LOW; RESET# held LOW.\n"
           "Use DC VOLTS, leads in COM and V sockets, NOT current/ohms mode.\n"
           "Black lead: common circuit GND. Red lead: named measurement pin.\n"
           "Pin numbers are IC package pins counted from the notch/dot,\n"
           "NOT breadboard rows. Pico header pins are explicitly labelled.\n"
           "U4 AHCT244 notch faces DOWN in the documented layout.\n"
           "Use insulated tips; do not bridge neighbouring contacts.\n"
           "Power off before moving clips or changing wiring.\n"
           "All fitted 5 V ICs must be powered; never use USB alone.\n"
           "Power on: external 5 V BEFORE USB; off: USB BEFORE external 5 V.\n"
           "Confirm bypasses, common GND, correct ICs/orientation, startup\n"
           "pulls and no raw 5 V wire to Pico before continuing.\n"
           "Do not insert/remove ICs while powered.\n");
  } else if (meter_step == STEP_POWER) {
    printf("POWER - measure red lead at each point, black at common GND:\n"
           "  U1 Z80 pin 11, U2 SRAM pins 32 and 30,\n"
           "  U3 HCT32 pin 14, U4 AHCT244 pin 20: nominal +5 V.\n"
           "  Bench target: 4.75 to 5.25 V at EVERY listed point.\n"
           "  Pico header 36 (3V3 OUT): nominal 3.3 V; target 3.2 to 3.4 V.\n"
           "  U1 pin 29, U2 pins 16 and 2 (A16), U3 pin 7,\n"
           "  U4 pins 10, 1 and 19 (both enables): approximately 0 V.\n"
           "If a rail is missing/wrong, STOP; do not test logic.\n");
  } else if (meter_step == STEP_SAFE_OUTPUTS) {
    printf("SAFE STATIC OUTPUTS - measure every endpoint:\n"
           "  RESET#: Pico GP3/header 5 -> U1 pin 26: LOW, about 0 V.\n"
           "  CLK: GP2/header 4 -> U4 pin 2: LOW; U4 pin 18 -> U1 pin 6: LOW.\n"
           "  BUSREQ#: GP4/header 6 -> U4 pin 4: about 3.3 V;\n"
           "            U4 pin 16 -> U1 pin 25: about +5 V (inactive).\n"
           "  Inhibit: GP5/header 7 -> U4 pin 6: about 3.3 V;\n"
           "           U4 pin 14 -> U3 pin 2: about +5 V.\n"
           "  GP6/header 9 and GP7/header 10: about 3.3 V (data paths off).\n"
           "  GP9/header 12 -> U3 pin 5: LOW, about 0 V.\n"
           "  U3 pin 3 -> U2 SRAM pin 24 (OE#): about +5 V (disabled).\n");
  } else if (meter_step < STEP_RESET_CONTROLS) {
    unsigned half_step = meter_step - STEP_FIRST_CLOCK;
    bool high = half_step % 2 == 0;
    printf("RESET CLOCK %u/6 - CLK HELD %s; RESET# remains LOW.\n",
           half_step / 2 + 1, high ? "HIGH" : "LOW");
    printf("  GP2/Pico header 4 and U4 input pin 2: %s.\n"
           "  U4 output pin 18 and U1 Z80 CLK pin 6: %s.\n"
           "  U1 RESET# pin 26: about 0 V.\n"
           "  U2 SRAM OE# pin 24: about +5 V.\n"
           "Wait for your meter to settle; record all readings.\n"
           "The NEXT n + Enter changes only the next held clock level.\n",
           high ? "about 3.3 V" : "about 0 V",
           high ? "about +5 V" : "about 0 V");
  } else if (meter_step == STEP_RESET_CONTROLS) {
    printf("SIX RESET CLOCKS COMPLETED - CLK LOW; reset still asserted.\n"
           "Measure the CPU-to-SRAM control chains:\n"
           "  MREQ#: U1 pin 19 -> U4 input 8; U4 output 12 -> U2 CE# pin 22.\n"
           "  WR#:   U1 pin 22 -> U4 input 11; U4 output 9 -> U2 WE# pin 29.\n"
           "  RD#:   U1 pin 21 -> U4 input 13; U4 output 7 -> U3 pin 1.\n"
           "Expected: all HIGH/inactive; U4 outputs close to +5 V.\n"
           "Raw Z80 HIGH is TTL (may be below +5 V; >=2.4 V is expected).\n"
           "SRAM CE#, WE# and OE# must all remain HIGH. STOP if LOW.\n");
  } else if (meter_step == STEP_INHIBIT) {
    printf("HCT32 STATIC CHECK - no inputs will be forced LOW:\n"
           "  U3 pin 2 (buffered inhibit): about +5 V.\n"
           "  U3 pin 3 -> SRAM OE# pin 24: about +5 V.\n"
           "  U1 IORQ# pin 20 -> U3 pin 4: HIGH; GP9 -> U3 pin 5: LOW.\n"
           "  U3 pin 6 -> U1 WAIT# pin 24: about +5 V.\n"
           "  GP6 -> U3 pin 10: about 3.3 V;\n"
           "  U3 pin 8 -> empty U9 OE# socket pin 19: about +5 V.\n"
           "This checks only these static combinations, NOT the full OR\n"
           "truth tables. Do not ground a CPU output or bypass a buffer.\n");
  } else if (meter_step == STEP_ADDRESS_PULLS) {
    printf("ADDRESS PULLS - reset floats the CPU address bus.\n"
           "Existing 10k pulls should hold each SRAM address near +5 V:\n"
           "  A0=12 A1=11 A2=10 A3=9 A4=8 A5=7 A6=6 A7=5\n"
           "  A8=27 A9=26 A10=23 A11=25 A12=4 A13=28 A14=3 A15=31\n"
           "These are U2 SRAM pin numbers. A16 pin 2 must stay about 0 V.\n"
           "This does NOT prove CPU address drive, distinct address wiring,\n"
           "or SRAM contents. Do not interpret the floating data bus.\n");
  } else {
    printf("GUIDE COMPLETE - operator advanced through the measurement steps.\n"
           "No automatic hardware PASS: keep your actual voltage record.\n"
           "CLK LOW; RESET# LOW; SRAM output inhibited; no further clocks.\n"
           "Proven only if your readings agreed: static rails, control\n"
           "levels, held clock translation and address pull levels.\n"
           "NOT proven: pulse/edge timing, CPU execution, SRAM reads/writes,\n"
           "address aliasing, or full logic truth tables.\n"
           "Fit/check U7 LVC244, U9 AHCT245 and U10 LVC245 with power OFF\n"
           "before attempting the Phase 6 loader. Do not release reset here.\n");
  }
  printf("Bench indications: LOW near 0 V (target <=0.3 V); Pico HIGH near\n"
         "3.3 V; buffered 5 V HIGH near its measured rail (within 0.5 V).\n"
         "These are troubleshooting targets, NOT timing/logic qualification.\n"
         "If readings agree, record them, then n + Enter; r repeats; x stops.\n");
}

static void restart_guide(void) {
  hold_safe();
  meter_step = STEP_PREPARATION;
  stopped = false;
  print_help();
  print_step();
}

static void run_command(int command) {
  if (command == 'b') {
    restart_guide();
  } else if (command == 'h') {
    print_help();
    print_step();
  } else if (command == 'r') {
    print_step();
  } else if (command == 'n') {
    if (stopped || meter_step == STEP_FINISHED) {
      printf("No advance: guide stopped/complete. Use b + Enter to restart.\n");
      return;
    }
    ++meter_step;
    bool clock_high = meter_step >= STEP_FIRST_CLOCK &&
                      meter_step < STEP_RESET_CONTROLS &&
                      (meter_step - STEP_FIRST_CLOCK) % 2 == 0;
    gpio_put(PIN_CLK, clock_high);
    print_step();
  } else {
    printf("ERROR: unknown command. Use h + Enter; outputs unchanged.\n");
  }
}

static void handle_character(int character) {
  if (character == 'x') {
    hold_safe();
    stopped = true;
    pending_command = 0;
    invalid_line = true;
    print_step();
  } else if (character == '\r' || character == '\n') {
    if (invalid_line)
      printf("Line discarded; use ONE command letter then Enter.\n");
    else if (pending_command != 0)
      run_command(pending_command);
    pending_command = 0;
    invalid_line = false;
  } else if (pending_command != 0 || invalid_line) {
    invalid_line = true;
  } else {
    pending_command = character;
  }
}

int main(void) {
  hold_safe();
  stdio_init_all();
  while (true) {
    while (!stdio_usb_connected())
      tight_loop_contents();
    pending_command = 0;
    invalid_line = false;
    restart_guide();
    while (stdio_usb_connected()) {
      int character = getchar_timeout_us(0);
      if (character >= 0)
        handle_character(character);
      tight_loop_contents();
    }
    hold_safe();
  }
}
