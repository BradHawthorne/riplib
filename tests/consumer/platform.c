#include "riplib_platform.h"
static uint16_t palette[256];
static int sent;
void palette_write_rgb565(uint8_t index, uint16_t color) { palette[index]=color; }
uint16_t palette_read_rgb565(uint8_t index) { return palette[index]; }
void riplib_host_tx(const char *data, int len) {
    if (len==6 && memcmp(data,"redraw",6)==0) ++sent;
}
int consumer_sent(void) { return sent; }
