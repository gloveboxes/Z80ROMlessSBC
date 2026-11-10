#include "core_coordination.h"

uint32_t cpu_state;
uint32_t usb_state;
uint32_t load_cancelled;
uint32_t core1_ready;
uint32_t workers_ready;
uint32_t flash_pause_request;
uint32_t flash_paused;
queue_t control_commands;
