// SPDX-License-Identifier: BSD-2-Clause-Patent
// Camera bring-up on houji (Xiaomi 14), stage 1: power the front camera and find what answers on its bus. Boot argument houji_camera=1
// (off by default); runs once, 70 s into the boot, then powers everything down again.
// Facts from the handset's stock dtbo (fragment@70, qcom,cam-sensor1 under qupv3_se9_i2c): the front sensor and its EEPROM sit on QUP2
// SE1's I2C (SDA GPIO 4, SCL GPIO 5, external pull-ups, 1 MHz in stock), not on CCI; supplies cam_vio L5M 1.8 V, cam_vana L3M 2.8 V,
// cam_vdig L1M 1.1 V (PM8010, RPMh "ldom5/3/1"); MCLK4 at 19.2 MHz on GPIO 104 (function 1, cam_aon_mclk4), from camcc (0x0ade0000)
// mclk4 RCG 0x15070 and branch 0x15088 (camcc-sm8650.c); reset on GPIO 7, active low. camcc needs the MMCX rail (on for the display) and GCC's camera AHB
// (0x26004) and XO (0x26028) branches. This stage drives I2C by toggling the two pins (open drain: low = output 0, high = input), so no
// QUP firmware or GENI programming is involved, scans 0x08-0x77 and reads the usual chip ID registers of every device that answers.
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "../houji-adsp/QcomRpmh.h"

// [0] stage (negative: the step that failed), [1] rails found, [2] mclk branch after enable, [3] devices answering, [4..7] their addresses,
// [8] I2C errors
extern "C" {volatile int32_t native_camera_stats[10];}

namespace {
volatile int32_t *const cs = native_camera_stats;
struct Window {IOMemoryMap *map; volatile uint32_t *va;};
bool mapWindow(Window &w, uint64_t base, uint64_t size) {
    IODeviceMemory *m = IODeviceMemory::withRange(base, size);
    w.map = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (m) m->release();
    w.va = w.map ? reinterpret_cast<volatile uint32_t *>(w.map->getVirtualAddress()) : nullptr;
    return w.va != nullptr;
}
void release(Window &w) {if (w.map) w.map->release(); w = {};}
void dsb() {__asm__ volatile("dsb sy" ::: "memory");}

constexpr uint64_t kTlmm = 0xf100000, kGcc = 0x100000, kCamcc = 0x0ade0000;
constexpr unsigned kSda = 4, kScl = 5, kMclk = 104, kReset = 7;
constexpr uint64_t kRscDrv = 0x17a20000, kRscTcs = 0xd00, kCmdDb = 0x81c60000, kCmdDbSize = 0x20000;
Window gTlmm, gGcc, gCamcc;

// ---- TLMM: config at pin * 0x1000 (pull 1:0, function 5:2, drive 8:6, output enable 9), io at +4 (bit 0 in, bit 1 out) ----
volatile uint32_t &pinCfg(unsigned pin) {return gTlmm.va[pin * 0x1000 / 4];}
volatile uint32_t &pinIo(unsigned pin) {return gTlmm.va[(pin * 0x1000 + 4) / 4];}
void pinGpio(unsigned pin, bool output, int level) {
    if (level >= 0) pinIo(pin) = level ? 2u : 0u;
    pinCfg(pin) = (output ? 1u << 9 : 0u) | (1u << 6);   // function 0 (GPIO), 4 mA, no pull
    dsb();
}

// ---- I2C by hand, about 100 kHz ----
void half() {IODelay(5);}
void sdaLow() {pinIo(kSda) = 0; pinCfg(kSda) = (1u << 9) | (1u << 6); dsb();}
void sdaRelease() {pinCfg(kSda) = 1u << 6; dsb();}
void sclLow() {pinIo(kScl) = 0; pinCfg(kScl) = (1u << 9) | (1u << 6); dsb();}
bool sclRelease() {pinCfg(kScl) = 1u << 6; dsb(); for (unsigned i = 0; i < 1000; ++i) {if (pinIo(kScl) & 1) return true; IODelay(1);} return false;}
bool sdaRead() {return pinIo(kSda) & 1;}
void start() {sdaRelease(); sclRelease(); half(); sdaLow(); half(); sclLow(); half();}
void stop() {sdaLow(); half(); sclRelease(); half(); sdaRelease(); half();}
bool writeByte(uint8_t b) {   // true: acknowledged
    for (int i = 7; i >= 0; --i) {if ((b >> i) & 1) sdaRelease(); else sdaLow(); half(); sclRelease(); half(); sclLow();}
    sdaRelease(); half(); sclRelease(); half();
    const bool ack = !sdaRead();
    sclLow(); half();
    return ack;
}
uint8_t readByte(bool ack) {
    uint8_t b = 0;
    sdaRelease();
    for (int i = 7; i >= 0; --i) {half(); sclRelease(); half(); b = (uint8_t)(b << 1 | (sdaRead() ? 1 : 0)); sclLow();}
    if (ack) sdaLow(); else sdaRelease();
    half(); sclRelease(); half(); sclLow(); sdaRelease();
    return b;
}
bool probe(uint8_t addr) {start(); const bool a = writeByte((uint8_t)(addr << 1)); stop(); return a;}
// a 16-bit register address, one byte back (the sensors' convention); -1 when not acknowledged
int read16(uint8_t addr, uint16_t reg) {
    start();
    if (!writeByte((uint8_t)(addr << 1)) || !writeByte((uint8_t)(reg >> 8)) || !writeByte((uint8_t)reg)) {stop(); return -1;}
    start();
    if (!writeByte((uint8_t)(addr << 1 | 1))) {stop(); return -1;}
    const uint8_t v = readByte(false);
    stop();
    return v;
}

// ---- RPMh: the camera rails through active TCS 2 (the ADSP uses 1, Wi-Fi 0) ----
struct Rpmh {
    uint8_t *db; Window rsc; QcomRscLayout layout;
    bool open() {
        db = static_cast<uint8_t *>(IOMallocData(kCmdDbSize));
        Window dbw = {};
        if (!db || !mapWindow(dbw, kCmdDb, kCmdDbSize)) return false;
        for (uint64_t i = 0; i < kCmdDbSize; i += 4) {const uint32_t w = dbw.va[i / 4]; memcpy(db + i, &w, 4);}
        release(dbw);
        if (!mapWindow(rsc, kRscDrv, 0x10000)) return false;
        layout = qcom_rsc_layout(rsc.va[0]);
        return true;
    }
    void close() {if (db) IOFreeData(db, kCmdDbSize); db = nullptr; release(rsc);}
    void send(uint32_t address, uint32_t value) {
        QcomRpmhWrite writes[16];
        const unsigned n = qcom_rpmh_active_request(&layout, 2, &address, &value, 1, writes, 16);
        for (unsigned i = 0; i < n; ++i) rsc.va[(kRscTcs + writes[i].offset) / 4] = writes[i].value;
        dsb(); IOSleep(2);
        const unsigned m = qcom_rpmh_finish(&layout, 2, writes, 16);
        for (unsigned i = 0; i < m; ++i) rsc.va[(kRscTcs + writes[i].offset) / 4] = writes[i].value;
    }
    bool ldo(const char *name, uint32_t mv, bool on) {
        QcomRpmhResource r;
        if (qcom_cmd_db_find(db, kCmdDbSize, name, &r)) return false;
        if (on) {send(r.address + 8, 7); send(r.address, mv); send(r.address + 4, 1);}
        else send(r.address + 4, 0);
        IOSleep(3);
        return true;
    }
};

bool waitBits(volatile uint32_t *reg, uint32_t mask, bool set, unsigned us) {
    for (unsigned i = 0; i < us; i += 10) {if (((*reg & mask) != 0) == set) return true; IODelay(10);}
    return ((*reg & mask) != 0) == set;
}

void cameraThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-camera");
    unsigned delay = 70; PE_parse_boot_argn("houji_camera_delay", &delay, sizeof delay);
    IOSleep(delay * 1000);
    Rpmh rpmh = {};
    if (!mapWindow(gTlmm, kTlmm, 0x100000) || !mapWindow(gGcc, kGcc, 0x80000) || !rpmh.open()) {cs[0] = -1; return;}
    cs[0] = 1;
    // camcc's rail MMCX is already up for the display (dispcc shares it); a vote from this DRV would replace the firmware's, so none here.
    uint32_t found = 0;
    static const uint32_t branches[] = {0x26004u, 0x26028u};
    for (uint32_t off : branches) {gGcc.va[off / 4] |= 1u; dsb(); waitBits(&gGcc.va[off / 4], 1u << 31, false, 1000);}
    if (!mapWindow(gCamcc, kCamcc, 0x20000)) {cs[0] = -2; rpmh.close(); return;}
    cs[0] = 2;
    // supplies in the sensor's order: vio, vana, vdig
    if (rpmh.ldo("ldom5", 1800, true)) found |= 2;
    if (rpmh.ldo("ldom3", 2800, true)) found |= 4;
    if (rpmh.ldo("ldom1", 1100, true)) found |= 8;
    cs[1] = (int32_t)found;
    IOSleep(5);
    cs[0] = 3;
    // MCLK4: RCG from the crystal, divider 1 (CFG source 0, SRC_DIV 2 * 1 - 1), update, then the branch; then the pin to the clock
    volatile uint32_t *rcg = gCamcc.va + 0x15070 / 4, *cbcr = gCamcc.va + 0x15088 / 4;
    rcg[1] = 1u; dsb();
    rcg[0] |= 1u; dsb();
    waitBits(&rcg[0], 1u, false, 2000);
    *cbcr |= 1u; dsb();
    const bool running = waitBits(cbcr, 1u << 31, false, 2000);
    cs[2] = (int32_t)*cbcr;
    pinCfg(kMclk) = (1u << 2) | (1u << 6); dsb();   // function 1, 4 mA
    cs[0] = running ? 4 : -4;
    // reset: low, then released after the clock runs
    pinGpio(kReset, true, 0); IOSleep(5);
    pinGpio(kReset, true, 1); IOSleep(20);
    cs[0] = 5;
    // the bus: both lines released, a scan, then the ID registers of whatever answered
    pinGpio(kSda, false, -1); pinGpio(kScl, false, -1);
    IOSleep(1);
    IOLog("HOUJI CAMERA: rails %x, mclk4 cbcr %08x, bus idle sda %u scl %u\n", found, cs[2], (unsigned)(pinIo(kSda) & 1), (unsigned)(pinIo(kScl) & 1));
    unsigned count = 0;
    for (uint8_t a = 0x08; a < 0x78; ++a) {
        if (!probe(a)) continue;
        if (count < 4) cs[4 + count] = a;
        ++count;
        const int o0 = read16(a, 0x300a), o1 = read16(a, 0x300b), o2 = read16(a, 0x300c), s0 = read16(a, 0x0016), s1 = read16(a, 0x0017),
                  m0 = read16(a, 0x0000), m1 = read16(a, 0x0001);
        IOLog("HOUJI CAMERA: device 0x%02x answers: 300a-300c %02x %02x %02x, 0016-0017 %02x %02x, 0000-0001 %02x %02x\n", a, o0 & 0xff,
              o1 & 0xff, o2 & 0xff, s0 & 0xff, s1 & 0xff, m0 & 0xff, m1 & 0xff);
    }
    cs[3] = (int32_t)count;
    IOLog("HOUJI CAMERA: %u devices on the front camera bus\n", count);
    cs[0] = 6;
    // power down: reset, clock, supplies in reverse
    pinGpio(kReset, true, 0);
    pinGpio(kMclk, false, -1);
    *cbcr &= ~1u; dsb();
    rpmh.ldo("ldom1", 0, false); rpmh.ldo("ldom3", 0, false); rpmh.ldo("ldom5", 0, false);
    rpmh.close();
    cs[0] = 7;
}
}  // namespace

extern "C" void houji_camera_start(void) {
    unsigned stage = 0; PE_parse_boot_argn("houji_camera", &stage, sizeof stage);
    if (!stage || native_camera_stats[0]) return;
    thread_t t = nullptr;
    if (kernel_thread_start(cameraThread, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
}
extern "C" void houji_camera_report(void) {
    const volatile int32_t *s = native_camera_stats;
    if (s[0]) IOLog("HOUJI CAMERA STATE: stage=%d rails=%x mclk=%08x devices=%d %02x %02x %02x %02x\n", s[0], s[1], (unsigned)s[2], s[3], s[4], s[5],
                    s[6], s[7]);
}
