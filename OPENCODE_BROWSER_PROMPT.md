# opencode Task: Build a Styled-Text Browser for UniqOS (Bare Metal x86_64)

## Repo
`/home/illy/UniqOS/x86_64` — bare-metal x86_64 OS, GRUB/Multiboot2, VirtualBox target (PCnet NIC, VESA framebuffer)

## Current State (Broken)
| Component | Status | Evidence |
|-----------|--------|----------|
| Desktop rendering | ❌ Regressed | Screenshot: 63 colors (was 7,517) — solid blocks only |
| Terminal input | ❌ Broken | Keystrokes not reaching shell |
| Font rendering | ❌ Unreadable | 8×16 font garbled or missing |
| Browser | ❌ Skeleton | Strips `<tags>`, renders raw text, no layout, no links |
| Editor | ❌ Skeleton | Opens empty window |
| File Manager | ❌ Skeleton | Lists only, no actions |
| Sysmon | ❌ Skeleton | Static bars |
| Network (TCP) | ❌ Data ACKs lost | SYN/GET ACKs work; server retransmits data |

---

## Phase 0: Diagnostics (Run First — 10 min)

### 0.1 Capture Network Traffic
```bash
VBoxManage modifyvm "UniqOS" --nictrace1 on --nictracefile1 /tmp/net.pcap
# Boot OS, open browser, click fetch, wait 30s, poweroff
tshark -r /tmp/net.pcap -Y "tcp.port==80" -V
```
**Save pcap** — needed for Phase 3.

### 0.2 Add Render Tracing
Edit `desktop.c` in `desktop_run()`:
```c
kprintf("[dbg] frame: redraw=%d mouse=(%d,%d) btn=%d\n", redraw, mouse_x, mouse_y, mouse_buttons);
```
Verify each `draw_*` function executes per frame.

### 0.3 Verify Framebuffer
In `desktop_init()`:
```c
kprintf("[dbg] fb_buf=%p %dx%d\n", fb_buf, fb_w, fb_h);
```
Confirm valid pointer + 1024×768.

### 0.4 Test Font
In `display_init()` or `fbterm_init()`:
```c
for (int i=0;i<16;i++) for(int j=0;j<16;j++)
    display_put_char(j*8, i*16, 'A'+(i*16+j)%26, 0xFFFFFF, 0);
display_present();
```
Boot — should see 256-char grid.

---

## Phase 1: Desktop Rendering Regression (Critical)

**Goal**: Restore full GUI matching `../desktop.png` (gradient bg, 6 icons, top bar with Activities/launchers/window tabs/clock/power, window borders, terminal content, mouse cursor).

### 1.1 Verify Draw Pipeline Order
In `desktop_run()` render block — **exact sequence**:
```c
draw_background();
draw_desktop_icons();
window_render_all();   // window contents + borders + titlebars
draw_topbar();
draw_context_menu();
draw_mouse();
display_present();
```

### 1.2 Fix `window_render_all()`
Check `window.c`:
- Loop uses `z_order[]` — verify `window_count > 0`
- `window_get(id)` must return visible, non-minimized windows
- Each window: fill bg → `win->draw(win)` callback → titlebar → border
- Terminal window has `win->draw = term_draw` → calls `fbterm_render()`

### 1.3 Fix `display_present()`
In `display.c` — VESA linear framebuffer may need explicit flush:
```c
void display_present(void) {
    asm volatile("clflushopt (%0)" :: "r"(fb_buf) : "memory");
    // or if banked: switch banks per scanline
}
```

### 1.4 Fix `redraw` Logic
In `desktop_run()`:
```c
int redraw = 1;  // force first frame
// ...
if (redraw) {
    // all draw calls
    redraw = 0;
}
// Set redraw=1 on: mouse move, key press, window change, timer tick (clock)
```

### 1.5 Validation
- Screenshot: `VBoxManage controlvm "UniqOS" screenshotpng /tmp/test.png`
- Compare: `compare -metric AE /tmp/test.png ../desktop.png null:` → diff < 5%

---

## Phase 2: Terminal Input + Font (Critical)

### 2.1 Fix Keyboard → Shell Path
Trace: `keyboard.c` → `keyboard_getc()` → `desktop_run()` key handling → `window_handle_key()` → `win->on_key(win, c)` → `term_key()` → `shell_handle_char()`

**Likely bugs**:
- `focused_id` not set → `window_get(focused_id)` returns NULL
- `term_key` not registered: `window_set_onkey(shell_win_id, term_key)`
- `shell_handle_char` not processing keys

### 2.2 Fix Font Rendering
In `display.c` / `fbterm.c`:
```c
void display_put_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x16_basic[(unsigned char)c];
    for (int row=0; row<16; row++) {
        uint8_t bits = glyph[row];
        for (int col=0; col<8; col++)
            fb[(y+row)*fb_w + (x+col)] = (bits & (0x80>>col)) ? fg : bg;
    }
}
```
Verify `font8x16_basic[128][16]` is populated in `font.c`.

### 2.3 Add Bold/Italic Synthesis (for later CSS)
```c
void display_put_char_styled(int x, int y, char c, uint32_t fg, uint32_t bg, int bold, int italic) {
    const uint8_t *glyph = font8x16_basic[(unsigned char)c];
    for (int row=0; row<16; row++) {
        uint8_t bits = glyph[row];
        if (bold) bits |= bits >> 1;
        if (italic) bits = (bits << 1) | (bits >> 7);
        for (int col=0; col<8; col++)
            fb[(y+row)*fb_w + (x+col)] = (bits & (0x80>>col)) ? fg : bg;
    }
}
```

### 2.4 Validation
- Boot, open terminal, type `help` → executes, shows prompt
- Type `echo hello world` → renders correctly
- Font grid test (Phase 0.4) shows all 256 chars

---

## Phase 3: TCP ACK Transmission Fix (Critical)

**Use pcap from Phase 0.1**.

### 3.1 Analyze PCAP
```bash
tshark -r /tmp/net.pcap -Y "tcp.port==80" -T fields -e frame.number -e tcp.srcport -e tcp.dstport -e tcp.flags -e tcp.seq -e tcp.ack -e tcp.len -e ip.src -e ip.dst
```
Look for:
- Our ACK packets leaving VM? (src=10.0.2.15, flags=ACK)
- Correct `ack` value? (should be server's `seq + len`)
- Checksum valid?

### 3.2 Fix `virtio_net.c` TX Path
In `virtio_send()`:
```c
// 1. Fill TX descriptor
tx_ring[t].base = (uint32_t)(uintptr_t)tx_buf;
tx_ring[t].flags = (1<<31) | (1<<25) | (1<<24) | (0xF<<12) | ((4096 - len) & 0xFFF);
// OWN=1, SOP=1, EOP=1, BUF_LEN=4096-len

// 2. FLUSH descriptor to RAM so VMM sees it
asm volatile("clflush (%0)" :: "r"(&tx_ring[t].flags) : "memory");
asm volatile("mfence" ::: "memory");

// 3. Kick TX (TDMD + STRT + INEA)
outw(iobase + RDP_OFF, 0x004A);

// 4. WAIT for VMM to clear OWN bit (optional but helps)
for (int i=0; i<10000; i++) {
    if (!(tx_ring[t].flags & (1<<31))) break;
    asm volatile("pause");
}
```

### 3.3 Remove Nested `virtio_poll_all()` from `virtio_send()`
```c
// REMOVE THIS LINE:
virtio_poll_all();  // was causing RX/TX descriptor collision
```

### 3.4 Validation
- Boot, open browser, fetch `http://example.com/`
- PCAP shows: SYN → SYN-ACK → ACK → GET → ACK(data) → HTTP 200 body
- Browser receives body, `browser_fetching = 0`

---

## Phase 4: Browser — Styled Text + Links + Image Placeholders

### 4.1 Architecture
```
browser.c (orchestrator)
├── html_parse.c/h      // NEW: tiny DOM tree
├── css_minimal.c/h     // NEW: computed styles (bold, italic, color, block/inline)
├── layout.c/h          // NEW: flow layout → positioned boxes
├── render.c/h          // NEW: draw boxes using display_* primitives
├── link_map.c/h        // NEW: clickable rects → URL mapping
└── font.c/h            // EXTEND: bold/italic synthesis
```

### 4.2 Minimal DOM (`html_parse.h`)
```c
typedef enum { NODE_ELEMENT, NODE_TEXT } node_type_t;

typedef struct html_node {
    node_type_t type;
    char *tag;                    // "div", "a", "img", NULL for text
    char *text;                   // for text nodes
    struct html_attr { char *name, *value; } *attrs;
    int attr_count;
    struct html_node **children;
    int child_count;
    struct html_node *parent;
} html_node_t;

html_node_t *html_parse(const char *html, int len);
void html_free(html_node_t *root);
```
**Algorithm**: Recursive descent, handles malformed HTML (like hubbub). Recognize: `b, strong, i, em, u, a[href], img[src,alt], p, br, h1-h6, ul, ol, li, div, span, table, tr, td, th`.

### 4.3 Minimal CSS (`css_minimal.h`)
```c
typedef struct {
    int bold;
    int italic;
    int underline;
    uint32_t color;
    uint32_t bg_color;
    int margin_top, margin_bottom;
    int margin_left, margin_right;
    int display_block;  // 1 = block (newline before/after), 0 = inline
} computed_style_t;

computed_style_t css_compute(html_node_t *node, computed_style_t *parent_style);
```
**Hardcoded defaults**:
- `b, strong` → bold
- `i, em` → italic
- `u` → underline
- `a` → color=0x0000FF, underline=1
- `h1` → bold, margin_top=16, margin_bottom=8, display_block=1
- `p, div` → display_block=1, margin_top=8, margin_bottom=8
- `img` → display_block=0 (inline-block)
- `br` → forced line break in layout

### 4.4 Layout (`layout.h`)
```c
typedef struct {
    int x, y, w, h;
    html_node_t *node;
    computed_style_t style;
    char *text;           // for text nodes: positioned glyphs
    int link_url_idx;     // -1 if not a link
} layout_box_t;

int layout_document(html_node_t *root, int viewport_w, layout_box_t **boxes, int *box_count);
```
**Flow layout algorithm**:
- Cursor `(cx, cy)` starts at `(margin_left, margin_top)`
- For each node in pre-order:
  - `style = css_compute(node, parent_style)`
  - If `display_block`: `cx = margin_left; cy += margin_top;`
  - If text node: measure glyphs (8px each), wrap at `viewport_w - margin_right`, create line boxes
  - If inline element: continue on same line
  - If block element: `cy += margin_bottom` after children
  - `img`: reserve `width`×`height` (attrs or 16×16 default)
- Output: `layout_box_t[]` with absolute positions

### 4.5 Render (`render.h`)
```c
void render_boxes(layout_box_t *boxes, int count, int win_x, int win_y, int win_w, int win_h);
```
- Iterate boxes:
  - Background: `display_fill_rect(win_x+bx.x, win_y+bx.y, bx.w, bx.h, bx.style.bg_color)`
  - Text: `display_put_char_styled(win_x+bx.x, win_y+bx.y, c, bx.style.color, bx.style.bg_color, bx.style.bold, bx.style.italic)`
  - Underline: draw line at `y + 15`
  - Image placeholder: colored rect + alt text

### 4.6 Link Map (`link_map.h`)
```c
typedef struct { int x, y, w, h; char *url; } link_rect_t;
void link_map_build(layout_box_t *boxes, int count, link_rect_t **out, int *out_count);
int link_map_click(link_rect_t *map, int count, int mx, int my, char **out_url);
```

### 4.7 Integration in `browser.c`
```c
void browser_http_cb(const char *response, uint16_t len) {
    // 1. Parse HTML
    html_node_t *dom = html_parse(response, len);
    // 2. Compute styles + layout
    layout_box_t *boxes; int box_count;
    layout_document(dom, win_w - 20, &boxes, &box_count);
    // 3. Build link map
    link_map_build(boxes, box_count, &browser_links, &browser_link_count);
    // 4. Store for render
    browser_boxes = boxes; browser_box_count = box_count;
    browser_fetching = 0;
    html_free(dom);  // boxes own no DOM pointers
}

void browser_draw(window_t *win) {
    if (browser_fetching) { draw "Fetching..."; return; }
    render_boxes(browser_boxes, browser_box_count, win->x+8, win->y+32, win->w-16, win->h-40);
}

int browser_on_click(int mx, int my) {
    char *url;
    if (link_map_click(browser_links, browser_link_count, mx - win_x - 8, my - win_y - 32, &url)) {
        browser_fetch(url);
        return 1;
    }
    return 0;
}
```

### 4.8 Validation
- Fetch `http://example.com/` → renders bold/italic/links/paragraphs
- Click link (mouse or keyboard nav) → navigates
- Back button works (history stack)

---

## Phase 5: App Flesh-Out (Parallelizable)

### 5.1 Editor (`editor.c`)
- Gap buffer or rope for text
- Syntax highlight: keywords, strings, comments, numbers (C/JS/HTML)
- Keys: arrows, home/end, pgup/dn, backspace, del, enter, tab
- Ctrl+S save, Ctrl+O open, Ctrl+Q quit

### 5.2 File Manager (`fileman.c`)
- Double-click dir → `cd`, file → open in editor
- Toolbar: up, home, new file, new dir, delete, rename
- Right-click context menu

### 5.3 Sysmon (`sysmon.c`)
- CPU: idle% from scheduler tick count
- Mem: `pmm_get_free_count()` × 4KB
- Net: RX/TX bytes/sec (add counters in `virtio_net.c`)
- Processes: walk `task_pool[]` → PID, name, state, CPU%

---

## Phase 6: Code Quality (Required Before Merge)

### 6.1 Headers for All `.c` Files
Create `.h` for: `display.c`, `fbterm.c`, `fileman.c`, `editor.c`, `sysmon.c`, `timefmt.c`, `browser.c`, `http.c`, `tcp.c`, `ip.c`, `arp.c`, `net.c`, `pci.c`, `heap.c`, `pmm.c`, `vmm.c`, `gdt.c`, `idt.c`, `pic.c`, `pit.c`, `keyboard.c`, `mouse.c`, `serial.c`, `scheduler.c`, `syscall.c`, `vfs.c`, `window.c`, `shell.c`, `font.c`, `support.c`

### 6.2 Modular Makefile
```makefile
OBJ_KERNEL := kernel.o boot.o interrupts.o gdt.o idt.o pic.o pit.o pmm.o vmm.o heap.o scheduler.o syscall.o serial.o
OBJ_DRIVERS := keyboard.o mouse.o display.o fbterm.o pci.o virtio_net.o
OBJ_NET := net.o arp.o ip.o tcp.o http.o
OBJ_FS := vfs.o
OBJ_GUI := window.o desktop.o font.o
OBJ_APPS := shell.o browser.o editor.o fileman.o sysmon.o timefmt.o support.o

OBJ := $(OBJ_KERNEL) $(OBJ_DRIVERS) $(OBJ_NET) $(OBJ_FS) $(OBJ_GUI) $(OBJ_APPS)

debug: CFLAGS += -g -O0 -DDEBUG
debug: all

lint:
	clang-tidy -checks='*' *.c -- -target x86_64-none-elf -ffreestanding -nostdlib -I.
	cppcheck --enable=all --std=c11 --platform=unix64 *.c

format:
	clang-format -i *.c *.h
```

### 6.3 Assert/Panic Macro
In `kernel.h`:
```c
#define kassert(cond, fmt, ...) \
    do { if (!(cond)) { kprintf("ASSERT %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); for(;;) asm("hlt"); } } while(0)
```
Replace manual checks in `vfs.c`, `shell.c`, `window.c`, `pmm.c`, `vmm.c`.

### 6.4 Bounds Checking
All path copies → `strlcpy`/`snprintf`
All array indexes → validate against count

---

## Files to Create/Modify

```
x86_64/
├── html_parse.c/h      # NEW
├── css_minimal.c/h     # NEW
├── layout.c/h          # NEW
├── render.c/h          # NEW
├── link_map.c/h        # NEW
├── browser.c           # REFACTOR heavily
├── font.c/h            # ADD: bold/italic synthesis
├── display.c/h         # ADD: display_put_char_styled
├── window.c/h          # FIX: render pipeline, focus
├── desktop.c           # FIX: redraw logic, draw order
├── virtio_net.c        # FIX: TX flush, wait, no nested poll
├── keyboard.c          # FIX: key delivery to focused window
├── Makefile            # MODULAR objs, debug/lint/format targets
├── kernel.h            # ADD: kassert, strlcpy
└── nsport/             # (later, for NetSurf port)
```

---

## Execution Order

```
Phase 0 (diagnostics) → Phase 1 (desktop render) → Phase 2 (terminal+font)
                                                          ↓
Phase 3 (TCP ACK fix) ←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←
                                                          ↓
Phase 4 (browser styled text) → Phase 5 (apps) → Phase 6 (quality)
```

**Phases 1+2 can run in parallel with Phase 3** (different files).

---

## Start Command

```bash
cd /home/illy/UniqOS/x86_64
opencode < /home/illy/UniqOS/OPENCODE_BROWSER_PROMPT.md
```

---

## Validation Checklist (Copy-Paste to Track)

```
[ ] Phase 0.1: net.pcap captured, HTTP flow visible
[ ] Phase 0.2: render trace shows all draw_* calls/frame
[ ] Phase 0.3: fb_buf valid 1024x768
[ ] Phase 0.4: font grid renders 256 chars
[ ] Phase 1: screenshot matches desktop.png (<5% diff)
[ ] Phase 2.1: type "help" in terminal → executes
[ ] Phase 2.2: font readable, all chars correct
[ ] Phase 2.3: bold/italic synthesis works
[ ] Phase 3.1: pcap shows our ACK packets with correct ack#
[ ] Phase 3.2: virtio_send flushes + waits for OWN clear
[ ] Phase 3.3: no nested virtio_poll_all in virtio_send
[ ] Phase 3.4: browser fetches example.com → HTTP 200 body received
[ ] Phase 4.1: html_parse builds DOM for <b>bold <i>both</i></b>
[ ] Phase 4.2: css_compute returns correct styles
[ ] Phase 4.3: layout_document produces non-overlapping boxes
[ ] Phase 4.4: render_boxes draws styled text + link underlines
[ ] Phase 4.5: link_map_click navigates on click
[ ] Phase 4.6: back button works
[ ] Phase 5.1: editor edit/save/open works
[ ] Phase 5.2: fileman navigate/open/delete works
[ ] Phase 5.3: sysmon shows live CPU/mem/net/processes
[ ] Phase 6.1: every .c has .h, no implicit declarations
[ ] Phase 6.2: make debug / make lint / make format work
[ ] Phase 6.3: kassert used throughout
[ ] Phase 6.4: zero buffer overflows (cppcheck clean)
```

---

## Notes for opencode

- **Test after each phase**: `make clean iso` → boot → verify → serial log → screenshot
- **No stdlib**: only `kernel.h` APIs (`kmalloc/kfree`, `memcpy/strcmp/strlen`, `kprintf`, `timer_get_ms`, `inb/outb/inw/outw`)
- **Single-threaded**: no mutex, no async — everything in `desktop_run()` loop
- **Framebuffer**: `display_get_buffer()` → `uint32_t*`, 32bpp ARGB, 1024×768
- **Serial debug**: `kprintf` → COM1 (0x3F8) → captured in host terminal
- **Poweroff**: `outw(0xB004, 0x2000); outw(0x604, 0x2000);`
- **Reboot**: `outb(0x64, 0xFE);`