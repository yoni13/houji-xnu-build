// SPDX-License-Identifier: BSD-2-Clause-Patent
// Panel power for iOS's display state. backboardd switches the display with IOMobileFramebuffer selector 12 (scalar 0 off, 1 on: seen
// on every power-key sleep and wake and on auto-lock); SweetFramebuffer forwards it here. Off: brightness 0, then DCS display off (0x28)
// and, with houji_panel_sleep=1, sleep in (0x10). On: sleep out (0x11, 120 ms) when it slept, display on (0x29), then the brightness
// read back from the panel at start (DCS 0x52). Nothing else changes the panel's state: a touch on a dark screen does not light it,
// matching iOS, which ignores touches while its display is off.
// Commands go through DSI0's command DMA FIFO (0x0ae94000) with lonzzi/UniXNU-houji's HoujiBacklightProtocol.h; short DCS writes use the
// MDSS header (last packet bit 31, data type 0x05, the command in bits 7:0).
// USB console: "screen off", "screen on" (the panel only; iOS keeps its own state), "screen LEVEL" (25..2866).
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "HoujiBacklightProtocol.h"

// [0] stage (1 running; negative: failed), [1] panel on, [2] level, [3] commands sent, [4] failed, [5] last change cause (1 iOS,
// 4 console), [6] iOS power requests, [7] level read from the panel (0: not read)
extern "C" {volatile int32_t native_screen_stats[8];}

namespace {
IOMemoryMap *gMap;
volatile uint8_t *gDsi;
uint32_t rd(void *, unsigned off) {
    const uint32_t v = *reinterpret_cast<volatile uint32_t *>(gDsi + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}
void wr(void *, unsigned off, uint32_t v) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *reinterpret_cast<volatile uint32_t *>(gDsi + off) = v;
    __asm__ volatile("dsb osh" ::: "memory");
}
uint64_t nowUs() {uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000;}
HoujiDsiCommand gCmd;
unsigned gLevel = 1600;
bool gOn = true, gSlept = false, gSleepIn = false;
volatile uint32_t gRequest;   // bit 31 set: a request; 1 off, 2 on (from iOS: | 0x100), 0x10000 | level
IOLock *gLock;
volatile int32_t *const st = native_screen_stats;

constexpr uint32_t dcs(uint8_t command) {return 0x80050000u | command;}

// one transfer, waited for with 1 ms sleeps (50 ms command limit in the protocol)
bool sendWords(const uint32_t *words, unsigned count) {
    if (gCmd.failed) return false;
    if (!gCmd.begin(words, count, nowUs())) {st[4] = st[4] + 1; return false;}
    for (unsigned i = 0; i < 80; ++i) {
        const int r = gCmd.poll(nowUs());
        if (r) {if (r < 0) {st[4] = st[4] + 1; gCmd.failed = false;} else st[3] = st[3] + 1; return r > 0;}
        IOSleep(1);
    }
    st[4] = st[4] + 1;
    return false;
}
bool sendDcs(uint8_t command) {const uint32_t w = dcs(command); return sendWords(&w, 1);}
bool sendLevel(unsigned level) {
    const uint32_t words[2] = {0xc0390003u, 0xff000051u | ((level & 255) << 16) | ((level >> 8) << 8)};
    return sendWords(words, 2);
}
// the panel's current brightness (DCS 0x52), as upstream HoujiBacklight reads it at start
bool readLevel(unsigned *level) {
    if (!gCmd.readBrightness(nowUs())) return false;
    int r = 0;
    for (unsigned i = 0; i < 80 && !(r = gCmd.poll(nowUs())); ++i) IOSleep(1);
    if (r <= 0) {gCmd.failed = false; return false;}
    for (unsigned i = 0; i < 50; ++i) {
        const uint32_t control = rd(nullptr, 0x1d4);
        if (rd(nullptr, 0x110) & (1u << 24)) return false;
        if ((control >> 16) >= 4) return houjiBacklightReadback(control, rd(nullptr, 0x6c), level);
        IOSleep(1);
    }
    return false;
}

void panelOff(int cause) {
    if (!gOn) return;
    sendLevel(0);
    sendDcs(0x28);
    if (gSleepIn) {sendDcs(0x10); IOSleep(5); gSlept = true;}
    gOn = false; st[1] = 0; st[5] = cause;
    IOLog("HOUJI SCREEN: off (%s)%s\n", cause == 1 ? "iOS" : "console", gSlept ? ", panel asleep" : "");
}
void panelOn(int cause) {
    if (gOn) return;
    if (gSlept) {sendDcs(0x11); IOSleep(120); gSlept = false;}
    sendDcs(0x29);
    IOSleep(1);
    sendLevel(gLevel);
    gOn = true; st[1] = 1; st[5] = cause;
    IOLog("HOUJI SCREEN: on (%s), level %u\n", cause == 1 ? "iOS" : "console", gLevel);
}

void screenThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-screen");
    IODeviceMemory *m = IODeviceMemory::withRange(0x0ae94000ULL, 0x1000);
    gMap = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (m) m->release();
    if (!gMap) {st[0] = -1; return;}
    gDsi = reinterpret_cast<volatile uint8_t *>(gMap->getVirtualAddress());
    gCmd.io = {nullptr, rd, wr};
    unsigned sleepIn = 0; PE_parse_boot_argn("houji_panel_sleep", &sleepIn, sizeof sleepIn);
    gSleepIn = sleepIn != 0;
    unsigned level = 0;
    if (PE_parse_boot_argn("houji_screen_level", &level, sizeof level) && level >= HBL_MIN && level <= HBL_MAX) gLevel = level;
    else if (readLevel(&level) && level >= HBL_MIN && level <= HBL_MAX) {gLevel = level; st[7] = (int32_t)level;}
    st[0] = 1; st[1] = 1; st[2] = (int32_t)gLevel;
    IOLog("HOUJI SCREEN: ready, level %u (%s), panel sleep %s\n", gLevel, st[7] ? "read from the panel" : "default", gSleepIn ? "on" : "off");
    for (;;) {
        IOLockLock(gLock);
        while (!gRequest) IOLockSleep(gLock, (void *)&gRequest, THREAD_UNINT);
        const uint32_t req = gRequest; gRequest = 0;
        IOLockUnlock(gLock);
        if (!req) continue;
        const int cause = (req & 0x100) ? 1 : 4;
        if (req & 0x10000) {
            gLevel = req & 0xffff; st[2] = (int32_t)gLevel;
            if (gOn) sendLevel(gLevel);
        } else if ((req & 3) == 1) panelOff(cause);
        else if ((req & 3) == 2) panelOn(cause);
    }
}
void post(uint32_t req) {   // the latest request wins
    IOLockLock(gLock); gRequest = req | 0x80000000u; IOLockWakeup(gLock, (void *)&gRequest, false); IOLockUnlock(gLock);
}
}  // namespace

extern "C" void houji_screen_start(void) {
    unsigned enabled = 1; PE_parse_boot_argn("houji_screen", &enabled, sizeof enabled);
    if (!enabled || native_screen_stats[0] || !(gLock = IOLockAlloc())) return;
    thread_t t = nullptr;
    if (kernel_thread_start(screenThread, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
}
// iOS's display power (IOMobileFramebuffer selector 12)
extern "C" void houji_screen_power(int on) {
    st[6] = st[6] + 1;
    if (st[0] == 1) post((on ? 2u : 1u) | 0x100u);
}
// "off", "on", or a level 25..2866
extern "C" void houji_screen_command(const char *what) {
    if (st[0] != 1) return;
    if (!strcmp(what, "off")) post(1);
    else if (!strcmp(what, "on")) post(2);
    else {unsigned v = 0; for (const char *c = what; *c >= '0' && *c <= '9'; ++c) v = v * 10 + (unsigned)(*c - '0');
          if (v >= HBL_MIN && v <= HBL_MAX) post(0x10000 | v);}
}
extern "C" void houji_screen_report(void) {
    const volatile int32_t *s = native_screen_stats;
    IOLog("HOUJI SCREEN STATE: stage=%d on=%d level=%d sent=%d failed=%d cause=%d ios-requests=%d panel-level=%d\n", s[0], s[1], s[2], s[3],
          s[4], s[5], s[6], s[7]);
}
