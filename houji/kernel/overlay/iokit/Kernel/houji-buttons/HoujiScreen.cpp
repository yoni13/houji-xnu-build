// SPDX-License-Identifier: BSD-2-Clause-Patent
// The panel's brightness as the screen's on/off switch (DCS 0x51 over DSI0 at 0x0ae94000 through the command FIFO, the protocol of
// lonzzi/UniXNU-houji's HoujiBacklightProtocol.h). This port cannot sleep the display, so an idle screen goes dark instead: after
// houji_screen_off seconds without a touch (default 120, 0 never) brightness 0; the next touch, or the power key, brings back the level
// (houji_screen_level, default 1600 of 2866). USB console: "screen off", "screen on", "screen LEVEL".
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "HoujiBacklightProtocol.h"

extern "C" uint64_t houji_dvfs_touch_time(void);
// [0] stage (1 running; negative: failed), [1] on, [2] level, [3] commands sent, [4] failed, [5] last change cause (1 idle, 2 touch,
// 3 power key, 4 console)
extern "C" {volatile int32_t native_screen_stats[6];}

namespace {
IOMemoryMap *gMap;
volatile uint8_t *gDsi;
uint32_t rd(void *, unsigned off) {return *reinterpret_cast<volatile uint32_t *>(gDsi + off);}
void wr(void *, unsigned off, uint32_t v) {*reinterpret_cast<volatile uint32_t *>(gDsi + off) = v; __asm__ volatile("dsb sy" ::: "memory");}
uint64_t nowUs() {uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000;}
HoujiDsiCommand gCmd;
unsigned gLevel = 1600;
bool gOn = true;
volatile uint32_t gRequest;   // 0 none, 1 off, 2 on, 3 toggle, 0x10000 | level: set level and on
volatile int32_t *const st = native_screen_stats;

bool send(unsigned level) {
    gCmd.failed = false;
    if (!gCmd.brightness(level, nowUs())) {st[4] = st[4] + 1; return false;}
    for (unsigned i = 0; i < 80; ++i) {
        const int r = gCmd.poll(nowUs());
        if (r) {if (r < 0) st[4] = st[4] + 1; else st[3] = st[3] + 1; return r > 0;}
        IOSleep(1);
    }
    return false;
}
void apply(bool on, int cause) {
    if (on == gOn) return;
    if (send(on ? gLevel : 0)) {
        gOn = on; st[1] = on; st[5] = cause;
        IOLog("HOUJI SCREEN: %s (%s)\n", on ? "on" : "off", cause == 1 ? "idle" : cause == 2 ? "touch" : cause == 3 ? "power key" : "console");
    }
}
void screenThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-screen");
    IODeviceMemory *m = IODeviceMemory::withRange(0x0ae94000ULL, 0x1000);
    gMap = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (m) m->release();
    if (!gMap) {st[0] = -1; return;}
    gDsi = reinterpret_cast<volatile uint8_t *>(gMap->getVirtualAddress());
    gCmd.io = {nullptr, rd, wr};
    unsigned idle = 120;
    PE_parse_boot_argn("houji_screen_off", &idle, sizeof idle);
    PE_parse_boot_argn("houji_screen_level", &gLevel, sizeof gLevel);
    if (gLevel < HBL_MIN || gLevel > HBL_MAX) gLevel = 1600;
    st[0] = 1; st[1] = 1; st[2] = (int32_t)gLevel;
    uint64_t lastTouch = houji_dvfs_touch_time(), startedAt = mach_absolute_time();
    for (;;) {
        IOSleep(250);
        const uint32_t req = __atomic_exchange_n(&gRequest, 0u, __ATOMIC_ACQ_REL);
        if (req & 0x10000) {
            gLevel = req & 0xffff; st[2] = (int32_t)gLevel;
            if (send(gLevel)) {gOn = true; st[1] = 1; st[5] = 4;}
            lastTouch = mach_absolute_time();
            continue;
        }
        if (req == 1) apply(false, 4); else if (req == 2) apply(true, 4); else if (req == 3) apply(!gOn, 3);
        if (req) {startedAt = mach_absolute_time(); continue;}   // a manual change holds until the next touch or idle period
        const uint64_t touch = houji_dvfs_touch_time();
        if (touch && touch != lastTouch) {lastTouch = touch; apply(true, 2); continue;}
        if (!idle || !gOn) continue;
        const uint64_t since = touch > startedAt ? touch : startedAt;
        uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - since, &ns);
        if (ns > (uint64_t)idle * 1000000000ull) apply(false, 1);
    }
}
}  // namespace

extern "C" void houji_screen_start(void) {
    unsigned enabled = 1; PE_parse_boot_argn("houji_screen", &enabled, sizeof enabled);
    if (!enabled || native_screen_stats[0]) return;
    thread_t t = nullptr;
    if (kernel_thread_start(screenThread, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
}
// "off", "on", "toggle", or a level 25..2866
extern "C" void houji_screen_command(const char *what) {
    if (!strcmp(what, "off")) gRequest = 1;
    else if (!strcmp(what, "on")) gRequest = 2;
    else if (!strcmp(what, "toggle")) gRequest = 3;
    else {unsigned v = 0; for (const char *c = what; *c >= '0' && *c <= '9'; ++c) v = v * 10 + (unsigned)(*c - '0');
          if (v >= HBL_MIN && v <= HBL_MAX) gRequest = 0x10000 | v;}
}
extern "C" void houji_screen_report(void) {
    const volatile int32_t *s = native_screen_stats;
    IOLog("HOUJI SCREEN STATE: stage=%d on=%d level=%d sent=%d failed=%d cause=%d\n", s[0], s[1], s[2], s[3], s[4], s[5]);
}
