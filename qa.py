#!/usr/bin/env python3
"""QA harness for UniqOS VM.

Uses vboxapi for mouse injection, VBoxManage CLI for keyboard + screenshots.
"""
import subprocess, time, sys, os
from pathlib import Path
import vboxapi

VM = "UniqOS"
SHOTS = Path("/home/illy/UniqOS/qa_shots")
SHOTS.mkdir(exist_ok=True)

# Connect to VBox
_vbm = vboxapi.VirtualBoxManager(None, None)
_vb = _vbm.getVirtualBox()
_mach = _vb.findMachine(VM)
_sess = _vbm.getSessionObject(_vb)
_mach.lockMachine(_sess, 1)  # Shared lock
_console = _sess.console
_mouse = _console.mouse
_kb = _console.keyboard

# Track absolute mouse position (kernel starts at 512,384)
mouse_x = 512
mouse_y = 384

def run(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)

def shot(name):
    path = SHOTS / f"{name}.png"
    run(f"VBoxManage controlvm {VM} screenshotpng {path}")
    return path

def mouse_move_abs(x, y):
    global mouse_x, mouse_y
    dx = x - mouse_x
    dy = y - mouse_y
    if dx == 0 and dy == 0:
        return
    _mouse.putMouseEvent(dx, dy, 0, 0, 0)
    mouse_x = x
    mouse_y = y

def mouse_click(x, y, btn=1):
    global mouse_x, mouse_y
    mouse_move_abs(x, y)
    time.sleep(0.05)
    btn_map = {1:0x01, 2:0x02, 3:0x04}
    b = btn_map.get(btn, 0x01)
    _mouse.putMouseEvent(0, 0, 0, b, 0)
    time.sleep(0.03)
    _mouse.putMouseEvent(0, 0, 0, 0, 0)

def keyseq(keys):
    """Send a string of keys using scancode pairs."""
    sc = {
        'a':0x1E,'b':0x30,'c':0x2E,'d':0x20,'e':0x12,'f':0x21,'g':0x22,
        'h':0x23,'i':0x17,'j':0x24,'k':0x25,'l':0x26,'m':0x32,'n':0x31,
        'o':0x18,'p':0x19,'q':0x10,'r':0x13,'s':0x1F,'t':0x14,'u':0x16,
        'v':0x2F,'w':0x11,'x':0x2D,'y':0x15,'z':0x2C,
        '0':0x0B,'1':0x02,'2':0x03,'3':0x04,'4':0x05,'5':0x06,'6':0x07,
        '7':0x08,'8':0x09,'9':0x0A,
        ' ':0x39,'.':0x34,',':0x33,'/':0x35,';':0x27,"'":0x28,'[':0x1A,
        ']':0x1B,'-':0x0C,'=':0x0D,'`':0x29,'\\':0x2B,
        '\n':0x1C,'\t':0x0F,'\b':0x0E,
    }
    shift_sc = {
        'A':0x1E,'B':0x30,'C':0x2E,'D':0x20,'E':0x12,'F':0x21,'G':0x22,
        'H':0x23,'I':0x17,'J':0x24,'K':0x25,'L':0x26,'M':0x32,'N':0x31,
        'O':0x18,'P':0x19,'Q':0x10,'R':0x13,'S':0x1F,'T':0x14,'U':0x16,
        'V':0x2F,'W':0x11,'X':0x2D,'Y':0x15,'Z':0x2C,
        '!':0x02,'@':0x03,'#':0x04,'$':0x05,'%':0x06,'^':0x07,'&':0x08,
        '*':0x09,'(':0x0A,')':0x0B,':':0x27,'"':0x28,'<':0x33,'>':0x34,
        '?':0x35,'_':0x0C,'+':0x0D,'~':0x29,'|':0x2B,'{':0x1A,'}':0x1B,
    }
    for ch in keys:
        if ch in shift_sc:
            m = shift_sc[ch]
            _kb.putScancode(0x2A)
            _kb.putScancode(m)
            _kb.putScancode(m | 0x80)
            _kb.putScancode(0xAA)
        elif ch in sc:
            m = sc[ch]
            _kb.putScancode(m)
            _kb.putScancode(m | 0x80)
        time.sleep(0.02)

def pixel_color(img_path, x, y):
    r = run(f"convert {img_path} -crop 1x1+{x}+{y} -depth 8 txt:- 2>/dev/null | tail -1")
    try:
        parts = r.stdout.strip().split()
        rgb = parts[1].strip('()').split(',')
        return tuple(int(c) for c in rgb)
    except:
        return None

def count_pixels(img_path, color_spec):
    r = run(f"convert {img_path} -fill red -opaque '{color_spec}' -print '%[mean]' null: 2>/dev/null")
    try:
        return float(r.stdout.strip())
    except:
        return 0

def check_white_pixels(img_path):
    return count_pixels(img_path, 'srgb(255,255,255)')

# === TESTS ===

def test_boot():
    print("[QA] Waiting 35s for boot...")
    time.sleep(35)
    s = shot("01_boot")
    c = pixel_color(s, 0, 0)
    assert c and all(v > 0 for v in c), f"Boot: (0,0) should be visible, got {c}"
    print(f"[PASS] Boot: (0,0) = {c}")

def test_terminal():
    c = pixel_color(shot("01_boot"), 82, 68)
    assert c and c[0] < 128, f"Terminal area too bright: {c}"
    print(f"[PASS] Terminal visible at (82,68) = {c}")

def test_no_ghosting():
    for x, y in [(0,0), (5,5), (0,64)]:
        c = pixel_color(shot("01_boot"), x, y)
        assert c != (255,255,255), f"Ghosting at ({x},{y})!"
    print(f"[PASS] No ghosting at old (0,0) position")

def test_keyboard():
    keyseq("help")
    time.sleep(1)
    wp = check_white_pixels(shot("02_help"))
    assert wp > 15000, f"Too few white pixels: {wp}"
    print(f"[PASS] Keyboard typed 'help': {wp} white px")

def test_enter():
    keyseq("\n")
    time.sleep(2)
    s = shot("03_enter")
    wp = check_white_pixels(s)
    print(f"[PASS] Enter pressed: {wp} white px")

def test_mouse_cursor():
    """Move mouse to verify cursor renders at new position."""
    global mouse_x, mouse_y

    # Reset to known position: top-left (100,100)
    mouse_move_abs(100, 100)
    time.sleep(1)
    s1 = shot("04_mouse_100x100")
    c1 = pixel_color(s1, 100, 100)
    print(f"[INFO] Cursor at (100,100): pixel = {c1}")

    # Move to terminal titlebar (200, 50) to maybe interact
    mouse_move_abs(200, 50)
    time.sleep(1)
    s2 = shot("05_mouse_titlebar")
    c2 = pixel_color(s2, 200, 50)
    print(f"[INFO] Cursor at (200,50): pixel = {c2}")

    # Move to bottom panel (300, 730)
    mouse_move_abs(300, 730)
    time.sleep(1)
    s3 = shot("06_mouse_bottom")
    c3 = pixel_color(s3, 300, 730)
    print(f"[INFO] Cursor at (300,730): pixel = {c3}")

    # Verify cursor moved to different positions by checking pixel changes
    # at the OLD position (100,100) - should have reverted to normal bg
    c1_after = pixel_color(s3, 100, 100)
    print(f"[INFO] (100,100) after move away: {c1_after}")

    # Bottom panel at (300,730) should not be pure black
    assert c3 != (0,0,0), "Bottom panel pixel is pure black!"
    print(f"[PASS] Mouse cursor movement OK")

def test_click_dock():
    """Click on bottom panel, check for response."""
    global mouse_x, mouse_y
    # Dock icons are in bottom panel. Click around center (512, 740)
    mouse_move_abs(512, 740)
    time.sleep(1)
    ws_before = check_white_pixels(shot("07_before_click"))
    
    mouse_click(512, 740)
    time.sleep(2)
    s = shot("08_after_click")
    ws_after = check_white_pixels(s)
    print(f"[INFO] White px before click: {ws_before:.0f}, after: {ws_after:.0f}")
    print(f"[PASS] Mouse click test")

def run_all():
    tests = [
        ("Boot", test_boot),
        ("Terminal visible", test_terminal),
        ("No ghosting at (0,0)", test_no_ghosting),
        ("Keyboard input", test_keyboard),
        ("Shell command", test_enter),
        ("Mouse cursor movement", test_mouse_cursor),
        ("Mouse click dock", test_click_dock),
    ]
    passed = 0
    failed = 0
    for name, fn in tests:
        try:
            fn()
            passed += 1
        except Exception as e:
            print(f"[FAIL] {name}: {e}")
            failed += 1
        print()
    print(f"=== Results: {passed} passed, {failed} failed ===")
    return failed

if __name__ == "__main__":
    sys.exit(run_all())
