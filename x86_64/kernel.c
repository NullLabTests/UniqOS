#include "kernel.h"
#include "multiboot2.h"
#include "serial.h"
#include "vga.h"
#include "desktop.h"
#include "virtio_net.h"
#include "net.h"
#include "tcp.h"
#include "http.h"
#include "browser.h"

extern char __bss_start[];
extern char __bss_end[];

static char shell_buf[256];
static int shell_pos = 0;

static void run_command(const char *cmd) {
    if (strcmp(cmd, "help") == 0) {
        vga_printf("Commands: help, uptime, clear, info, reboot\n");
        kprintf("Commands: help, uptime, clear, info, reboot\n");
    } else if (strcmp(cmd, "uptime") == 0) {
        uint64_t ms = timer_get_ms();
        kprintf("uptime: %llu ms\n", ms);
        vga_printf("Uptime: %llu ms\n", ms);
    } else if (strcmp(cmd, "clear") == 0) {
        vga_init();
    } else if (strcmp(cmd, "info") == 0) {
        uint64_t free = pmm_get_free_count() * 4 / 1024;
        vga_printf("UniqOS %s | %d MB free\n", UNIQOS_VERSION, (int)free);
        kprintf("UniqOS %s | %d MB free\n", UNIQOS_VERSION, (int)free);
    } else if (strcmp(cmd, "reboot") == 0) {
        vga_printf("Rebooting...\n");
        while (inb(0x64) & 0x02);
        outb(0x64, 0xFE);
        for(;;) asm("hlt");
    } else if (cmd[0]) {
        vga_printf("Unknown: '%s'\n", cmd);
    }
}

static void shell_handle_char(char c) {
    if (c == '\n') {
        vga_putchar('\n');
        kprintf("[shell] cmd: ");
        shell_buf[shell_pos] = 0;
        kprintf("%s\n", shell_buf);
        run_command(shell_buf);
        shell_pos = 0;
        vga_printf("$ ");
        kprintf("$ ");
        return;
    }
    if (c == '\b') {
        if (shell_pos > 0) {
            shell_pos--;
            vga_putchar('\b');
            vga_putchar(' ');
            vga_putchar('\b');
            kprintf("\b \b");
        }
        return;
    }
    if (c >= ' ' && shell_pos < 255) {
        shell_buf[shell_pos++] = c;
        vga_putchar(c);
        kprintf("%c", c);
    }
}

void kernel_main(uint32_t magic, multiboot2_info_t *mb_info) {
    for (volatile char *b = __bss_start; b < __bss_end; b++)
        *b = 0;

    serial_init();
    kprintf("UniqOS booting...\n");

    kprintf("UniqOS kernel %s (x86_64, bare metal)\n", UNIQOS_VERSION);
    kprintf("[boot] exception level long mode\n");

    uint64_t mem_upper = 0;

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC)
        kprintf("[boot] WARNING: not booted by Multiboot2\n");

    if (mb_info) {
        multiboot2_tag_t *tag = (multiboot2_tag_t *)((uintptr_t)mb_info + 8);
        while (tag->type != 0) {
            if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
                multiboot2_tag_mmap_t *mmap = (multiboot2_tag_mmap_t *)tag;
                multiboot2_mmap_entry_t *ent = mmap->entries;
                void *end = (void *)tag + tag->size;
                while ((void *)ent < end) {
                    if (ent->type == MULTIBOOT2_MEMORY_AVAILABLE && ent->addr == 0x100000)
                        mem_upper = ent->addr + ent->len;
                    ent = (multiboot2_mmap_entry_t *)((uintptr_t)ent + mmap->entry_size);
                }
            }
            tag = (multiboot2_tag_t *)((uintptr_t)tag + ((tag->size + 7) & ~7));
        }
    }

    if (mem_upper == 0) mem_upper = 128 * 1024 * 1024;
    kprintf("[boot] heap %llu MB\n", mem_upper / (1024 * 1024));

    pmm_init(mem_upper);
    vmm_init();
    heap_init();
    kprintf("[heap] self-test ok\n");

    gdt_init();
    idt_init();
    pic_init();
    pit_init(100);
    kprintf("[pit] timer at 100 Hz\n");

    keyboard_init();
    kprintf("[kbd] PS/2 keyboard initialized\n");

    display_init();
    kprintf("[display] initialization complete\n");

    int nic_ok = (virtio_init() == 0);
    if (nic_ok) {
        net_init();
        virtio_set_rx_callback(net_rx_dispatch);
    }

    if (display_is_fb()) {
        desktop_init();
        kprintf("[desktop] GUI environment ready\n");
    } else {
        vga_init();
        kprintf("[display] VGA text mode 80x25\n");
    }

    syscall_init();
    scheduler_init();

    asm("sti");

    if (display_is_fb()) {
        kprintf("[desktop] entering GUI main loop\n");
        desktop_run();
    } else {
        vga_printf("Welcome to UniqOS!\n");
        vga_printf("Type 'help' for commands.\n");
        vga_printf("$ ");

        for (;;) {
            int c = keyboard_getc();
            if (c > 0) shell_handle_char((char)c);
            asm("hlt");
        }
    }
}
