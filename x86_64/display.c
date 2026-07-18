#include "display.h"
#include "kernel.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"
#include "font.h"

#define VBE_DISPI_IOPORT_INDEX 0x1CE
#define VBE_DISPI_IOPORT_DATA  0x1CF

#define VBE_DISPI_INDEX_ID        0
#define VBE_DISPI_INDEX_XRES      1
#define VBE_DISPI_INDEX_YRES      2
#define VBE_DISPI_INDEX_BPP       3
#define VBE_DISPI_INDEX_ENABLE    4
#define VBE_DISPI_INDEX_BANK      5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET  8
#define VBE_DISPI_INDEX_Y_OFFSET  9

#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40
#define VBE_DISPI_NOCLEARMEM   0x80

#define VBE_LFB_PHYS_ADDR 0xE0000000

static int fb_width = 0;
static int fb_height = 0;
static int fb_bpp = 0;
static int fb_pitch = 0;
static uint32_t *fb_addr = 0;
static uint32_t *back_buffer = 0;
static int fb_initialized = 0;

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int vbe_init(int width, int height, int bpp) {
    uint16_t id = vbe_read(VBE_DISPI_INDEX_ID);
    kprintf("[display] VBE ID: 0x%x\n", id);

    if (id != 0xB0C0 && id != 0xB0C1 && id != 0xB0C2 && id != 0xB0C3 && id != 0xB0C4) {
        kprintf("[display] VBE not supported (ID=0x%x)\n", id);
        return -1;
    }

    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES, width);
    vbe_write(VBE_DISPI_INDEX_YRES, height);
    vbe_write(VBE_DISPI_INDEX_BPP, bpp);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    vbe_write(VBE_DISPI_INDEX_BANK, 0);

    uint16_t xres = vbe_read(VBE_DISPI_INDEX_XRES);
    uint16_t yres = vbe_read(VBE_DISPI_INDEX_YRES);
    uint16_t bpp_r = vbe_read(VBE_DISPI_INDEX_BPP);
    kprintf("[display] mode: %dx%dx%d\n", xres, yres, bpp_r);

    if (xres != width || yres != height) {
        kprintf("[display] WARNING: mode not exactly as requested\n");
    }

    return 0;
}

void display_init(void) {
    if (vbe_init(1024, 768, 32) == 0) {
        fb_width = 1024;
        fb_height = 768;
        // Verify actual bpp from VBE
        uint16_t actual_bpp = vbe_read(VBE_DISPI_INDEX_BPP);
        fb_bpp = actual_bpp ? actual_bpp : 32;
        if (fb_bpp != 32) {
            kprintf("[display] WARNING: VBE returned %d bpp, adjusting\n", fb_bpp);
        }
        fb_pitch = fb_width * (fb_bpp / 8);
        fb_addr = (uint32_t *)0x200000000;

        for (int i = 0; i < (fb_pitch * fb_height + 0xFFF) / 0x1000; i++)
            vmm_map_page((uint64_t)fb_addr + i * 0x1000, VBE_LFB_PHYS_ADDR + i * 0x1000, 3);

        back_buffer = (uint32_t *)kmalloc(fb_pitch * fb_height);
        if (!back_buffer) {
            kprintf("[display] WARNING: no memory for back buffer\n");
            return;
        }

        for (int i = 0; i < fb_width * fb_height; i++)
            back_buffer[i] = 0x001A1A2E;

        display_present();
        fb_initialized = 1;
        kprintf("[display] VBE framebuffer %dx%d initialized\n", fb_width, fb_height);
    } else {
        kprintf("[display] VBE not available, using VGA text mode\n");
    }
}

int display_is_fb(void) {
    return fb_initialized;
}

void display_put_pixel(int x, int y, uint32_t color) {
    if (!fb_initialized) return;
    if (x < 0 || x >= fb_width || y < 0 || y >= fb_height) return;
    back_buffer[y * fb_width + x] = color;
}

void display_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_initialized) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            back_buffer[(y + row) * fb_width + (x + col)] = color;
}

void display_present(void) {
    if (!fb_initialized || !back_buffer) return;
    memcpy((void*)fb_addr, back_buffer, fb_pitch * fb_height);
}

int display_get_width(void) { return fb_width; }
int display_get_height(void) { return fb_height; }
uint32_t *display_get_buffer(void) { return back_buffer; }

void display_put_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!fb_initialized) return;
    if (c < 0 || c > 127) return;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = font8x16_basic[(int)c][row];
        for (int col = 0; col < 8; col++) {
            int px = x + col, py = y + row;
            if (px < 0 || px >= fb_width || py < 0 || py >= fb_height) continue;
            back_buffer[py * fb_width + px] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}
