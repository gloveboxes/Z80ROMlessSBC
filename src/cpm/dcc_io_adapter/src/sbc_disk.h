#ifndef SBC_DISK_H
#define SBC_DISK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SBC_DISK_DRIVE_COUNT 4

typedef struct sbc_disk_context
{
    FILE *drives[SBC_DISK_DRIVE_COUNT];
    uint8_t drive;
    uint16_t lba;
    uint8_t status;
    uint8_t record[128];
    size_t record_index;
    int active;
} sbc_disk_context_t;

int sbc_disk_init(sbc_disk_context_t *context, const char *environment_file,
                  char *error, size_t error_size);
void sbc_disk_close(sbc_disk_context_t *context);
int sbc_disk_handles_port(uint8_t port);
uint8_t sbc_disk_input(sbc_disk_context_t *context, uint8_t port);
void sbc_disk_output(sbc_disk_context_t *context, uint8_t port, uint8_t data);

#endif
