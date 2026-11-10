#ifndef Z80_CORE_COORDINATION_H
#define Z80_CORE_COORDINATION_H

#include <stdint.h>
#include "pico/util/queue.h"

#ifndef Z80_STAGE09_AUTOBOOT
#define Z80_STAGE09_AUTOBOOT 0
#endif

enum {
  USB_COMMAND_ESCAPE = 0x1D,
  USB_REPORT_CONNECTION = 0x100,
  USB_CONFIG_REBOOT = 0x101,
  CPU_RUNNING = 1u << 0,
  CPU_STOPPING = 1u << 1,
  CPU_LOADING = 1u << 2,
  USB_CONNECTED = 1u << 0,
  USB_BROWSER = 1u << 1,
  USB_SETUP = 1u << 2,
};

/* Internal cross-core boundary. Published words use release stores/acquire
 * loads; control_commands is the SDK's synchronized core-1-to-core-0 queue. */
extern uint32_t cpu_state;           /* Core 0 publishes CPU flags. */
extern uint32_t usb_state;           /* Core 1 publishes connection flags. */
extern uint32_t load_cancelled;      /* Core 1 requests loader cancellation. */
extern uint32_t core1_ready;         /* Core 1 publishes startup outcome. */
extern uint32_t workers_ready;       /* Core 0 enables runtime workers. */
extern uint32_t flash_pause_request; /* Core 0 requests worker quiescence. */
extern uint32_t flash_paused;        /* Core 1 acknowledges quiescence. */
extern queue_t control_commands;

int core0_main(void);
void core1_main(void);

#endif
