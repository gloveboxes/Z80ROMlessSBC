#pragma once
#include <cstdint>
#include "pico/types.h"
struct dma_channel_config { bool read_increment = true, write_increment = false; uint dreq = 0; };
constexpr uint DMA_SIZE_32 = 2;
int dma_claim_unused_channel(bool);
void dma_channel_abort(uint);
void dma_channel_unclaim(uint);
bool dma_channel_is_busy(uint);
inline dma_channel_config dma_channel_get_default_config(uint) { return {}; }
inline void channel_config_set_transfer_data_size(dma_channel_config *, uint size) { if (size != DMA_SIZE_32) __builtin_trap(); }
inline void channel_config_set_read_increment(dma_channel_config *c, bool b) { c->read_increment = b; }
inline void channel_config_set_write_increment(dma_channel_config *c, bool b) { c->write_increment = b; }
inline void channel_config_set_dreq(dma_channel_config *c, uint d) { c->dreq = d; }
void dma_channel_configure(uint, const dma_channel_config *, volatile void *, const volatile void *, uint, bool);
