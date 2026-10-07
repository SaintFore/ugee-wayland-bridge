#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>

static const char *config_path(const char *path) {
    const char *override = getenv("UGEE_BRIDGE_CONFIG");
    const char *suffix = "/conf/Ugee_Tablet.xml";
    if (override && *override && path && strlen(path)>=strlen(suffix) &&
        !strcmp(path+strlen(path)-strlen(suffix),suffix)) {
        fprintf(stderr,"[ugee-bridge] using private configuration\n");
        return override;
    }
    return path;
}

/* Qt's QFile uses open64. Redirect only the vendor tablet configuration. */
int open64(const char *path, int flags, ...) {
    mode_t mode=0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE)==O_TMPFILE) {
        va_list args; va_start(args,flags); mode=va_arg(args,int); va_end(args);
    }
    int (*original)(const char *,int,...)=dlsym(RTLD_NEXT,"open64");
    if (!original) { errno=ENOSYS; return -1; }
    return original(config_path(path),flags,mode);
}

static int device = -1;
static unsigned char held[KEY_MAX + 1];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int emit(unsigned short type, unsigned short code, int value) {
    struct input_event event = {.type=type, .code=code, .value=value};
    ssize_t written;
    do { written = write(device, &event, sizeof(event)); } while (written < 0 && errno == EINTR);
    return written == sizeof(event) ? 0 : -1;
}

int ugee_bridge_init(void) {
    if (device >= 0) return 0;
    device = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (device < 0) goto fail;
    if (ioctl(device, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(device, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    for (int key=1; key<=247; key++) if (ioctl(device, UI_SET_KEYBIT, key) < 0) goto fail;
    if (ioctl(device, UI_SET_PHYS, "ugee/wayland-shortcuts") < 0) goto fail;
    struct uinput_setup setup = {.id={.bustype=BUS_VIRTUAL, .vendor=0x28bd, .product=0xf640, .version=1}};
    snprintf(setup.name, sizeof(setup.name), "UGEE Wayland Shortcut Keyboard");
    if (ioctl(device, UI_DEV_SETUP, &setup) < 0 || ioctl(device, UI_DEV_CREATE) < 0) goto fail;
    /* Allow udev and the compositor to discover the keyboard before the first chord. */
    usleep(500000);
    fprintf(stderr, "[ugee-bridge] virtual keyboard ready\n");
    return 0;
fail:
    fprintf(stderr, "[ugee-bridge] cannot create virtual keyboard: %s\n", strerror(errno));
    if (device >= 0) close(device);
    device = -1;
    return -1;
}

static int valid_keymap(Display *display) {
    /* This bridge targets the local XWayland evdev keycode map, not arbitrary X servers. */
    static const struct { KeySym symbol; int key; } checks[] = {
        {XK_Control_L, KEY_LEFTCTRL}, {XK_Alt_L, KEY_LEFTALT},
        {XK_Shift_L, KEY_LEFTSHIFT}, {XK_z, KEY_Z},
        {XK_space, KEY_SPACE}, {XK_Return, KEY_ENTER}
    };
    for (size_t i=0; i<sizeof(checks)/sizeof(checks[0]); i++)
        if (XKeysymToKeycode(display, checks[i].symbol) != checks[i].key + 8) return 0;
    return 1;
}

int XTestFakeKeyEvent(Display *display, unsigned int keycode, Bool press, unsigned long delay) {
    int result = 0;
    pthread_mutex_lock(&lock);
    if (!display || keycode <= 8 || keycode > 255 || !valid_keymap(display)) {
        fprintf(stderr, "[ugee-bridge] rejected unsupported X keycode/map (%u)\n", keycode);
        goto done;
    }
    if (ugee_bridge_init() < 0) goto done;
    if (delay && delay <= 5000) usleep(delay * 1000);
    unsigned int key = keycode - 8;
    if (!press && !held[key]) { result=1; goto done; }
    int value = press ? (held[key] ? 2 : 1) : 0;
    if (emit(EV_KEY, key, value) < 0 || emit(EV_SYN, SYN_REPORT, 0) < 0) {
        fprintf(stderr, "[ugee-bridge] event write failed: %s\n", strerror(errno));
        goto done;
    }
    held[key] = !!press;
    if (getenv("UGEE_BRIDGE_DEBUG")) fprintf(stderr, "[ugee-bridge] key=%u value=%d\n",key,value);
    result=1;
done:
    pthread_mutex_unlock(&lock);
    return result;
}

__attribute__((constructor)) static void startup(void) {
    char path[4096];
    ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
    if (n<0) return;
    path[n]=0;
    const char *name=strrchr(path,'/');
    if (name && !strcmp(name+1,"ugeeTabletDriver") && ugee_bridge_init()<0) _exit(126);
}
void ugee_bridge_release_keys(void) {
    pthread_mutex_lock(&lock);
    if (device >= 0) {
        for (int key=1; key<=KEY_MAX; key++) if (held[key]) {
            emit(EV_KEY,key,0);
            held[key]=0;
        }
        emit(EV_SYN,SYN_REPORT,0);
    }
    pthread_mutex_unlock(&lock);
}
__attribute__((destructor)) static void cleanup(void) {
    if (device<0) return;
    ugee_bridge_release_keys();
    ioctl(device,UI_DEV_DESTROY);
    close(device);
    device=-1;
}
