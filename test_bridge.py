"""Verify actual uinput events while grabbing only the test virtual keyboard."""
import ctypes as c
import errno
import fcntl
import os
from pathlib import Path
import select
import struct
import sys
import time

x = c.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
original_uid = os.geteuid()
if original_uid == 0:
    os.seteuid(int(os.environ['PKEXEC_UID']))
display = x.XOpenDisplay(sys.argv[2].encode())
if original_uid == 0:
    os.seteuid(original_uid)
assert display, 'Cannot open the session XWayland display'
bridge = c.CDLL(sys.argv[1])
assert bridge.ugee_bridge_init() == 0
bridge.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]

deadline = time.monotonic() + 5
node = None
while time.monotonic() < deadline:
    for event in Path('/sys/class/input').glob('event*'):
        if (event/'device/name').read_text().strip() == 'UGEE Wayland Shortcut Keyboard':
            candidate = Path('/dev/input') / event.name
            if candidate.exists():
                node = candidate
                break
    if node:
        break
    time.sleep(.05)
assert node, 'Virtual keyboard was not discovered'
fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, 0x40044590, 1)  # EVIOCGRAB: prevent test shortcuts reaching applications.
record = struct.Struct('llHHi')

def read_keys():
    result = []
    while select.select([fd], [], [], .05)[0]:
        try:
            data = os.read(fd, record.size * 64)
        except OSError as error:
            if error.errno == errno.ENODEV:
                break
            raise
        if not data:
            break
        for offset in range(0, len(data), record.size):
            _, _, kind, code, value = record.unpack_from(data, offset)
            if kind == 1:
                result.append((code, value))
    return result

def verify(label, calls, expected):
    for key, pressed in calls:
        assert bridge.XTestFakeKeyEvent(display, key + 8, pressed, 0) == 1
    actual = read_keys()
    assert actual == expected, (label, actual, expected)
    print('PASS:', label, actual, flush=True)

try:
    verify('Ctrl+Z', [(29,1),(44,1),(29,0),(44,0)], [(29,1),(44,1),(29,0),(44,0)])
    verify('Ctrl+S', [(29,1),(31,1),(31,0),(29,0)], [(29,1),(31,1),(31,0),(29,0)])
    verify('Alt press/release', [(56,1),(56,0)], [(56,1),(56,0)])
    verify('repeat and redundant release', [(57,1),(57,1),(57,0),(57,0)], [(57,1),(57,2),(57,0)])
    assert bridge.XTestFakeKeyEvent(display, 8, 1, 0) == 0
    assert read_keys() == []
    print('PASS: invalid keycode emits nothing', flush=True)
    assert bridge.XTestFakeKeyEvent(display, 37, 1, 0) == 1
    read_keys()
    # Device destruction revokes evdev readers, so verify the same cleanup release
    # routine before destroying the device, then check that destruction completes.
    bridge.ugee_bridge_release_keys()
    assert read_keys() == [(29, 0)]
    print('PASS: cleanup releases held Ctrl', flush=True)
    libdl = c.CDLL(None)
    libdl.dlclose.argtypes = [c.c_void_p]
    assert libdl.dlclose(bridge._handle) == 0
    assert read_keys() == []
    print('PASS: virtual keyboard destruction', flush=True)
finally:
    os.close(fd)
