#include "sbc_disk.h"

#include <stdio.h>
#include <string.h>

#define DISK_BYTES (80u * 32u * 128u)
#define RECORD_BYTES 128u

enum
{
    DISK_COMMAND = 0x10,
    DISK_DRIVE = 0x11,
    DISK_LBA_LOW = 0x12,
    DISK_LBA_HIGH = 0x13,
    DISK_DATA = 0x14,
    ADAPTER_ACTIVATE = 0x15,
    DISK_READ = 1,
    DISK_FLUSH = 5,
    DISK_READY = 0x01,
    DISK_DATA_READY = 0x02,
    DISK_DATA_ROOM = 0x04,
    DISK_ERROR = 0x80
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size != 0)
        snprintf(error, error_size, "%s", message);
}

static void close_drives(sbc_disk_context_t *context)
{
    size_t index;

    for (index = 0; index < SBC_DISK_DRIVE_COUNT; ++index)
    {
        if (context->drives[index] != NULL)
            fclose(context->drives[index]);
        context->drives[index] = NULL;
    }
}

static int load_environment(sbc_disk_context_t *context, const char *path,
                            char *error, size_t error_size)
{
    static const char *names[SBC_DISK_DRIVE_COUNT] = {
        "DRIVE_A", "DRIVE_B", "DRIVE_C", "DRIVE_D"
    };
    char line[4096];
    FILE *environment;

    if (path == NULL || path[0] == '\0')
    {
        set_error(error, error_size, "an environment file is required");
        return 0;
    }
    environment = fopen(path, "r");
    if (environment == NULL)
    {
        set_error(error, error_size, "cannot open environment file");
        return 0;
    }
    while (fgets(line, sizeof(line), environment) != NULL)
    {
        char *equals = strchr(line, '=');
        size_t index;

        if (equals == NULL)
            continue;
        *equals++ = '\0';
        equals[strcspn(equals, "\r\n")] = '\0';
        for (index = 0; index < SBC_DISK_DRIVE_COUNT; ++index)
        {
            if (strcmp(line, names[index]) == 0)
            {
                context->drives[index] = fopen(equals, "r+b");
                if (context->drives[index] == NULL)
                {
                    fclose(environment);
                    set_error(error, error_size, "cannot open configured drive");
                    return 0;
                }
                break;
            }
        }
    }
    fclose(environment);

    for (size_t index = 0; index < SBC_DISK_DRIVE_COUNT; ++index)
    {
        long size;

        if (context->drives[index] == NULL ||
            fseek(context->drives[index], 0, SEEK_END) != 0 ||
            (size = ftell(context->drives[index])) != (long)DISK_BYTES ||
            fseek(context->drives[index], 0, SEEK_SET) != 0)
        {
            set_error(error, error_size,
                      "each drive must be a configured 327680-byte image");
            return 0;
        }
    }
    return 1;
}

static int seek_record(sbc_disk_context_t *context)
{
    long offset = (long)context->lba * RECORD_BYTES;

    if (context->drive >= SBC_DISK_DRIVE_COUNT ||
        offset >= (long)DISK_BYTES)
        return 0;
    return fseek(context->drives[context->drive], offset, SEEK_SET) == 0;
}

static void disk_command(sbc_disk_context_t *context, uint8_t command)
{
    context->record_index = 0;
    if (command == DISK_READ)
    {
        if (!seek_record(context) ||
            fread(context->record, 1, RECORD_BYTES,
                  context->drives[context->drive]) != RECORD_BYTES)
            context->status = DISK_ERROR;
        else
            context->status = DISK_READY | DISK_DATA_READY;
    }
    else if (command >= 2 && command <= 4)
    {
        context->status = seek_record(context)
            ? DISK_READY | DISK_DATA_ROOM
            : DISK_ERROR;
    }
    else if (command == DISK_FLUSH)
    {
        size_t index;

        context->status = DISK_READY;
        for (index = 0; index < SBC_DISK_DRIVE_COUNT; ++index)
            if (fflush(context->drives[index]) != 0)
                context->status = DISK_ERROR;
    }
    else
    {
        context->status = DISK_ERROR;
    }
}

int sbc_disk_init(sbc_disk_context_t *context, const char *environment_file,
                  char *error, size_t error_size)
{
    memset(context, 0, sizeof(*context));
    context->status = DISK_READY;
    if (!load_environment(context, environment_file, error, error_size))
    {
        close_drives(context);
        return 0;
    }
    return 1;
}

void sbc_disk_close(sbc_disk_context_t *context)
{
    close_drives(context);
    memset(context, 0, sizeof(*context));
}

int sbc_disk_handles_port(uint8_t port)
{
    return port >= DISK_COMMAND && port <= ADAPTER_ACTIVATE;
}

uint8_t sbc_disk_input(sbc_disk_context_t *context, uint8_t port)
{
    switch (port)
    {
        case DISK_COMMAND: return context->status;
        case DISK_DRIVE: return context->drive;
        case DISK_LBA_LOW: return (uint8_t)context->lba;
        case DISK_LBA_HIGH: return (uint8_t)(context->lba >> 8);
        case DISK_DATA:
            if ((context->status & DISK_DATA_READY) == 0 ||
                context->record_index >= RECORD_BYTES)
                return 0;
            {
                uint8_t value = context->record[context->record_index++];
                if (context->record_index == RECORD_BYTES)
                    context->status = DISK_READY;
                return value;
            }
        default: return 0;
    }
}

void sbc_disk_output(sbc_disk_context_t *context, uint8_t port, uint8_t data)
{
    switch (port)
    {
        case DISK_COMMAND:
            disk_command(context, data);
            break;
        case DISK_DRIVE:
            context->drive = data;
            break;
        case DISK_LBA_LOW:
            context->lba = (uint16_t)((context->lba & 0xff00u) | data);
            break;
        case DISK_LBA_HIGH:
            context->lba = (uint16_t)((context->lba & 0x00ffu) |
                                      ((uint16_t)data << 8));
            break;
        case DISK_DATA:
            if ((context->status & DISK_DATA_ROOM) != 0 &&
                context->record_index < RECORD_BYTES)
            {
                context->record[context->record_index++] = data;
                if (context->record_index == RECORD_BYTES)
                {
                    if (fwrite(context->record, 1, RECORD_BYTES,
                               context->drives[context->drive]) != RECORD_BYTES)
                        context->status = DISK_ERROR;
                    else
                        context->status = DISK_READY;
                }
            }
            break;
        case ADAPTER_ACTIVATE:
            context->active = data == 0xa5;
            break;
        default:
            break;
    }
}
