// SPDX-License-Identifier: BSD-2-Clause-Patent
// The audio DSP (ADSP, LPASS) of the sheng board: boot argument sheng-adsp=1 surveys what the boot stages left (SMEM partition and items,
// the DSP's SMP2P state, the AOSS mailbox, the RSC, the command database) and writes nothing; sheng-adsp=2 starts the DSP and brings up
// GLINK so the battery manager (PMIC GLINK) and the audio services (GPR) can talk to it.
//
// Devicetree facts: remoteproc@6800000 (qcom,sm8550-adsp-pas), PAS id 1 for adsp.mbn in adspslpi 0x9ea00000 + 0x4680000 and id 36 for
// adsp_dtb.mbn in q6-adsp-dtb 0x9e980000 + 0x80000; proxy power domains lcx and lmx (RPMh ARC lcx.lvl, lmx.lvl) and the AOSS load_state
// "adsp"; SMEM 0x81d00000 + 0x200000 with TCSR mutex 3 (0x1f40000, stride 0x1000); SMP2P items 443 (in) and 429 (out), remote pid 2,
// entries "master-kernel" (bit 0 stop) and "slave-kernel" (fatal 0, ready 1, handover 2, stop ack 3); IPCC 0x408000 (SPI 229): LPASS
// client 3 with signal 0 GLINK and 2 SMP2P, AOP client 0 signal 0; GLINK items 478 (descriptor), 479 (our FIFO), 480 (the DSP's FIFO);
// AOSS message RAM 0xc300000 + 0x400; apps RSC DRV 2 at 0x17a20000 with TCS block at +0xd00 and its interrupt SPI 5; cmd-db 0x81c60000.
//
// Design: one kernel thread owns every step and all protocol state. Interrupt handlers only acknowledge their hardware, record which
// doorbell rang and wake the thread; the thread waits on that event with a deadline for each step (QMP 1 s per phase, RPMh 100 ms, DSP
// ready 5 s) and then sleeps until the next doorbell or client send. Nothing spins on a register in a loop, and no lock is held across a
// wait. Clients (ShengAdsp.h) register channels and queue sends from any thread; their callbacks run on this thread.
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
extern "C" {
#include "QcomGlink.h"
#include "QcomMdt.h"
#include "QcomQmp.h"
#include "QcomRpmh.h"
#include "QcomScm.h"
#include "QcomSmem.h"
#include "QcomSmp2p.h"
#include "ShengAdsp.h"
int sheng_fw_open(const char *path, void **handle, uint64_t *size);
void sheng_qrtr_start(void);
int sheng_fw_read(void *handle, uint64_t offset, void *to, size_t length);
void sheng_fw_close(void *handle);
}

extern "C" int armcpu_spi_attach(uint32_t, bool, void (*)(void *), void *, uint32_t *, uint64_t *);
// [0] stage (see Stage; negative: the step that failed), [1] last error, [2] IPCC interrupts, [3] the DSP's slave-kernel bits (-1 no
// item), [4] items (bit 0 SMP2P in 443, 1 out 429, 2 GLINK 478, 3 479, 4 480, 8 our partition, 9 global partition), [5] QMP state | AOSS
// link bits << 8, [6] RSC DRV id, [7] adsp.mbn KiB, [8] ms from the start call to "ready", [9] GLINK messages in, [10] out, [11] channels
// up (bit per client) | version agreed << 14 | link broken << 15, [12] channel opens nobody serves, [13] last SCM x0, [14] its x1,
// [15] votes: lcx index | lmx index << 8 | RSC done interrupts << 16
extern "C" {volatile int32_t native_adsp_stats[16]; char native_adsp_text[96];}

namespace {
enum Stage {
    StMapped = 1, StSurveyed = 2, StFirmware = 3, StSmp2p = 4, StDoorbells = 5, StLoadState = 6, StRails = 7, StDtb = 8, StLoaded = 9,
    StStarted = 10, StReady = 11, StGlink = 12, StChannel = 13,
};
// houji (SM8650, pineapple FDT): IPCC moved to 0x406000; the ADSP carveouts are 0x9df00000 and 0x9de80000; the rest is unchanged.
constexpr uint64_t kSmem = 0x81d00000, kSmemSize = 0x200000, kMutex = 0x1f40000 + 3 * 0x1000, kIpcc = 0x406000, kAoss = 0xc300000;
constexpr uint64_t kRscDrv = 0x17a20000, kRscTcs = 0xd00, kCmdDb = 0x81c60000, kCmdDbSize = 0x20000;
constexpr uint64_t kAdspBase = 0x9df00000, kAdspSize = 0x4680000, kDtbBase = 0x9de80000, kDtbSize = 0x80000;
constexpr uint32_t kPasAdsp = 1, kPasDtb = 36, kRemotePid = 2, kItemIn = 443, kItemOut = 429, kItemCrash = 423;
constexpr uint32_t kItemGlinkDesc = 478, kItemGlinkTx = 479, kItemGlinkRx = 480, kGlinkFifo = 0x4000;
constexpr uint32_t kSpiIpcc = 229, kSpiRsc = 5;
constexpr uint32_t kFatal = 1u << 0, kReady = 1u << 1, kHandover = 1u << 2;
constexpr uint32_t kEvSmp2p = 1, kEvGlink = 2, kEvAop = 4, kEvRsc = 8, kEvSend = 16, kEvMSmp2p = 32, kEvMGlink = 64;
constexpr unsigned kClients = 4, kQueue = 8;

struct Window {IOMemoryMap *map; volatile uint8_t *va;};
bool mapWindow(Window &w, uint64_t base, uint64_t size, IOOptionBits cache = kIOMapInhibitCache) {
    IODeviceMemory *memory = IODeviceMemory::withRange(base, size);
    w.map = memory ? memory->map(kIOMapAnywhere | cache) : nullptr;
    if (memory) memory->release();
    w.va = w.map ? reinterpret_cast<volatile uint8_t *>(w.map->getVirtualAddress()) : nullptr;
    return w.va != nullptr;
}
void unmap(Window &w) {if (w.map) w.map->release(); w = {};}
uint32_t rd(const Window &w, uint64_t at) {return *reinterpret_cast<volatile uint32_t *>(w.va + at);}
void wr(const Window &w, uint64_t at, uint32_t value) {*reinterpret_cast<volatile uint32_t *>(w.va + at) = value; __asm__ volatile("dsb sy" ::: "memory");}
void fail(int step, int error) {native_adsp_stats[1] = error; native_adsp_stats[0] = -step;}
void stage(int value) {native_adsp_stats[0] = value;}
void note(const char *prefix, const char *text) {
    size_t at = 0;
    for (; prefix[at] && at < sizeof native_adsp_text - 1; ++at) native_adsp_text[at] = prefix[at];
    for (size_t i = 0; text[i] && at < sizeof native_adsp_text - 1; ++i) native_adsp_text[at++] = text[i];
    native_adsp_text[at] = 0;
}

Window gSmemW, gMutex, gIpcc, gAoss, gRsc;
QcomSmem gSmem;
QcomSmemPartition gPart;
QcomSmp2p gP2p;
int gStopEntry = -1;
QcomQmp gQmp;
QcomGlink gGlink;
bool gGlinkStarted;
QcomRscLayout gLayout;
uint32_t gRscIrqBits;
volatile uint32_t gPending, gRscDone;

// ---- clients ----
struct Message {uint32_t length; uint8_t data[SHENG_ADSP_MAX_MESSAGE];};
}  // namespace
extern "C" {volatile int32_t native_adsp_audio[8];}
struct ShengAdspChannel {
    char name[QCOM_GLINK_NAME];
    uint32_t intent_size, intent_count;
    int initiator;
    ShengAdspClient client;
    void *context;
    uint8_t *storage;
    QcomGlinkChannel *glink;
    Message *queue;  // kQueue slots
    uint32_t head, count;
};
namespace {
ShengAdspChannel gClients[kClients];
uint32_t gClientCount;
IOLock *gClientLock;

void wake(uint32_t bits) {
    __atomic_fetch_or(&gPending, bits, __ATOMIC_RELEASE);
    thread_wakeup((event_t)&gPending);
}

// ---- interrupts: acknowledge, record, wake ----
void ipccIrq(void *) {
    uint32_t bits = 0;
    for (unsigned n = 0; n < 32; ++n) {
        const uint32_t id = rd(gIpcc, QCOM_IPCC_RECV_ID);
        if (id == QCOM_IPCC_NONE) break;
        wr(gIpcc, QCOM_IPCC_RECV_CLEAR, id);
        if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P)) bits |= kEvSmp2p;
        else if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP)) bits |= kEvGlink;
        else if (id == qcom_ipcc_id(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP)) bits |= kEvAop;
        else if (id == qcom_ipcc_id(2, QCOM_IPCC_SIGNAL_SMP2P)) bits |= kEvMSmp2p;     // MPSS
        else if (id == qcom_ipcc_id(2, QCOM_IPCC_SIGNAL_GLINK_QMP)) bits |= kEvMGlink;
    }
    native_adsp_stats[2] = native_adsp_stats[2] + 1;
    if (bits) wake(bits);
}
void rscIrq(void *) {
    const uint32_t status = rd(gRsc, kRscTcs + gLayout.irq_status) & gRscIrqBits;
    if (!status) return;
    wr(gRsc, kRscTcs + gLayout.irq_clear, status);
    __atomic_fetch_or(&gRscDone, status, __ATOMIC_RELEASE);
    native_adsp_stats[15] = native_adsp_stats[15] + (1 << 16);
    wake(kEvRsc);
}

// Waits for an event up to ms milliseconds; returns the event bits (0: the deadline passed).
uint32_t waitEvents(uint32_t ms) {
    uint32_t bits = __atomic_exchange_n(&gPending, 0, __ATOMIC_ACQ_REL);
    if (bits) return bits;
    assert_wait_timeout((event_t)&gPending, THREAD_UNINT, ms, kMillisecondScale);
    bits = __atomic_exchange_n(&gPending, 0, __ATOMIC_ACQ_REL);
    if (bits) {clear_wait(current_thread(), THREAD_AWAKENED); return bits;}
    thread_block(THREAD_CONTINUE_NULL);
    return __atomic_exchange_n(&gPending, 0, __ATOMIC_ACQ_REL);
}

void ring(uint32_t client, uint32_t signal) {wr(gIpcc, QCOM_IPCC_SEND, qcom_ipcc_id(client, signal));}
void kickAop(void *) {ring(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP);}
void kickGlink(void *) {ring(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP);}
void kickSmp2p() {ring(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P);}

// ---- SMEM helpers (allocation under the TCSR hardware mutex) ----
bool lockSmem() {
    // The mutex is held only for the few writes of one allocation by whichever processor has it; it is tried a bounded number of times.
    for (unsigned i = 0; i < 1000; ++i) {
        wr(gMutex, 0, 1);
        if (rd(gMutex, 0) == 1) return true;
        IODelay(10);
    }
    return false;
}
void unlockSmem() {wr(gMutex, 0, 0);}
int allocItem(uint16_t item, uint32_t size) {
    if (!lockSmem()) return -16;
    const int rc = qcom_smem_alloc(&gSmem, &gPart, item, size);
    unlockSmem();
    return rc == QcomSmemExists ? 0 : rc;
}
volatile uint8_t *item(uint16_t id, uint32_t *size) {
    uint32_t at = 0, bytes = 0;
    if (qcom_smem_find(&gSmem, &gPart, id, &at, &bytes)) return nullptr;
    if (size) *size = bytes;
    return gSmemW.va + at;
}

void readCrashReason() {
    QcomSmemPartition global;
    uint32_t at = 0, size = 0;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global) ||
        qcom_smem_find(&gSmem, &global, kItemCrash, &at, &size) || !size)
        return;
    char text[80];
    size_t n = 0;
    for (; n < sizeof text - 1 && n < size; ++n) {const char c = (char)gSmemW.va[at + n]; if (!c) break; text[n] = c >= 32 && c < 127 ? c : '.';}
    text[n] = 0;
    if (n) note("crash: ", text);
}

// ---- the survey (sheng-adsp=1 and the first step of 2): reads only ----
void survey() {
    int32_t bits = 0;
    if (!qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, kRemotePid, &gPart)) {
        bits |= 1 << 8;
        const uint16_t ids[5] = {kItemIn, kItemOut, kItemGlinkDesc, kItemGlinkTx, kItemGlinkRx};
        for (unsigned i = 0; i < 5; ++i) if (item(ids[i], nullptr)) bits |= 1 << i;
    }
    QcomSmemPartition global;
    if (!qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global)) bits |= 1 << 9;
    native_adsp_stats[4] = bits;
    native_adsp_stats[3] = -1;
    if (volatile uint8_t *in = item(kItemIn, nullptr)) {
        QcomSmp2p view = {};
        view.in = reinterpret_cast<volatile uint32_t *>(in);
        view.local_pid = 0; view.remote_pid = kRemotePid;
        const int index = qcom_smp2p_check_in(view.in, 0, kRemotePid) ? -1 : qcom_smp2p_find_in(&view, "slave-kernel");
        if (index >= 0) native_adsp_stats[3] = (int32_t)qcom_smp2p_in_value(&view, index);
    }
    const uint32_t up = 0xffff;
    native_adsp_stats[5] = (rd(gAoss, 0) == 0x4d41494cu ? 1 : 0) << 12 | (rd(gAoss, 0xc) == up) << 8 | (rd(gAoss, 0x24) == up) << 9 |
                           (rd(gAoss, 0x14) == up) << 10 | (rd(gAoss, 0x2c) == up) << 11;
    native_adsp_stats[6] = (int32_t)rd(gRsc, 0);
    readCrashReason();
}

void service(uint32_t bits);

// ---- RPMh proxy votes ----
bool readCmdDb(uint8_t *copy) {
    Window db = {};
    if (!mapWindow(db, kCmdDb, kCmdDbSize)) return false;
    for (uint64_t i = 0; i < kCmdDbSize; i += 4) {const uint32_t w = rd(db, i); memcpy(copy + i, &w, 4);}
    unmap(db);
    return true;
}
QcomRpmhResource gLcx, gLmx;
int gTcs = -1;

// Votes both rails to the given ARC index pair through active TCS 1 and waits up to 100 ms for its done interrupt. The active TCSs are 0
// to 2 (tcs-config lists the active type first); the WiFi bring-up sends through TCS 0, so the two never share one. A request whose done
// interrupt is not seen still counts (bit 15 of [15]): the WiFi rails show such requests take effect.
int vote(uint32_t lcx, uint32_t lmx) {
    if (gTcs < 0) {
        gTcs = 1;
        gRscIrqBits = 1u << gTcs;
        wr(gRsc, kRscTcs + 0, rd(gRsc, kRscTcs + 0) | gRscIrqBits);  // IRQ_ENABLE for our TCS
    }
    const uint32_t addresses[2] = {gLcx.address, gLmx.address}, values[2] = {lcx, lmx};
    QcomRpmhWrite writes[16];
    const unsigned n = qcom_rpmh_active_request(&gLayout, (unsigned)gTcs, addresses, values, 2, writes, 16);
    if (!n) return -22;
    __atomic_store_n(&gRscDone, 0, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < n; ++i) wr(gRsc, kRscTcs + writes[i].offset, writes[i].value);
    uint64_t deadline;
    clock_interval_to_deadline(100, kMillisecondScale, &deadline);
    while (!(__atomic_load_n(&gRscDone, __ATOMIC_ACQUIRE) & gRscIrqBits) && mach_absolute_time() < deadline) service(waitEvents(20));
    bool done = (__atomic_load_n(&gRscDone, __ATOMIC_ACQUIRE) & gRscIrqBits) != 0;
    if (!done && (rd(gRsc, kRscTcs + gLayout.irq_status) & gRscIrqBits)) done = true;  // one look in case the line is not ours
    const unsigned m = qcom_rpmh_finish(&gLayout, (unsigned)gTcs, writes, 16);
    for (unsigned i = 0; i < m; ++i) wr(gRsc, kRscTcs + writes[i].offset, writes[i].value);
    native_adsp_stats[15] = (native_adsp_stats[15] & ~0xffff) | (int32_t)(lcx | lmx << 8 | (done ? 0 : 1u << 15));
    return 0;
}

// ---- secure world calls ----
struct ScmResult {int64_t x0; uint64_t x1;};
ScmResult scm(uint32_t command, uint32_t arginfo, uint64_t a, uint64_t b, uint64_t c) {
    ScmResult result = {-1, 0};
    for (unsigned busy = 0; busy < 20; ++busy) {
        uint64_t function = qcom_scm_fn(QCOM_SCM_SVC_PIL, command), session = 0;
        unsigned resumes = 0;
        for (;;) {
            register uint64_t x0 __asm__("x0") = function, x1 __asm__("x1") = arginfo, x2 __asm__("x2") = a, x3 __asm__("x3") = b,
                              x4 __asm__("x4") = c, x5 __asm__("x5") = 0, x6 __asm__("x6") = session, x7 __asm__("x7") = 0;
            __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                             :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
            result = {(int64_t)x0, x1};
            if ((int64_t)x0 != QCOM_SCM_INTERRUPTED || ++resumes > 100000) break;
            function = QCOM_SCM_INTERRUPTED;  // resume the interrupted call in its session
            session = x6;
        }
        if (result.x0 != QCOM_SCM_V2_EBUSY) break;
        IOSleep(30);
    }
    native_adsp_stats[13] = (int32_t)result.x0;
    native_adsp_stats[14] = (int32_t)result.x1;
    return result;
}
bool scmOk(const ScmResult &r) {return r.x0 == 0 && r.x1 == 0;}

void cleanToPoc(const void *start, size_t length) {
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    const uintptr_t line = static_cast<uintptr_t>(4) << ((ctr >> 16) & 15);
    for (uintptr_t a = reinterpret_cast<uintptr_t>(start) & ~(line - 1); a < reinterpret_cast<uintptr_t>(start) + length; a += line)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

// ---- a firmware image: parsed from its file, metadata handed over, segments written into the carveout ----
struct Image {  // plain data, zero at start: the two live in static storage (a few KiB of header table each, kept off the stack)
    void *file;
    uint64_t size;
    QcomMdtImage mdt;
    IOBufferMemoryDescriptor *meta;
};
Image gDtbImage, gAdspImage;
int fileRead(void *context, uint64_t offset, void *to, size_t length) {return sheng_fw_read(context, offset, to, length);}
void release(Image &image) {
    if (image.file) sheng_fw_close(image.file);
    if (image.meta) image.meta->release();
    image.file = nullptr; image.meta = nullptr;
}
int openImage(Image &image, const char *path) {
    int rc = sheng_fw_open(path, &image.file, &image.size);
    if (rc) {image.file = nullptr; return rc;}
    rc = qcom_mdt_parse(&image.mdt, image.size, fileRead, image.file);
    return rc ? 1000 - rc : 0;
}
// init_image with the metadata in physically contiguous memory, then the memory setup of a relocatable image.
int authenticateHeader(Image &image, uint32_t pas, uint64_t base) {
    const uint64_t bytes = qcom_mdt_metadata_size(&image.mdt);
    image.meta = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, (IOOptionBits)kIODirectionInOut | (IOOptionBits)kIOMemoryPhysicallyContiguous, bytes,
                                                                 0xfffffffffffff000ull);
    if (!image.meta || image.meta->prepare() != kIOReturnSuccess) return -12;
    uint8_t *va = static_cast<uint8_t *>(image.meta->getBytesNoCopy());
    if (qcom_mdt_metadata(&image.mdt, fileRead, image.file, va, bytes)) return -5;
    cleanToPoc(va, bytes);
    const uint64_t phys = image.meta->getPhysicalSegment(0, nullptr, kIOMemoryMapperNone);
    if (!scmOk(scm(QCOM_SCM_PAS_INIT_IMAGE, qcom_scm_args(2, QCOM_SCM_VAL, QCOM_SCM_RW, 0), pas, phys, 0))) return -1;
    if (image.mdt.relocatable &&
        !scmOk(scm(QCOM_SCM_PAS_MEM_SETUP, qcom_scm_args(3, 0, 0, 0), pas, base, qcom_mdt_span(&image.mdt))))
        return -2;
    return 0;
}
int loadSegments(Image &image, uint64_t base, uint64_t size) {
    Window region = {};
    if (!mapWindow(region, base, size, kIOMapWriteCombineCache)) return -12;
    constexpr size_t kChunk = 256 * 1024;
    uint8_t *bounce = static_cast<uint8_t *>(IOMallocData(kChunk));
    int rc = bounce ? 0 : -12;
    for (unsigned i = 0; !rc && i < image.mdt.count; ++i) {
        const QcomMdtSegment &s = image.mdt.segment[i];
        uint64_t at = 0;
        if (!s.load) continue;
        if (qcom_mdt_place(&image.mdt, i, base, size, &at)) {rc = -7; break;}
        uint8_t *to = const_cast<uint8_t *>(region.va) + at;
        for (uint64_t done = 0; !rc && done < s.file_size;) {
            const size_t n = s.file_size - done < kChunk ? (size_t)(s.file_size - done) : kChunk;
            if (sheng_fw_read(image.file, s.file_offset + done, bounce, n)) {rc = -5; break;}
            memcpy(to + done, bounce, n);
            done += n;
        }
        if (!rc && s.memory_size > s.file_size) bzero(to + s.file_size, (size_t)(s.memory_size - s.file_size));
    }
    __asm__ volatile("dsb sy" ::: "memory");
    if (bounce) IOFreeData(bounce, kChunk);
    unmap(region);
    return rc;
}

// ---- events ----
uint32_t gSlave;
bool gHandedOver;
void serviceSmp2p() {
    if (!gP2p.in) {
        if (volatile uint8_t *in = item(kItemIn, nullptr)) gP2p.in = reinterpret_cast<volatile uint32_t *>(in);
        else return;
    }
    uint32_t value = 0, changed = 0;
    if (qcom_smp2p_service(&gP2p, "slave-kernel", &value, &changed)) kickSmp2p();
    gSlave = value;
    native_adsp_stats[3] = (int32_t)value;
    if ((changed & value & kFatal)) readCrashReason();
    if ((value & kHandover) && !gHandedOver) {
        gHandedOver = true;
        vote(0, 0);  // the DSP holds its own votes now
    }
}

void applyClients();
void pumpSends();
void serviceGlink() {
    if (!gGlinkStarted) return;
    if (!gGlink.rx.data) {
        uint32_t size = 0;
        if (volatile uint8_t *rx = item(kItemGlinkRx, &size)) qcom_glink_set_rx(&gGlink, rx, size);
    }
    qcom_glink_poll(&gGlink);
    pumpSends();
    uint32_t up = 0;
    for (unsigned i = 0; i < gClientCount; ++i) if (gClients[i].glink && gClients[i].glink->up) up |= 1u << i;
    native_adsp_stats[9] = (int32_t)gGlink.rx_messages;
    native_adsp_stats[10] = (int32_t)gGlink.tx_messages;
    native_adsp_stats[11] = (int32_t)(up | (uint32_t)gGlink.version_agreed << 14 | (uint32_t)gGlink.broken << 15);
    native_adsp_stats[12] = (int32_t)gGlink.unknown_opens;
    if (gGlink.unknown_opens && strncmp(native_adsp_text, "crash: ", 7)) note("open: ", gGlink.unknown_names);
    if (gGlink.version_agreed && native_adsp_stats[0] >= StReady && native_adsp_stats[0] < StGlink) stage(StGlink);
    if (up && native_adsp_stats[0] >= StGlink) stage(StChannel);
}
void serviceModem(uint32_t bits);
void service(uint32_t bits) {
    serviceModem(bits);
    if (bits & kEvAop) qcom_qmp_step(&gQmp);
    if (bits & (kEvSmp2p | kEvGlink)) serviceSmp2p();
    if (bits & (kEvGlink | kEvSend)) {applyClients(); serviceGlink();}
}
// Runs events until done() holds or ms pass; returns done().
template <typename F> bool waitUntil(uint32_t ms, F done) {
    uint64_t deadline;
    clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
    while (!done()) {
        const uint64_t now = mach_absolute_time();
        if (now >= deadline) return done();
        uint64_t left = 0;
        absolutetime_to_nanoseconds(deadline - now, &left);
        service(waitEvents((uint32_t)(left / 1000000) + 1));
    }
    return true;
}

// ---- client glue (DSP thread side) ----
void clientState(void *context, QcomGlinkChannel *, int up) {
    ShengAdspChannel *c = static_cast<ShengAdspChannel *>(context);
    if (c->client.state) c->client.state(c->context, up);
}
void clientReceive(void *context, QcomGlinkChannel *, const uint8_t *data, uint32_t length) {
    ShengAdspChannel *c = static_cast<ShengAdspChannel *>(context);
    if (c->client.receive) c->client.receive(c->context, data, length);
}
void applyClients() {
    if (!gGlinkStarted) return;
    const QcomGlinkClient glue = {clientState, clientReceive};
    IOLockLock(gClientLock);
    for (unsigned i = 0; i < gClientCount; ++i) {
        ShengAdspChannel &c = gClients[i];
        if (!c.glink) c.glink = qcom_glink_channel(&gGlink, c.name, c.intent_size, c.intent_count, c.storage, &glue, &c, c.initiator);
    }
    IOLockUnlock(gClientLock);
}
// The audio channel's send state, for the log: [0] sent, [1] deferred (no intent or FIFO room), [2] refused otherwise, [3] queued now,
// [4] intent asked (0/1), [5] valid remote intents, [6] the largest remote intent, [7] the last deferred length
void noteChannel(const ShengAdspChannel &c, int rc, uint32_t length) {
    if (strcmp(c.name, "adsp_apps")) return;
    if (rc == QcomGlinkOk) native_adsp_audio[0] = native_adsp_audio[0] + 1;
    else if (rc == QcomGlinkAgain) {native_adsp_audio[1] = native_adsp_audio[1] + 1; native_adsp_audio[7] = (int32_t)length;}
    else native_adsp_audio[2] = native_adsp_audio[2] + 1;
    native_adsp_audio[3] = (int32_t)c.count;
    native_adsp_audio[4] = c.glink->intent_asked;
    int32_t valid = 0, largest = 0;
    for (unsigned k = 0; k < QCOM_GLINK_MAX_REMOTE_INTENTS; ++k)
        if (c.glink->remote[k].valid) {++valid; if ((int32_t)c.glink->remote[k].size > largest) largest = (int32_t)c.glink->remote[k].size;}
    native_adsp_audio[5] = valid;
    native_adsp_audio[6] = largest;
}
void pumpSends() {
    for (unsigned i = 0; i < gClientCount; ++i) {
        ShengAdspChannel &c = gClients[i];
        if (!c.glink || !c.glink->up) continue;
        for (;;) {
            IOLockLock(gClientLock);
            if (!c.count) {IOLockUnlock(gClientLock); break;}
            Message *m = &c.queue[c.head];
            IOLockUnlock(gClientLock);
            // Only this thread takes messages off the queue, so m stays valid while the lock is dropped for the send.
            const int rc = qcom_glink_send(&gGlink, c.glink, m->data, m->length);
            noteChannel(c, rc, m->length);
            if (rc != QcomGlinkOk) break;
            IOLockLock(gClientLock);
            c.head = (c.head + 1) % kQueue;
            c.count--;
            IOLockUnlock(gClientLock);
        }
    }
}

// ---- the bring-up ----
bool gIrq;
bool attachIrqs() {
    int rc = 0;
    for (unsigned tries = 0;; ++tries) {  // the GIC driver may not have started yet (as ButtonProbe found)
        rc = armcpu_spi_attach(32 + kSpiIpcc, false, ipccIrq, nullptr, nullptr, nullptr);
        if (rc != kIOReturnNotReady || tries == 240) break;
        IOSleep(250);
    }
    if (rc) {fail(StDoorbells, rc); return false;}
    if (armcpu_spi_attach(32 + kSpiRsc, false, rscIrq, nullptr, nullptr, nullptr)) native_adsp_stats[15] |= 0x80 << 16;
    const uint32_t enable[3] = {qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_SMP2P),
                                qcom_ipcc_id(QCOM_IPCC_CLIENT_LPASS, QCOM_IPCC_SIGNAL_GLINK_QMP),
                                qcom_ipcc_id(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP)};
    for (uint32_t id : enable) wr(gIpcc, QCOM_IPCC_RECV_ENABLE, id);
    gIrq = true;
    return true;
}

// The firmware comes from the device's own firmware partition ("modem:", read in place) or, with sheng-adsp-fw=DIR, from DIR/adsp.mbn
// and DIR/adsp_dtb.mbn on a mounted volume. Without the argument the partition is tried first, then the two directories files may be
// installed in. The UFS driver and the Data volume may not be up when this thread starts: a few tries with growing pauses (about two
// minutes in all), not a poll. source gets the source that opened ("modem:" or a directory).
int openPair(Image &dtb, Image &adsp, const char *source, char *path, size_t size) {
    const bool modem = !strcmp(source, "modem:");
    snprintf(path, size, modem ? "%sadsp_dtb" : "%s/adsp_dtb.mbn", source);
    int rc = openImage(dtb, path);
    if (rc) return rc;
    snprintf(path, size, modem ? "%sadsp" : "%s/adsp.mbn", source);
    if ((rc = openImage(adsp, path))) release(dtb);
    return rc;
}
int openFirmware(Image &dtb, Image &adsp, char *path, size_t size, const char *chosen) {
    static const uint32_t pauses[] = {1, 2, 4, 8, 16, 30, 30, 30};
    const char *sources[3] = {"modem:", "/private/var/firmware/qcom/sm8650/houji", "/private/var/ngpu/fw"};
    const unsigned count = chosen ? 1 : 3;
    if (chosen) sources[0] = chosen;
    int rc = 0;
    for (unsigned i = 0;; ++i) {
        int first = 0;
        for (unsigned s = 0; s < count; ++s) {
            rc = openPair(dtb, adsp, sources[s], path, size);
            if (!rc) return 0;
            if (!s) first = rc;
            if (rc != ENOENT && rc != ENXIO && rc != EIO) return rc;  // found but unusable: no other source is tried
        }
        rc = first;  // the reason the first source failed is the one worth reporting
        if (i == sizeof pauses / sizeof pauses[0]) return rc;
        IOSleep(pauses[i] * 1000);
    }
}

void shutdownDsp(bool dtb, bool adsp) {
    if (adsp) scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPasAdsp, 0, 0);
    if (dtb) scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPasDtb, 0, 0);
}

bool start() {
    char dir[80] = "";
    const bool chosen = PE_parse_boot_argn("houji_adsp_fw", dir, sizeof dir) && dir[0];
    char path[112];
    Image &dtb = gDtbImage, &adsp = gAdspImage;
    int rc = openFirmware(dtb, adsp, path, sizeof path, chosen ? dir : nullptr);
    if (rc) {note("fw: ", path); fail(StFirmware, rc); return false;}
    native_adsp_stats[7] = (int32_t)(adsp.size >> 10);
    stage(StFirmware);
    if (native_adsp_stats[3] > 0) {fail(StFirmware, -114); release(dtb); release(adsp); return false;}  // running already: left alone

    // Our SMP2P item, before the DSP starts (it looks for it at boot).
    if ((rc = allocItem(kItemOut, QCOM_SMP2P_ITEM_BYTES))) {fail(StSmp2p, rc); release(dtb); release(adsp); return false;}
    qcom_smp2p_init_out(&gP2p, reinterpret_cast<volatile uint32_t *>(item(kItemOut, nullptr)), 0, kRemotePid);
    gStopEntry = qcom_smp2p_add_entry(&gP2p, "master-kernel");
    stage(StSmp2p);
    if (!attachIrqs()) {release(dtb); release(adsp); return false;}
    kickSmp2p();
    stage(StDoorbells);

    qcom_qmp_init(&gQmp, reinterpret_cast<volatile uint32_t *>(gAoss.va), 0x400, kickAop, nullptr);
    for (int phase = 0; phase < 4 && qcom_qmp_step(&gQmp) > 0 && gQmp.state != QcomQmpOpen; ++phase)
        waitUntil(1000, [] {return qcom_qmp_step(&gQmp) == QcomQmpOpen || gQmp.state < 0;});
    native_adsp_stats[5] = (native_adsp_stats[5] & ~0xff) | (gQmp.state & 0xff);
    if (gQmp.state != QcomQmpOpen) {fail(StLoadState, gQmp.state); release(dtb); release(adsp); return false;}
    qcom_qmp_send(&gQmp, "{class: image, res: load_state, name: adsp, val: on}");
    if (!waitUntil(1000, [] {return qcom_qmp_step(&gQmp) == QcomQmpOpen;})) {
        qcom_qmp_abandon(&gQmp);
        fail(StLoadState, -60); release(dtb); release(adsp); return false;
    }
    stage(StLoadState);

    // A copy of the command database, held only while the levels are looked up (a static copy would sit in the kernel's __DATA).
    struct CmdDbCopy {uint8_t *bytes = static_cast<uint8_t *>(IOMallocData(kCmdDbSize)); ~CmdDbCopy() {if (bytes) IOFreeData(bytes, kCmdDbSize);}} copy;
    uint8_t *const db = copy.bytes;
    uint32_t lcx = 0, lmx = 0;
    if (!db || !readCmdDb(db) || qcom_cmd_db_find(db, kCmdDbSize, "lcx.lvl", &gLcx) || qcom_cmd_db_find(db, kCmdDbSize, "lmx.lvl", &gLmx) ||
        qcom_rpmh_level_index(&gLcx, 0xffffffffu, &lcx) || qcom_rpmh_level_index(&gLmx, 0xffffffffu, &lmx)) {
        fail(StRails, -2); release(dtb); release(adsp); return false;
    }
    if ((rc = vote(lcx, lmx))) {fail(StRails, rc); release(dtb); release(adsp); return false;}
    stage(StRails);

    // The DSP's devicetree image first, as its own authenticated image, then the DSP image.
    if ((rc = authenticateHeader(dtb, kPasDtb, kDtbBase)) || (rc = loadSegments(dtb, kDtbBase, kDtbSize)) ||
        (rc = scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kPasDtb, 0, 0)) ? 0 : -3)) {
        fail(StDtb, rc); vote(0, 0); release(dtb); release(adsp); return false;
    }
    stage(StDtb);
    if ((rc = authenticateHeader(adsp, kPasAdsp, kAdspBase)) || (rc = loadSegments(adsp, kAdspBase, kAdspSize))) {
        fail(StLoaded, rc); shutdownDsp(true, false); vote(0, 0); release(dtb); release(adsp); return false;
    }
    stage(StLoaded);
    const uint64_t began = mach_absolute_time();
    if (!scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kPasAdsp, 0, 0))) {
        fail(StStarted, -3); shutdownDsp(true, false); vote(0, 0); release(dtb); release(adsp); return false;
    }
    stage(StStarted);
    const bool ready = waitUntil(5000, [] {return (gSlave & (kReady | kFatal)) != 0;});
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns);
    native_adsp_stats[8] = (int32_t)(ns / 1000000);
    release(dtb);
    release(adsp);  // the metadata may go once the images are authenticated
    if (!ready || (gSlave & kFatal)) {
        readCrashReason();
        fail(StReady, ready ? -5 : -60);
        shutdownDsp(true, true);
        if (!gHandedOver) vote(0, 0);
        return false;
    }
    stage(StReady);
    return true;
}

bool startGlink() {
    int rc = allocItem(kItemGlinkDesc, 32);
    if (!rc) rc = allocItem(kItemGlinkTx, kGlinkFifo);
    uint32_t size = 0;
    volatile uint8_t *desc = item(kItemGlinkDesc, nullptr), *txf = item(kItemGlinkTx, &size);
    if (rc || !desc || !txf) {fail(StGlink, rc ? rc : -2); return false;}
    volatile uint32_t *d = reinterpret_cast<volatile uint32_t *>(desc);
    // Descriptor words: our FIFO's tail and head, then the DSP's FIFO's tail and head.
    const QcomGlinkFifo tx = {&d[1], &d[0], txf, size}, rx = {&d[3], &d[2], nullptr, 0};
    qcom_glink_init(&gGlink, tx, rx, kickGlink, nullptr);
    gGlinkStarted = true;
    applyClients();
    serviceGlink();
    return true;
}

}  // namespace

// ==== the modem (MPSS), for GNSS: houji_modem=2 starts it (1, the default, only surveys its image) ====
// Facts (handset devicetree, mainline sm8650.dtsi and qcom_q6v5_pas.c sm8650_mpss_resource): PAS 4 for modem.mdt in mpss 0x8bc00000 +
// 0xf400000, PAS 0x26 for modem_dtb.mdt in q6_mpss_dtb 0x9b000000 + 0x80000; proxy rails cx and mss; AOSS load_state "modem"; crash reason
// SMEM item 421; SMP2P items 435 (in) and 428 (out), remote pid 1; IPCC client 2 (MPSS) signal 0 GLINK, 2 SMP2P; GLINK items 478-480 in
// the apps/modem partition. Before the start the DSM regions (0x86b00000 + 0x4900000, 0x8b400000 + 0x800000) and qlink logging
// (0x84800000 + 0x200000) go to the modem's MSA VM (SCM MP assign, HLOS 3 -> MSS_MSA 15). rmtfs: the modem's EFS partitions (modemst1,
// modemst2, fsg, fsc) are read once into RAM and served over QMI (service 14) through a buffer at 0xd7c00000 + 0x400000 (mainline's
// rmtfs_mem; outside both the stock reservations' users and this UEFI's memory map) shared HLOS + MSS_MSA. Writes stay in RAM.
// [0] stage, [1] error, [2] slave bits, [3] ready ms, [4] GLINK in, [5] out, [6] QRTR services the modem announced, [7] rmtfs requests,
// [8] rmtfs opens, [9] rmtfs bytes read KiB, [10] written KiB, [11] last SCM x0 of an assign, [12] locator requests, [13] QRTR hello,
// [14] LOC (service 0x10) announced, [15] packets dropped
extern "C" {volatile int32_t native_mpss_stats[16]; char native_mpss_text[96];}
extern "C" int native_ufs_part_find(const char *name, uint32_t *lun, uint64_t *first, uint64_t *blocks);
extern "C" int native_ufs_part_read(uint32_t lun, uint64_t lba, uint32_t blocks, void *dst);
namespace {
enum MStage {MsImages = 1, MsSmp2p = 2, MsLoadState = 3, MsRails = 4, MsAssign = 5, MsRmtfs = 6, MsDtb = 7, MsLoaded = 8, MsStarted = 9,
             MsReady = 10, MsGlink = 11, MsQrtr = 12};
constexpr uint32_t kIpccMpss = 2, kMPid = 1, kMItemIn = 435, kMItemOut = 428, kMItemCrash = 421, kMPas = 4, kMPasDtb = 0x26;
constexpr uint64_t kMBase = 0x8bc00000, kMSize = 0xf400000, kMDtbBase = 0x9b000000, kMDtbSize = 0x80000;
constexpr uint64_t kRmtfsBase = 0xd7c00000, kRmtfsSize = 0x400000;
constexpr uint32_t kVmHlos = 3, kVmMssMsa = 15, kPermRw = 6;
volatile int32_t *const ms = native_mpss_stats;
void mfail(int step, int error) {ms[1] = error; ms[0] = -step;}
void mnote(const char *prefix, const char *text) {
    size_t at = 0;
    for (; prefix[at] && at < sizeof native_mpss_text - 1; ++at) native_mpss_text[at] = prefix[at];
    for (size_t i = 0; text[i] && at < sizeof native_mpss_text - 1; ++i) native_mpss_text[at++] = text[i];
    native_mpss_text[at] = 0;
}
QcomSmemPartition gMPart;
QcomSmp2p gMP2p;
QcomGlink gMGlink;
bool gMGlinkStarted, gMOn;
uint32_t gMSlave;
Image gMDtbImage, gMImage;
void kickMGlink(void *) {ring(kIpccMpss, QCOM_IPCC_SIGNAL_GLINK_QMP);}
void kickMSmp2p() {ring(kIpccMpss, QCOM_IPCC_SIGNAL_SMP2P);}
volatile uint8_t *mitem(uint16_t id, uint32_t *size) {
    uint32_t at = 0, bytes = 0;
    if (qcom_smem_find(&gSmem, &gMPart, id, &at, &bytes)) return nullptr;
    if (size) *size = bytes;
    return gSmemW.va + at;
}
int mallocItem(uint16_t item, uint32_t size) {
    if (!lockSmem()) return -16;
    const int rc = qcom_smem_alloc(&gSmem, &gMPart, item, size);
    unlockSmem();
    return rc == QcomSmemExists ? 0 : rc;
}
void readModemCrash() {
    QcomSmemPartition global;
    uint32_t at = 0, size = 0;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global) || qcom_smem_find(&gSmem, &global, kMItemCrash, &at, &size) || !size)
        return;
    char text[80];
    size_t n = 0;
    for (; n < sizeof text - 1 && n < size; ++n) {const char c = (char)gSmemW.va[at + n]; if (!c) break; text[n] = c >= 32 && c < 127 ? c : '.';}
    text[n] = 0;
    if (n) mnote("crash: ", text);
}

// SCM MP assign (svc 0x0c, cmd 0x16): seven arguments, the last four in a buffer whose address goes in x5 (SMCCC extended arguments).
int64_t scmAssign(uint64_t base, uint64_t size, const uint32_t *vmids, unsigned count) {
    IOBufferMemoryDescriptor *b = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        (IOOptionBits)kIODirectionInOut | (IOOptionBits)kIOMemoryPhysicallyContiguous, 4096, 0xfffffffffffff000ull);
    if (!b || b->prepare() != kIOReturnSuccess) {OSSafeReleaseNULL(b); return -12;}
    uint8_t *va = static_cast<uint8_t *>(b->getBytesNoCopy());
    bzero(va, 4096);
    const uint64_t pa = b->getPhysicalSegment(0, nullptr, kIOMemoryMapperNone);
    const uint32_t src = kVmHlos;
    memcpy(va, &src, 4);                                         // source VMs at +0
    memcpy(va + 64, &base, 8); memcpy(va + 72, &size, 8);        // the region at +64
    for (unsigned i = 0; i < count; ++i) {                       // destinations at +128: vmid, perm, ctx, ctx_size, unused
        const uint32_t vp[2] = {vmids[i], kPermRw};
        memcpy(va + 128 + 24 * i, vp, 8);
    }
    const uint64_t ext[4] = {4, pa + 128, 24ull * count, 0};      // args 3..6
    memcpy(va + 512, ext, sizeof ext);
    cleanToPoc(va, 4096);
    const uint32_t arginfo = 7u | 1u << 4 | 1u << 8 | 1u << 12;  // RO, VAL, RO, VAL, RO, VAL, VAL
    ScmResult r = {-1, 0};
    for (unsigned busy = 0; busy < 20; ++busy) {
        register uint64_t x0 __asm__("x0") = qcom_scm_fn(0x0c, 0x16), x1 __asm__("x1") = arginfo, x2 __asm__("x2") = pa + 64,
                          x3 __asm__("x3") = 16, x4 __asm__("x4") = pa, x5 __asm__("x5") = pa + 512, x6 __asm__("x6") = 0, x7 __asm__("x7") = 0;
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                         :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
        r = {(int64_t)x0, x1};
        if (r.x0 != QCOM_SCM_V2_EBUSY) break;
        IOSleep(30);
    }
    b->complete(); b->release();
    ms[11] = (int32_t)r.x0;
    return r.x0 ? r.x0 : (int64_t)r.x1;
}

// ---- rmtfs ----
struct RmtfsFile {const char *path, *part; uint8_t *data; uint32_t size; bool open;};
RmtfsFile gRmtfs[4] = {{"/boot/modem_fs1", "modemst1"}, {"/boot/modem_fs2", "modemst2"}, {"/boot/modem_fsg", "fsg"}, {"/boot/modem_fsc", "fsc"}};
Window gRmtfsMem;
QcomQrtr gMQrtr;
QcomGlinkChannel *gMChannel;
uint8_t *gMChannelStorage;
struct MPacket {uint32_t length; uint8_t data[1536];};
MPacket *gMQueue; uint32_t gMHead, gMCount;
bool loadRmtfs() {
    for (RmtfsFile &f : gRmtfs) {
        uint32_t lun = 0; uint64_t first = 0, blocks = 0;
        if (!native_ufs_part_find(f.part, &lun, &first, &blocks) || !blocks || blocks > 4096) return false;
        f.size = (uint32_t)(blocks * 4096);
        f.data = static_cast<uint8_t *>(IOMallocData(f.size));
        if (!f.data) return false;
        for (uint64_t done = 0; done < blocks;) {
            const uint32_t n = blocks - done > 32 ? 32 : (uint32_t)(blocks - done);
            if (!native_ufs_part_read(lun, first + done, n, f.data + done * 4096)) return false;
            done += n;
        }
    }
    return true;
}
uint32_t qmiResponse(uint8_t *out, const QcomQmiMessage &m, uint16_t result, uint16_t error) {
    out[0] = QCOM_QMI_RESPONSE;
    memcpy(out + 1, &m.transaction, 2); memcpy(out + 3, &m.message, 2);
    out[7] = 0x02; const uint16_t four = 4; memcpy(out + 8, &four, 2); memcpy(out + 10, &result, 2); memcpy(out + 12, &error, 2);
    const uint16_t len = 7; memcpy(out + 5, &len, 2);
    return 14;
}
uint32_t qmiAdd(uint8_t *out, uint32_t at, uint8_t type, const void *value, uint16_t size) {
    out[at] = type; memcpy(out + at + 1, &size, 2); memcpy(out + at + 3, value, size);
    uint16_t len; memcpy(&len, out + 5, 2); len = (uint16_t)(len + 3 + size); memcpy(out + 5, &len, 2);
    return at + 3 + size;
}
void rmtfsReceive(void *, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length) {
    QcomQmiMessage m;
    if (qcom_qmi_parse(data, length, &m) || m.type != QCOM_QMI_REQUEST) return;
    ms[7] = ms[7] + 1;
    uint8_t out[64];
    uint32_t n = 0, size = 0, caller = 0;
    const uint8_t *id = qcom_qmi_tlv(&m, 0x01, &size);
    if (m.message != 1 && id && size == 4) memcpy(&caller, id, 4);
    const bool known = caller < 4 && gRmtfs[caller].open;
    switch (m.message) {
    case 1: {   // open
        int found = -1;
        for (unsigned i = 0; id && i < 4; ++i)
            if (strlen(gRmtfs[i].path) == size && !memcmp(gRmtfs[i].path, id, size)) found = (int)i;
        char path[40] = {}; if (id) memcpy(path, id, size < 39 ? size : 39);
        mnote("rmtfs open ", path);
        if (found < 0) {n = qmiResponse(out, m, 1, 0x30); break;}
        gRmtfs[found].open = true; ms[8] = ms[8] + 1;
        n = qmiResponse(out, m, 0, 0);
        const uint32_t cid = (uint32_t)found; n = qmiAdd(out, n, 0x10, &cid, 4);
        break; }
    case 2: if (known) gRmtfs[caller].open = false; n = qmiResponse(out, m, known ? 0 : 1, known ? 0 : 0x30); break;
    case 3: {   // read or write: caller, direction, iovec[] {sector, buffer offset, sectors}, force sync
        uint32_t dsize = 0, vsize = 0;
        const uint8_t *dir = qcom_qmi_tlv(&m, 0x02, &dsize), *v = qcom_qmi_tlv(&m, 0x03, &vsize);
        bool ok = known && dir && dsize == 1 && v && vsize >= 1;
        uint32_t count = 0, skip = 1;
        if (ok) {count = v[0]; if (1 + 12 * count != vsize && vsize >= 4) {memcpy(&count, v, 4); skip = 4;} ok = skip + 12 * count == vsize;}
        for (uint32_t i = 0; ok && i < count; ++i) {
            uint32_t e[3]; memcpy(e, v + skip + 12 * i, 12);
            const uint64_t off = (uint64_t)e[0] * 512, bytes = (uint64_t)e[2] * 512;
            if (off + bytes > gRmtfs[caller].size || (uint64_t)e[1] + bytes > kRmtfsSize) {ok = false; break;}
            volatile uint8_t *shared = gRmtfsMem.va + e[1];
            if (dir[0] == 0) {for (uint64_t k = 0; k < bytes; ++k) shared[k] = gRmtfs[caller].data[off + k]; ms[9] = ms[9] + (int32_t)(bytes >> 10);}
            else {for (uint64_t k = 0; k < bytes; ++k) gRmtfs[caller].data[off + k] = shared[k]; ms[10] = ms[10] + (int32_t)(bytes >> 10);}
        }
        __asm__ volatile("dsb sy" ::: "memory");
        n = qmiResponse(out, m, ok ? 0 : 1, ok ? 0 : 0x30);
        break; }
    case 4: {   // the shared buffer
        n = qmiResponse(out, m, 0, 0);
        const uint64_t at = kRmtfsBase; n = qmiAdd(out, n, 0x10, &at, 8);
        break; }
    case 5: {n = qmiResponse(out, m, 0, 0); const uint8_t none = 0; n = qmiAdd(out, n, 0x10, &none, 1); break;}
    default: n = qmiResponse(out, m, 1, 0x47); break;   // invalid message
    }
    if (n) qcom_qrtr_send_data(q, port, node, from, out, n);
}
const char *const kMGps[] = {"gps/gps_service", nullptr};
const QcomPdDomain kMDomains[] = {{"msm/adsp/audio_pd", 74, nullptr}, {"msm/adsp/root_pd", 74, nullptr}, {"msm/adsp/charger_pd", 74, nullptr},
                                  {"msm/modem/root_pd", 180, kMGps}};
void mlocator(void *, QcomQrtr *q, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length) {
    uint8_t answer[480];
    ms[12] = ms[12] + 1;
    const uint32_t n = qcom_servloc_answer(kMDomains, sizeof kMDomains / sizeof kMDomains[0], data, length, answer, sizeof answer);
    if (n) qcom_qrtr_send_data(q, port, node, from, answer, n);
}
int mqrtrSend(void *, const uint8_t *packet, uint32_t length) {
    if (gMCount == 16 || length > sizeof gMQueue[0].data) {ms[15] = ms[15] + 1; return -1;}
    MPacket &p = gMQueue[(gMHead + gMCount) % 16];
    memcpy(p.data, packet, length); p.length = length; gMCount++;
    return 0;
}
void mpump() {
    while (gMCount && gMChannel && gMChannel->up) {
        MPacket &p = gMQueue[gMHead];
        if (qcom_glink_send(&gMGlink, gMChannel, p.data, p.length) != QcomGlinkOk) break;
        gMHead = (gMHead + 1) % 16; gMCount--;
    }
}
void mcounters() {
    ms[13] = gMQrtr.hello_received;
    ms[6] = (int32_t)gMQrtr.remote_count;
    ms[14] = qcom_qrtr_find_remote(&gMQrtr, 0x10) >= 0;
    unsigned at = 0;
    for (uint32_t i = 0; i < gMQrtr.remote_count && at + 12 < sizeof native_mpss_text; ++i)
        at += (unsigned)snprintf(native_mpss_text + at, sizeof native_mpss_text - at, "%s%x:%x", at ? " " : "", gMQrtr.remote[i].service,
                                 gMQrtr.remote[i].instance);
}
void mlinkState(void *, QcomGlinkChannel *, int up) {
    if (up) {gMQrtr.hello_sent = gMQrtr.hello_received = 0; gMQrtr.remote_count = 0; gMQrtr.remote_node = QCOM_QRTR_NODE_BROADCAST;
             qcom_qrtr_start(&gMQrtr); if (ms[0] >= MsGlink) ms[0] = MsQrtr;}
    mcounters();
}
void mlinkReceive(void *, QcomGlinkChannel *, const uint8_t *data, uint32_t length) {qcom_qrtr_receive(&gMQrtr, data, length); mcounters();}

void serviceModem(uint32_t bits) {
    if (!gMOn) return;
    if (bits & (kEvMSmp2p | kEvMGlink)) {
        if (!gMP2p.in) if (volatile uint8_t *in = mitem(kMItemIn, nullptr)) gMP2p.in = reinterpret_cast<volatile uint32_t *>(in);
        if (gMP2p.in) {
            uint32_t value = 0, changed = 0;
            if (qcom_smp2p_service(&gMP2p, "slave-kernel", &value, &changed)) kickMSmp2p();
            gMSlave = value; ms[2] = (int32_t)value;
            if (changed & value & kFatal) readModemCrash();
        }
    }
    if (gMGlinkStarted && (bits & (kEvMGlink | kEvSend))) {
        if (!gMGlink.rx.data) {uint32_t size = 0; if (volatile uint8_t *rx = mitem(kItemGlinkRx, &size)) qcom_glink_set_rx(&gMGlink, rx, size);}
        qcom_glink_poll(&gMGlink);
        mpump();
        qcom_glink_poll(&gMGlink);
        ms[4] = (int32_t)gMGlink.rx_messages; ms[5] = (int32_t)gMGlink.tx_messages;
    }
}

bool modemStart() {
    int rc;
    if ((rc = openImage(gMDtbImage, "modemfw:modem_dtb")) || (rc = openImage(gMImage, "modemfw:modem"))) {mfail(MsImages, rc); return false;}
    ms[0] = MsImages;
    if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, kMPid, &gMPart)) {mfail(MsSmp2p, -2); return false;}
    if ((rc = mallocItem(kMItemOut, QCOM_SMP2P_ITEM_BYTES))) {mfail(MsSmp2p, rc); return false;}
    qcom_smp2p_init_out(&gMP2p, reinterpret_cast<volatile uint32_t *>(mitem(kMItemOut, nullptr)), 0, kMPid);
    qcom_smp2p_add_entry(&gMP2p, "master-kernel");
    const uint32_t enable[2] = {qcom_ipcc_id(kIpccMpss, QCOM_IPCC_SIGNAL_SMP2P), qcom_ipcc_id(kIpccMpss, QCOM_IPCC_SIGNAL_GLINK_QMP)};
    for (uint32_t id : enable) wr(gIpcc, QCOM_IPCC_RECV_ENABLE, id);
    gMOn = true;
    kickMSmp2p();
    ms[0] = MsSmp2p;
    qcom_qmp_send(&gQmp, "{class: image, res: load_state, name: modem, val: on}");
    if (!waitUntil(1000, [] {return qcom_qmp_step(&gQmp) == QcomQmpOpen;})) {mfail(MsLoadState, -60); return false;}
    ms[0] = MsLoadState;
    {
        struct CmdDbCopy {uint8_t *bytes = static_cast<uint8_t *>(IOMallocData(kCmdDbSize)); ~CmdDbCopy() {if (bytes) IOFreeData(bytes, kCmdDbSize);}} copy;
        QcomRpmhResource cx, mss; uint32_t cxl = 0, mssl = 0;
        if (!copy.bytes || !readCmdDb(copy.bytes) || qcom_cmd_db_find(copy.bytes, kCmdDbSize, "cx.lvl", &cx) ||
            qcom_cmd_db_find(copy.bytes, kCmdDbSize, "mss.lvl", &mss) || qcom_rpmh_level_index(&cx, 0xffffffffu, &cxl) ||
            qcom_rpmh_level_index(&mss, 0xffffffffu, &mssl)) {mfail(MsRails, -2); return false;}
        const QcomRpmhResource lcx = gLcx, lmx = gLmx;
        gLcx = cx; gLmx = mss;
        rc = vote(cxl, mssl);
        gLcx = lcx; gLmx = lmx;
        if (rc) {mfail(MsRails, rc); return false;}
    }
    ms[0] = MsRails;
    static const uint64_t regions[3][2] = {{0x86b00000, 0x4900000}, {0x8b400000, 0x800000}, {0x84800000, 0x200000}};
    const uint32_t msa = kVmMssMsa, both[2] = {kVmHlos, kVmMssMsa};
    for (auto &r : regions) if (scmAssign(r[0], r[1], &msa, 1)) {mfail(MsAssign, (int)ms[11]); return false;}
    ms[0] = MsAssign;
    if (scmAssign(kRmtfsBase, kRmtfsSize, both, 2)) {mfail(MsRmtfs, -1); return false;}
    if (!mapWindow(gRmtfsMem, kRmtfsBase, kRmtfsSize) || !loadRmtfs()) {mfail(MsRmtfs, -2); return false;}
    for (uint64_t i = 0; i < kRmtfsSize; i += 4) *reinterpret_cast<volatile uint32_t *>(gRmtfsMem.va + i) = 0;
    gMQueue = static_cast<MPacket *>(IOMallocData(sizeof(MPacket) * 16));
    gMChannelStorage = static_cast<uint8_t *>(IOMallocData(4096 * 8));
    if (!gMQueue || !gMChannelStorage) {mfail(MsRmtfs, -12); return false;}
    qcom_qrtr_init(&gMQrtr, 1, mqrtrSend, nullptr);
    qcom_qrtr_add_service(&gMQrtr, QCOM_SERVLOC_SERVICE, QCOM_SERVLOC_INSTANCE, 1, mlocator, nullptr);
    qcom_qrtr_add_service(&gMQrtr, 14, 1, 2, rmtfsReceive, nullptr);
    ms[0] = MsRmtfs;
    if ((rc = authenticateHeader(gMDtbImage, kMPasDtb, kMDtbBase)) || (rc = loadSegments(gMDtbImage, kMDtbBase, kMDtbSize)) ||
        (rc = scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kMPasDtb, 0, 0)) ? 0 : -3)) {mfail(MsDtb, rc); return false;}
    ms[0] = MsDtb;
    if ((rc = authenticateHeader(gMImage, kMPas, kMBase)) || (rc = loadSegments(gMImage, kMBase, kMSize))) {mfail(MsLoaded, rc); return false;}
    ms[0] = MsLoaded;
    // GLINK items before the start: the modem looks for the descriptor when it comes up
    if ((rc = mallocItem(kItemGlinkDesc, 32)) || (rc = mallocItem(kItemGlinkTx, 0x4000))) {mfail(MsLoaded, 100 + rc); return false;}
    const uint64_t began = mach_absolute_time();
    if (!scmOk(scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kMPas, 0, 0))) {mfail(MsStarted, -3); return false;}
    ms[0] = MsStarted;
    const bool ready = waitUntil(20000, [] {return (gMSlave & (kReady | kFatal)) != 0;});
    uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns); ms[3] = (int32_t)(ns / 1000000);
    release(gMDtbImage); release(gMImage);
    if (!ready || (gMSlave & kFatal)) {readModemCrash(); mfail(MsReady, ready ? -5 : -60); return false;}
    ms[0] = MsReady;
    uint32_t size = 0;
    volatile uint8_t *desc = mitem(kItemGlinkDesc, nullptr), *txf = mitem(kItemGlinkTx, &size);
    if (!desc || !txf) {mfail(MsGlink, -2); return false;}
    volatile uint32_t *d = reinterpret_cast<volatile uint32_t *>(desc);
    const QcomGlinkFifo tx = {&d[1], &d[0], txf, size}, rx = {&d[3], &d[2], nullptr, 0};
    qcom_glink_init(&gMGlink, tx, rx, kickMGlink, nullptr);
    static const QcomGlinkClient glue = {mlinkState, mlinkReceive};
    gMChannel = qcom_glink_channel(&gMGlink, "IPCRTR", 4096, 8, gMChannelStorage, &glue, nullptr, 0);
    gMGlinkStarted = true;
    ms[0] = MsGlink;
    serviceModem(kEvMGlink);
    return true;
}
}  // namespace
namespace {
void adspThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-adsp");
    unsigned mode = 2;
    PE_parse_boot_argn("houji_adsp", &mode, sizeof mode);
    if (!mapWindow(gSmemW, kSmem, kSmemSize) || !mapWindow(gMutex, kMutex, 0x1000) || !mapWindow(gIpcc, kIpcc, 0x1000) ||
        !mapWindow(gAoss, kAoss, 0x1000) || !mapWindow(gRsc, kRscDrv, 0x10000)) {
        fail(StMapped, -12);
        return;
    }
    gSmem = {gSmemW.va, (uint32_t)kSmemSize};
    gLayout = qcom_rsc_layout(rd(gRsc, 0));
    stage(StMapped);
    survey();
    stage(StSurveyed);
    if (mode < 2 || !start() || !startGlink()) return;
    unsigned modem = 1;
    PE_parse_boot_argn("houji_modem", &modem, sizeof modem);
    if (modem >= 2) {unsigned delay = 30; PE_parse_boot_argn("houji_modem_delay", &delay, sizeof delay);
                     waitUntil(delay * 1000, [] {return false;}); modemStart();}
    // From here on the thread only answers doorbells and client sends; with nothing pending it sleeps without a deadline, except while
    // a send waits for an intent or FIFO room (then it looks again every second).
    for (;;) {
        bool pending = false;
        for (unsigned i = 0; i < gClientCount; ++i) pending |= gClients[i].count != 0;
        pending |= gMCount != 0;
        service(waitEvents(pending ? 1000 : 3600u * 1000u) | (pending ? kEvSend : 0));
    }
}
}  // namespace

extern "C" ShengAdspChannel *sheng_adsp_channel(const char *name, uint32_t intent_size, uint32_t intent_count, int initiator,
                                                const ShengAdspClient *client, void *context) {
    if (!gClientLock || !name || !client || !intent_size || !intent_count || intent_count > QCOM_GLINK_MAX_LOCAL_INTENTS) return nullptr;
    uint8_t *storage = static_cast<uint8_t *>(IOMallocData((vm_size_t)intent_size * intent_count));
    Message *queue = static_cast<Message *>(IOMallocData(sizeof(Message) * kQueue));
    IOLockLock(gClientLock);
    ShengAdspChannel *c = gClientCount < kClients && storage && queue ? &gClients[gClientCount] : nullptr;
    if (c) {
        bzero(c, sizeof *c);
        strlcpy(c->name, name, sizeof c->name);
        c->intent_size = intent_size; c->intent_count = intent_count; c->initiator = initiator;
        c->client = *client; c->context = context; c->storage = storage; c->queue = queue;
        __atomic_store_n(&gClientCount, gClientCount + 1, __ATOMIC_RELEASE);
    }
    IOLockUnlock(gClientLock);
    if (!c) {
        if (storage) IOFreeData(storage, (vm_size_t)intent_size * intent_count);
        if (queue) IOFreeData(queue, sizeof(Message) * kQueue);
        return nullptr;
    }
    wake(kEvSend);
    return c;
}

extern "C" int sheng_adsp_send(ShengAdspChannel *c, const void *data, uint32_t length) {
    if (!c || !data || !length) return -2;
    if (length > SHENG_ADSP_MAX_MESSAGE) return -1;
    IOLockLock(gClientLock);
    if (c->count == kQueue) {IOLockUnlock(gClientLock); return -1;}
    Message &m = c->queue[(c->head + c->count) % kQueue];
    memcpy(m.data, data, length);
    m.length = length;
    c->count++;
    IOLockUnlock(gClientLock);
    wake(kEvSend);
    return 0;
}

extern "C" void sheng_adsp_probe(void) {
    unsigned mode = 2;
    PE_parse_boot_argn("houji_adsp", &mode, sizeof mode);
    if (!mode || native_adsp_stats[0]) return;
    gClientLock = IOLockAlloc();
    if (gClientLock && mode >= 2) sheng_qrtr_start();  // the router link registers before the DSP can open it
    thread_t thread = nullptr;
    if (!gClientLock || kernel_thread_start(adspThread, nullptr, &thread) != KERN_SUCCESS) {fail(StMapped, -7); return;}
    thread_deallocate(thread);
}

// ---- houji glue ----
// No GIC SPI attach in this kernel: the two interrupt handlers above only acknowledge a hardware status register (IPCC RECV_ID returns
// NONE when nothing is pending, the RSC irq_status is masked to our TCS), so a 2 ms poller thread calls them instead.
namespace {
struct Poller {void (*fn)(void *); void *context;};
Poller gPollers[4];
unsigned gPollerCount;
void pollerThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-adsp-irq");
    for (;;) {
        const unsigned n = __atomic_load_n(&gPollerCount, __ATOMIC_ACQUIRE);
        for (unsigned i = 0; i < n; ++i) gPollers[i].fn(gPollers[i].context);
        IOSleep(2);
    }
}
}  // namespace
extern "C" int armcpu_spi_attach(uint32_t, bool, void (*handler)(void *), void *context, uint32_t *, uint64_t *) {
    static bool started;
    const unsigned n = __atomic_load_n(&gPollerCount, __ATOMIC_ACQUIRE);
    if (n >= 4 || !handler) return kIOReturnNoResources;
    gPollers[n] = {handler, context};
    __atomic_store_n(&gPollerCount, n + 1, __ATOMIC_RELEASE);
    if (!started) {
        thread_t t = nullptr;
        if (kernel_thread_start(pollerThread, nullptr, &t) != KERN_SUCCESS) return kIOReturnNoResources;
        thread_deallocate(t);
        started = true;
    }
    return kIOReturnSuccess;
}

extern "C" {volatile int32_t native_battery_stats[16];}
extern "C" void sheng_battery_start(void);
extern "C" {extern volatile int32_t native_charger_stats[16];}
// Starts the DSP (houji_adsp: 1 survey only, 2 start, default 2) and then the battery power source (houji_battery, default 1), after a
// delay so the boot's own GPU and UFS qualification runs alone.
// ---- GPS groundwork: the modem (MPSS) image, read only ----
// GNSS runs in the modem. Before anything starts it: the modem's stock image (modemfirmware_a, NAME.mdt + .bNN) is parsed and placed in
// its carveouts from the board's devicetree (mpss 0x8bc00000 + 0xf400000, PAS 4; q6_mpss_dtb 0x9b000000 + 0x80000). No SCM call, no
// write. [0] stage (1 modem parsed, 2 dtb parsed, 3 both fit; negative: the step that failed), [1] modem bytes, [2] modem span,
// [3] segments, [4] relocatable, [5] dtb span, [6] lowest address, [7] parse/place error
extern "C" {volatile int32_t native_modem_stats[8];}
static int surveyImage(const char *path, uint64_t base, uint64_t size, volatile int32_t *st, bool modem) {
    void *file = nullptr; uint64_t bytes = 0;
    int rc = sheng_fw_open(path, &file, &bytes);
    if (rc) return 100 + rc;
    QcomMdtImage *image = IOMallocType(QcomMdtImage);
    rc = qcom_mdt_parse(image, bytes, fileRead, file);
    if (!rc) {
        unsigned loads = 0;
        for (unsigned i = 0; i < image->count && !rc; ++i) {
            if (!image->segment[i].load) continue;
            ++loads;
            uint64_t at = 0;
            if (qcom_mdt_place(image, i, base, size, &at)) rc = 200 + (int)i;
        }
        if (modem) {st[1] = (int32_t)bytes; st[2] = (int32_t)qcom_mdt_span(image); st[3] = (int32_t)loads; st[4] = image->relocatable;
                    st[6] = (int32_t)image->min_address;}
        else st[5] = (int32_t)qcom_mdt_span(image);
        if (!rc && qcom_mdt_span(image) > size) rc = 300;
    }
    IOFreeType(image, QcomMdtImage);
    sheng_fw_close(file);
    return rc;
}
static void houjiModemSurvey() {
    volatile int32_t *st = native_modem_stats;
    int rc = surveyImage("modemfw:modem", 0x8bc00000ull, 0xf400000ull, st, true);
    if (rc) {st[0] = -1; st[7] = rc; return;}
    st[0] = 1;
    rc = surveyImage("modemfw:modem_dtb", 0x9b000000ull, 0x80000ull, st, false);
    if (rc) {st[0] = -2; st[7] = rc; return;}
    st[0] = 3;
}

static void houjiPowerThread(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "houji-power");
    unsigned delay = 45;
    PE_parse_boot_argn("houji_adsp_delay", &delay, sizeof delay);
    IOSleep(delay * 1000);
    sheng_adsp_probe();
    unsigned battery = 1;
    PE_parse_boot_argn("houji_battery", &battery, sizeof battery);
    if (battery == 1 && !native_battery_stats[0]) sheng_battery_start();
    unsigned modem = 1;
    PE_parse_boot_argn("houji_modem", &modem, sizeof modem);
    if (modem) houjiModemSurvey();
    thread_terminate(current_thread());
}
extern "C" void houji_wifi_start(void);
extern "C" void houji_wifi_report(void);
extern "C" void houji_buttons_start(void);
extern "C" void houji_buttons_report(void);
extern "C" void houji_power_start(void) {
    houji_wifi_start();
    houji_buttons_start();
    unsigned mode = 2;
    PE_parse_boot_argn("houji_adsp", &mode, sizeof mode);
    if (!mode) return;
    thread_t t = nullptr;
    if (kernel_thread_start(houjiPowerThread, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
}
extern "C" void houji_power_report(void) {
    houji_wifi_report();
    houji_buttons_report();
    {const volatile int32_t *m = native_modem_stats;
     const volatile int32_t *q = native_mpss_stats;
     if (q[0]) {
         IOLog("HOUJI MPSS: stage=%d err=%d slave=%x ready-ms=%d glink=%d/%d services=%d rmtfs=%d/%d rd=%dK wr=%dK scm=%d servloc=%d hello=%d loc=%d drop=%d\n",
               q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]);
         IOLog("HOUJI MPSS TEXT: %.90s\n", native_mpss_text);
     }
     if (m[0]) IOLog("HOUJI MODEM: stage=%d bytes=%d span=%x segments=%d relocatable=%d dtb-span=%x min=%x err=%d\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);}
    const volatile int32_t *a = native_adsp_stats;
    IOLog("HOUJI ADSP: stage=%d err=%d irq=%d slave=%x items=%x qmp=%x fw-KiB=%d ready-ms=%d glink=%d/%d chan=%x scm=%x/%x\n",
          a[0], a[1], a[2], a[3], a[4], a[5], a[7], a[8], a[9], a[10], a[11], a[13], a[14]);
    IOLog("HOUJI ADSP TEXT: %.90s\n", native_adsp_text);
    const volatile int32_t *b = native_battery_stats, *c = native_charger_stats;
    IOLog("HOUJI BATTERY: stage=%d pct=%d mV=%d mA=%d temp=%d flags=%x cycles-done=%d answers=%d to/err=%x notes=%d usb=%d full=%d design=%d "
          "cycles=%d drop=%x refused=%d\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    IOLog("HOUJI CHARGER: usb-online=%d usb-type=%d adapter=%d real-type=%d limit-mA=%d max-mA=%d usb-mV=%d usb-mA=%d status=%d type=%d "
          "thermal=%d chg-mA=%d watts=%d bits=%x sets(done/fcc/verify/limit)=%08x set-result=%d\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11],
          c[12], c[13], c[14], c[15]);
}
