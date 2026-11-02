// SPDX-License-Identifier: BSD-2-Clause-Patent
// Wi-Fi bring-up on houji (Xiaomi 14, SM8650): the WCN7850 ("Kiwi v2") on PCIe0. Adapted from UniXNU-sheng's WifiProbe.cpp (SM8550, the same
// chip on the same controller). Boot argument houji_wifi: 0 off (default), 1 a read-only survey, 2 power and link training, 3 also the
// endpoint's configuration read, 4 also its BAR and MHI registers.
// What differs from sheng, from Linux gcc-sm8650.c, tcsrcc-sm8650.c, icc sm8650.c and the board's devicetree:
// - GDSC collapse votes at 0x5214c (0x52020 on SM8550); the NoC branches report at 0x10058 / 0x10064 / 0x10074;
// - the PCIe0 reference clock gate is TCSR 0x1fc0000 + 0x31100; the PCIe memory NoC bus clock manager is SN4 (SN7 on SM8550);
// - WLAN_EN is GPIO 16 (80 on sheng); PERST# 94, CLKREQ# 95, WAKE# 96 as on sheng;
// - the WCN7850 PMU supplies (vdd smpi4 0.94 V, vddio ldob15 1.8 V, vddio1p2 ldoc3 1.2 V, vddaon smpc2 0.976 V, vdddig smpc3 0.916 V,
//   vddrfa1p2 smpc1 1.316 V, vddrfa1p8 smpc6 1.864 V), the shared antenna rail ldok6 1.8 V and the PMU's reference clock RF_CLK1.
// The QMP PHY tables, PARF/DBI/ATU/config windows, the stream IDs (0x1400 root port, 0x1401 endpoint) and the MSI line (SPI 141) are the same.
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include <stdint.h>
#include "WifiPcieTables.h"
#include "../houji-adsp/QcomRpmh.h"

// [0] stage (negative: the step that failed), [1] vote 0x52008, [2] vote 0x52000, [3] GDSC, [4] halt bits (bit i: branch i of kBranches off),
// [5] pins (WLAN_EN | PERST# << 8 | CLKREQ# << 16, bit 0 input level, bit 1 output), [6] PARF LTSSM, [7] root port vendor/device,
// [8] link control/status, [9] PARF PHY control, [10] pins after WLAN_EN, [11] GDSC bits | halt bits << 8, [12] PCS_STATUS1 | polls << 16,
// [13] port debug 0, [14] port debug 1, [15] endpoint vendor/device, [16] link ms, [17] rails found (bit i), [18] RF clock | BCM vote,
// [19] RSC id, [20] SID table hash | bypass register << 8, [21] pipe branch after the PHY, [22..31] endpoint: BAR0 mask, BAR0, command,
// MHIREGLEN, MHIVER, MHICFG, BHIOFF, BHIEOFF, MHICTRL, MHISTATUS.
extern "C" {volatile uint32_t native_wifi_stats[32];}

namespace {
constexpr uint64_t kGcc = 0x100000, kTlmm = 0xf100000, kParf = 0x1c00000, kDbi = 0x60000000, kPhy = 0x1c06000, kTcsr = 0x1fc0000;
constexpr uint64_t kAtu = 0x60001000, kConfig = 0x60100000;
constexpr uint32_t kTcsrPcie0Clkref = 0x31100;
constexpr uint32_t kVote8 = 0x52008, kVote0 = 0x52000, kGdsc = 0x6b004, kPhyGdsc = 0x6c000, kCollapse = 0x5214c;
// slv_axi, mstr_axi, cfg_ahb, aux, pipe, slv_q2a, cnoc_sf, aggre_noc, ddrss_sf, phy_rchng
constexpr uint32_t kBranches[] = {0x6b020, 0x6b02c, 0x6b038, 0x6b03c, 0x6b048, 0x6b01c, 0x10058, 0x10064, 0x10074, 0x6b054};
constexpr uint32_t kNeeded = 0xf;   // slv_axi, mstr_axi, cfg_ahb, aux
constexpr unsigned kWlanEn = 16, kPerst = 94, kClkReq = 95;
constexpr unsigned kPins[] = {kWlanEn, kPerst, kClkReq};
constexpr uint32_t kParfLtssm = 0x1b0, kParfPhyCtrl = 0x40, kLinkControl = 0x80;
constexpr uint32_t kPcieBcr = 0x6b000, kPhyBcr = 0x6c01c, kPipeMux = 0x6b070, kAuxRcg = 0x6b074, kRchngRcg = 0x6b058;
constexpr uint32_t kVote8Bits = 0x7f, kVote0Bits = 1u << 12 | 1u << 19 | 1u << 22;
constexpr uint32_t kParfSysCtrl = 0x0, kParfPmCtrl = 0x20, kParfDbiBase = 0x350, kParfDbiBaseHi = 0x354, kParfMhiClockReset = 0x174;
constexpr uint32_t kParfAxiHalt = 0x1a8, kParfAtuBase = 0x634, kParfAtuBaseHi = 0x638, kParfDeviceType = 0x1000;
constexpr uint32_t kParfSlvSpace = 0x358, kParfSlvSpaceHi = 0x35c, kSlvSpaceBytes = 0x80000000;
constexpr uint32_t kPortDebug0 = 0x728, kPortDebug1 = 0x72c, kBusNumbers = 0x18, kCommand = 0x4;
constexpr uint64_t kMemBase = 0x60300000, kMemSize = 0x3d00000;

struct Window {IOMemoryMap *map; volatile uint32_t *va;};
bool mapWindow(Window &w, uint64_t base, uint64_t size) {
    IODeviceMemory *memory = IODeviceMemory::withRange(base, size);
    w.map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (memory) memory->release();
    w.va = w.map ? reinterpret_cast<volatile uint32_t *>(w.map->getVirtualAddress()) : nullptr;
    return w.va != nullptr;
}
void release(Window &w) {if (w.map) w.map->release(); w = {};}

// TLMM pin config: pull in bits 0-1 (1 down, 3 up), function in 2-5, drive (mA / 2 - 1) in 6-8, output enable bit 9; io bit 1 drives
void setPin(volatile uint32_t *tlmm, unsigned pin, uint32_t config, int level) {
    if (level >= 0) tlmm[(pin * 0x1000 + 4) / 4] = level ? 2u : 0u;
    tlmm[pin * 0x1000 / 4] = config;
    if (level >= 0) tlmm[(pin * 0x1000 + 4) / 4] = level ? 2u : 0u;
}
uint32_t pinWord(volatile uint32_t *tlmm) {
    uint32_t w = 0;
    for (unsigned i = 0; i < 3; ++i) w |= (tlmm[(kPins[i] * 0x1000 + 4) / 4] & 3u) << (8 * i);
    return w;
}
bool waitClear(volatile uint32_t *reg, uint32_t mask, unsigned tries, unsigned *used = nullptr) {
    unsigned i = 0;
    for (; i < tries && (*reg & mask); ++i) IODelay(100);
    if (used) *used = i;
    return !(*reg & mask);
}
void pulseReset(volatile uint32_t *gcc, uint32_t bcr) {
    gcc[bcr / 4] |= 1u;
    IODelay(1000);
    gcc[bcr / 4] &= ~1u;
    IODelay(1000);
}

// The PMU's supplies in power sequencer order, then the antenna rail; each a VRM resource of the command database with its voltage in mV at
// +0, enable at +4 and mode at +8 (7: high power mode for these SMPSs and LDOs).
struct Rail {const char *name; uint32_t millivolts;};
constexpr Rail kRails[] = {{"smpi4", 940}, {"ldob15", 1800}, {"ldoc3", 1200}, {"smpc2", 976}, {"smpc3", 916}, {"smpc1", 1316},
                           {"smpc6", 1864}, {"ldok6", 1800}};
constexpr uint64_t kRscDrv = 0x17a20000, kRscTcs = 0xd00, kCmdDb = 0x81c60000, kCmdDbSize = 0x20000;

int powerRails() {
    uint8_t *db = static_cast<uint8_t *>(IOMallocData(kCmdDbSize));
    if (!db) return -1;
    Window dbw = {}, rsc = {};
    if (!mapWindow(dbw, kCmdDb, kCmdDbSize)) {IOFreeData(db, kCmdDbSize); return -1;}
    for (uint64_t i = 0; i < kCmdDbSize; i += 4) {const uint32_t w = dbw.va[i / 4]; memcpy(db + i, &w, 4);}
    release(dbw);
    if (!mapWindow(rsc, kRscDrv, 0x10000)) {IOFreeData(db, kCmdDbSize); return -2;}
    volatile uint32_t *r = rsc.va;
    const QcomRscLayout layout = qcom_rsc_layout(r[0]);
    const unsigned tcs = 0;   // active TCS 0; the ADSP's votes go through TCS 1
    native_wifi_stats[19] = r[0];
    auto send = [&](uint32_t address, uint32_t value) {
        QcomRpmhWrite writes[16];
        const unsigned n = qcom_rpmh_active_request(&layout, tcs, &address, &value, 1, writes, 16);
        for (unsigned i = 0; i < n; ++i) r[(kRscTcs + writes[i].offset) / 4] = writes[i].value;
        __asm__ volatile("dsb sy" ::: "memory");
        IOSleep(2);
        const unsigned m = qcom_rpmh_finish(&layout, tcs, writes, 16);
        for (unsigned i = 0; i < m; ++i) r[(kRscTcs + writes[i].offset) / 4] = writes[i].value;
    };
    uint32_t found = 0;
    for (unsigned i = 0; i < sizeof kRails / sizeof kRails[0]; ++i) {
        QcomRpmhResource res;
        if (qcom_cmd_db_find(db, kCmdDbSize, kRails[i].name, &res)) continue;
        found |= 1u << i;
        send(res.address + 8, 7);
        send(res.address, kRails[i].millivolts);
        send(res.address + 4, 1);
        IOSleep(5);
    }
    native_wifi_stats[17] = found;
    // the PMU's reference clock (RF_CLK1, a VRM-type clock: enable at +4)
    static const char *const clocks[] = {"clka1", "rfclka1"};
    for (const char *name : clocks) {
        QcomRpmhResource res;
        if (qcom_cmd_db_find(db, kCmdDbSize, name, &res)) continue;
        send(res.address + 4, 1);
        native_wifi_stats[18] = 0x80000000u | (name[0] == 'r' ? 2u : 1u) << 24;
        break;
    }
    // The endpoint's DMA crosses the PCIe memory NoC: its bus clock manager (SN4 on SM8650) gets the vote the interconnect driver makes when
    // the link comes up. vote = ceil(ceil(kBps * width / buswidth) * 1000 / unit), unit and width from the resource's aux data, buswidth 16.
    {
        QcomRpmhResource bcm;
        if (!qcom_cmd_db_find(db, kCmdDbSize, "SN4", &bcm)) {
            uint32_t vote = 0x400;
            if (bcm.level_count >= 3 && (bcm.levels[0] | (uint32_t)bcm.levels[1] << 16) && bcm.levels[2]) {
                const uint64_t unit = bcm.levels[0] | (uint32_t)bcm.levels[1] << 16;
                const uint64_t scaled = (2000000ull * bcm.levels[2] + 15) / 16;
                const uint64_t x = (scaled * 1000 + unit - 1) / unit;
                vote = x < 1 ? 1 : x > 0x3fff ? 0x3fff : (uint32_t)x;
            }
            send(bcm.address, 1u << 30 | 1u << 29 | vote << 14 | vote);
            native_wifi_stats[18] |= vote | 0x8000u;
        }
    }
    release(rsc);
    IOFreeData(db, kCmdDbSize);
    return (found & 0x7fu) == 0x7fu ? 0 : -3;   // the seven PMU supplies; the antenna rail is best effort
}

// Stage 4: forward the memory window to bus 1, give BAR0 an address in it, memory decoding and bus mastering on, read the MHI registers.
void endpointProbe(volatile uint32_t *a, volatile uint32_t *c, volatile uint32_t *d) {
    volatile uint32_t *r1 = a + 0x200 / 4;   // outbound iATU region 1: memory, the same PCI addresses
    r1[0x08 / 4] = uint32_t(kMemBase);
    r1[0x0c / 4] = uint32_t(kMemBase >> 32);
    r1[0x10 / 4] = uint32_t(kMemBase + kMemSize - 1);
    r1[0x20 / 4] = uint32_t((kMemBase + kMemSize - 1) >> 32);
    r1[0x14 / 4] = uint32_t(kMemBase);
    r1[0x18 / 4] = uint32_t(kMemBase >> 32);
    r1[0x00 / 4] = 0x0;
    r1[0x04 / 4] = 1u << 31;
    for (unsigned i = 0; i < 5 && !(r1[0x04 / 4] & (1u << 31)); ++i) IODelay(10);
    d[0x20 / 4] = uint32_t(((kMemBase + kMemSize - 1) & 0xfff00000u) | ((kMemBase >> 16) & 0xfff0u));
    const uint32_t saved = c[0x10 / 4];
    c[0x10 / 4] = 0xffffffffu;
    const uint32_t mask = c[0x10 / 4];
    c[0x10 / 4] = saved;
    native_wifi_stats[22] = mask;
    const uint32_t size = ~(mask & ~0xfu) + 1;
    if (!(mask & ~0xfu) || !size || size > kMemSize) {native_wifi_stats[0] = (uint32_t)-18; return;}
    const uint32_t address = uint32_t((kMemBase + size - 1) & ~uint64_t(size - 1));
    c[0x10 / 4] = address | (mask & 0xfu);
    if ((mask & 6u) == 4u) c[0x14 / 4] = 0;
    c[0x04 / 4] = (c[0x04 / 4] & 0xffffu) | 0x6u;
    native_wifi_stats[23] = c[0x10 / 4];
    native_wifi_stats[24] = c[0x04 / 4];
    Window bar = {};
    if (!mapWindow(bar, address, size < 0x200000 ? size : 0x200000)) {native_wifi_stats[0] = (uint32_t)-19; return;}
    static const uint32_t regs[7] = {0x00, 0x08, 0x10, 0x28, 0x2c, 0x38, 0x48};
    for (unsigned i = 0; i < 7; ++i) native_wifi_stats[25 + i] = bar.va[regs[i] / 4];
    native_wifi_stats[0] = 19;
    release(bar);
}

void bringUp(Window &gcc, Window &tlmm, unsigned stage) {
    volatile uint32_t *g = gcc.va, *t = tlmm.va;
    const int rails = powerRails();
    IOSleep(20);
    if (rails) {native_wifi_stats[0] = (uint32_t)(-9 * 100 + rails); return;}
    // PERST# held low, CLKREQ# to its PCIe function, then WLAN_EN low (a clean start) and high, and the WCN7850 power-up delay
    setPin(t, kPerst, 1u | 1u << 9, 0);
    setPin(t, kClkReq, 3u | 1u << 2, -1);
    setPin(t, kWlanEn, 1u | 7u << 6 | 1u << 9, 0);
    IOSleep(150);
    setPin(t, kWlanEn, 1u | 7u << 6 | 1u << 9, 1);
    IOSleep(60);
    native_wifi_stats[10] = pinWord(t);
    native_wifi_stats[0] = 10;

    g[kCollapse / 4] &= ~(1u << 0 | 1u << 3);
    unsigned tries = 0;
    while (tries++ < 100 && (!(g[kGdsc / 4] & (1u << 31)) || !(g[kPhyGdsc / 4] & (1u << 31)))) IODelay(10);
    const uint32_t power = (g[kGdsc / 4] >> 31) | (g[kPhyGdsc / 4] >> 31) << 1;
    native_wifi_stats[11] = power;
    if (power != 3) {native_wifi_stats[0] = (uint32_t)-11; return;}
    native_wifi_stats[0] = 11;

    g[(kAuxRcg + 4) / 4] = 0;              // TCXO
    g[kAuxRcg / 4] |= 1u;
    g[(kRchngRcg + 4) / 4] = 6u << 8 | 5u;   // GPLL0 even / 3: 100 MHz
    g[kRchngRcg / 4] |= 1u;
    waitClear(&g[kAuxRcg / 4], 1u, 100);
    waitClear(&g[kRchngRcg / 4], 1u, 100);
    g[kPipeMux / 4] = (g[kPipeMux / 4] & ~3u) | 2u;
    g[kVote8 / 4] |= kVote8Bits;
    g[kVote0 / 4] |= kVote0Bits;
    Window tcsr = {};
    if (!mapWindow(tcsr, kTcsr + kTcsrPcie0Clkref, 0x1000)) {native_wifi_stats[0] = (uint32_t)-12; return;}
    tcsr.va[0] |= 1u;
    release(tcsr);
    IODelay(200);
    uint32_t off = 0;
    for (unsigned i = 0; i < sizeof kBranches / sizeof kBranches[0]; ++i)
        if (g[kBranches[i] / 4] & (1u << 31)) off |= 1u << i;
    native_wifi_stats[11] = power | off << 8;
    if (off & ~(1u << 4)) {native_wifi_stats[0] = (uint32_t)-12; return;}   // the pipe branch runs only once the PHY drives it
    pulseReset(g, kPcieBcr);
    pulseReset(g, kPhyBcr);
    native_wifi_stats[0] = 12;

    Window parf = {}, dbi = {};
    if (!mapWindow(parf, kParf, 0x3000) || !mapWindow(dbi, kDbi, 0x1000)) {release(parf); native_wifi_stats[0] = (uint32_t)-13; return;}
    volatile uint32_t *f = parf.va, *d = dbi.va;
    f[kParfDeviceType / 4] = 4u;
    f[kParfPhyCtrl / 4] &= ~1u;
    f[kParfDbiBase / 4] = uint32_t(kDbi);
    f[kParfDbiBaseHi / 4] = uint32_t(kDbi >> 32);
    f[kParfAtuBase / 4] = uint32_t(kAtu);
    f[kParfAtuBaseHi / 4] = uint32_t(kAtu >> 32);
    f[kParfSlvSpace / 4] = 0;
    f[kParfSlvSpaceHi / 4] = kSlvSpaceBytes;
    f[kParfSysCtrl / 4] &= ~(1u << 29);
    f[kParfMhiClockReset / 4] |= 1u << 4;
    f[kParfPmCtrl / 4] &= ~(1u << 5);
    f[kParfAxiHalt / 4] |= 1u << 31;
    native_wifi_stats[0] = 13;

    Window phy = {};
    if (!mapWindow(phy, kPhy, 0x2000)) {release(dbi); release(parf); native_wifi_stats[0] = (uint32_t)-14; return;}
    volatile uint32_t *p = phy.va;
    using namespace wifi_phy;
    p[kPowerDownControl / 4] |= 3u;
    for (const Init &e : kSerdes) p[e.offset / 4] = e.value;
    for (const Init &e : kTx) {p[e.offset / 4] = e.value; p[(e.offset + kTx2) / 4] = e.value;}
    for (const Init &e : kRx) {p[e.offset / 4] = e.value; p[(e.offset + kRx2) / 4] = e.value;}
    for (const Init &e : kPcs) p[e.offset / 4] = e.value;
    for (const Init &e : kPcsMisc) p[e.offset / 4] = e.value;
    g[kPipeMux / 4] &= ~3u;
    p[kSwReset / 4] &= ~1u;
    p[kStartControl / 4] |= 3u;
    IODelay(1000);
    unsigned polls = 0;
    const bool ready = waitClear(&p[kPcsStatus1 / 4], 1u << 6, 100, &polls);
    native_wifi_stats[12] = (p[kPcsStatus1 / 4] & 0xffffu) | polls << 16;
    native_wifi_stats[21] = g[kBranches[4] / 4];
    release(phy);
    if (!ready) {release(dbi); release(parf); native_wifi_stats[0] = (uint32_t)-14; return;}

    // qcom_pcie_config_sid_1_9_0: requester 0x100 (the endpoint) -> stream offset 1 (0x1401); CRC-8 (poly 7) of the requester ID's bytes
    for (unsigned i = 0; i < 256; ++i) f[(0x2000 + 4 * i) / 4] = 0;
    f[0x2c00 / 4] &= ~1u;
    {
        uint8_t crc = 0;
        const uint8_t bytes[2] = {0x01, 0x00};
        for (uint8_t byte : bytes) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 7) : (uint8_t)(crc << 1);
        }
        f[(0x2000 + 4 * crc) / 4] = 0x100u << 16 | 1u << 8;
        __asm__ volatile("dsb sy" ::: "memory");
        native_wifi_stats[20] = crc | f[0x2c00 / 4] << 8;
    }

    d[kBusNumbers / 4] = 0x00010100;
    d[kCommand / 4] |= 0x6u;
    native_wifi_stats[7] = d[0];
    native_wifi_stats[0] = 14;

    IOSleep(100);
    setPin(t, kPerst, 1u | 1u << 9, 1);
    IODelay(1000);
    native_wifi_stats[0] = 15;
    f[kParfLtssm / 4] |= 1u << 8;
    const uint64_t start = mach_absolute_time();
    unsigned ms = 0;
    for (; ms < 1000; ++ms) {
        if ((d[kPortDebug1 / 4] & (1u << 4)) && (d[kPortDebug0 / 4] & 0x3f) == 0x11) break;
        IOSleep(1);
    }
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - start, &ns);
    native_wifi_stats[16] = uint32_t(ns / 1000000);
    native_wifi_stats[13] = d[kPortDebug0 / 4];
    native_wifi_stats[14] = d[kPortDebug1 / 4];
    native_wifi_stats[6] = f[kParfLtssm / 4];
    native_wifi_stats[8] = d[kLinkControl / 4];
    native_wifi_stats[9] = f[kParfPhyCtrl / 4];
    if (ms == 1000) {release(dbi); release(parf); native_wifi_stats[0] = (uint32_t)-16; return;}
    native_wifi_stats[0] = 16;
    if (stage < 3) {release(dbi); release(parf); return;}

    Window atu = {}, config = {};
    if (mapWindow(atu, kAtu, 0x1000) && mapWindow(config, kConfig, 0x1000)) {
        volatile uint32_t *a = atu.va;
        a[0x08 / 4] = uint32_t(kConfig);
        a[0x0c / 4] = uint32_t(kConfig >> 32);
        a[0x10 / 4] = uint32_t(kConfig + 0xfffff);
        a[0x20 / 4] = uint32_t((kConfig + 0xfffff) >> 32);
        a[0x14 / 4] = 1u << 24;
        a[0x18 / 4] = 0;
        a[0x00 / 4] = 0x4;
        a[0x04 / 4] = 1u << 31;
        for (unsigned i = 0; i < 5 && !(a[0x04 / 4] & (1u << 31)); ++i) IODelay(10);
        native_wifi_stats[15] = config.va[0];
        native_wifi_stats[0] = 17;
        if (stage >= 4 && (config.va[0] & 0xffff) == 0x17cb) endpointProbe(a, config.va, d);
    } else native_wifi_stats[0] = (uint32_t)-17;
    release(config); release(atu); release(dbi); release(parf);
}

void wifiThread(void *arg, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-wifi");
    const unsigned stage = (unsigned)reinterpret_cast<uintptr_t>(arg);
    unsigned delay = 50;
    PE_parse_boot_argn("houji_wifi_delay", &delay, sizeof delay);
    IOSleep(delay * 1000);
    Window gcc = {}, tlmm = {};
    if (!mapWindow(gcc, kGcc, 0x80000) || !mapWindow(tlmm, kTlmm, 0x100000)) {native_wifi_stats[0] = (uint32_t)-1; release(gcc); release(tlmm); return;}
    native_wifi_stats[0] = 1;
    native_wifi_stats[1] = gcc.va[kVote8 / 4];
    native_wifi_stats[2] = gcc.va[kVote0 / 4];
    native_wifi_stats[3] = gcc.va[kGdsc / 4];
    uint32_t off = 0;
    for (unsigned i = 0; i < sizeof kBranches / sizeof kBranches[0]; ++i)
        if (gcc.va[kBranches[i] / 4] & (1u << 31)) off |= 1u << i;
    native_wifi_stats[4] = off;
    native_wifi_stats[5] = pinWord(tlmm.va);
    native_wifi_stats[0] = 2;
    if (stage >= 2) bringUp(gcc, tlmm, stage);
    release(tlmm); release(gcc);
    thread_terminate(current_thread());
}
}  // namespace

extern "C" void houji_wifi_start(void) {
    unsigned stage = 0;
    if (!PE_parse_boot_argn("houji_wifi", &stage, sizeof stage) || !stage || native_wifi_stats[0]) return;
    thread_t thread = nullptr;
    if (kernel_thread_start(wifiThread, reinterpret_cast<void *>(uintptr_t(stage)), &thread) == KERN_SUCCESS) thread_deallocate(thread);
    else native_wifi_stats[0] = (uint32_t)-7;
}

extern "C" void houji_wifi_report(void) {
    const volatile uint32_t *s = native_wifi_stats;
    if (!s[0]) return;
    IOLog("HOUJI WIFI: stage=%d votes=%x/%x gdsc=%x off=%x pins=%x/%x power=%x phy=%x ltssm=%x/%x link-ms=%u rp=%x ep=%x\n",
          (int)s[0], s[1], s[2], s[3], s[4], s[5], s[10], s[11], s[12], s[13], s[14], s[16], s[7], s[15]);
    IOLog("HOUJI WIFI RAILS: found=%x clk=%x rsc=%x sid=%x pipe=%x lnk=%x parf=%x/%x bar=%x/%x cmd=%x mhi=%x %x %x %x %x %x %x\n",
          s[17], s[18], s[19], s[20], s[21], s[8], s[6], s[9], s[22], s[23], s[24], s[25], s[26], s[27], s[28], s[29], s[30], s[31]);
}
