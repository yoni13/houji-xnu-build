/* SPDX-License-Identifier: MIT */
#include "NativeAdrenoMemory.h"
#include "NativeAdrenoCpuPagePolicy.h"
#include <kern/cpu_data.h>
#include <vm/vm_map.h>
#include <mach/vm_param.h>
#include <kern/clock.h>
#include <vm/memory_types.h>
extern "C" vm_offset_t phystokv(uint64_t pa);
extern "C" uint64_t gPhysBase,gPhysSize;   /* the physical range phystokv covers (the kernel's own DRAM); anything else panics in phystokv */
extern "C" void pmap_set_cache_attributes(ppnum_t pn,unsigned int cacheattr);
extern "C" unsigned int pmap_cache_attributes(ppnum_t pn);
/* Time and bytes of every CPU cache maintenance pass (the board page shows them per submission), and the pages user BOs made
 * write-combined: [0] ticks, [1] bytes, [2] longest pass in ticks, [3] passes, [4] write-combined pages now. */
extern "C" {volatile uint64_t native_adreno_cache_stats[5];}
/* Time spent checking and re-asserting write-combined page attributes (NativeAdrenoReassertWriteCombined), for the board page. */
extern "C" {volatile uint64_t native_adreno_reassert_ticks;}
static void noteMaintenance(uint64_t began,size_t bytes)
{
    const uint64_t ticks=mach_absolute_time()-began;
    __atomic_fetch_add(&native_adreno_cache_stats[0],ticks,__ATOMIC_RELAXED);
    __atomic_fetch_add(&native_adreno_cache_stats[1],bytes,__ATOMIC_RELAXED);
    __atomic_fetch_add(&native_adreno_cache_stats[3],1,__ATOMIC_RELAXED);
    uint64_t longest=__atomic_load_n(&native_adreno_cache_stats[2],__ATOMIC_RELAXED);
    while(ticks>longest && !__atomic_compare_exchange_n(&native_adreno_cache_stats[2],&longest,ticks,true,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
}

IOReturn NativeAdrenoQueryCpuPages(NativeAdrenoCpuState *out)
{
    if(!out)return kIOReturnBadArgument;
    *out={};
    disable_preemption();
    __asm__ volatile("mrs %0, tcr_el1" : "=r"(out->tcr));
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(out->ttbr0));
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(out->ttbr1));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(out->sctlr));
    out->kernel_page_bytes=page_size;
    out->kernel_map_page_bytes=vm_map_page_size(kernel_map);
    enable_preemption();
    const bool native=NativeAdrenoCpuPageGeometry(PAGE_SIZE,out->kernel_page_bytes,
        out->kernel_map_page_bytes,out->tcr,out->sctlr,out->ttbr1,&out->cpu_page_shift);
    return native?kIOReturnSuccess:kIOReturnUnsupported;
}
IOReturn NativeAdrenoQueryCpu4K(NativeAdrenoCpuState *out)
{
    const IOReturn status=NativeAdrenoQueryCpuPages(out);
    return status?status:(out->kernel_page_bytes==4096?kIOReturnSuccess:kIOReturnUnsupported);
}
static IOReturn maintainOwnedPages(void *mapping,size_t bytes,NativeAdrenoCacheOperation op)
{
    /* Whole cache lines inside owned backing avoid IVAC corruption of a neighbour's dirty line.
     * No Apple coherent-I/O shortcut is used for these noncoherent DMA paths.
     * Pin this CPU while reading its CTR and issuing all cache operations. */
    if(!mapping || !bytes || (((uintptr_t)mapping|bytes)&4095) ||
        bytes>UINTPTR_MAX-(uintptr_t)mapping || op>NativeAdrenoInvalidate || op<NativeAdrenoClean)
        return kIOReturnBadArgument;
    disable_preemption();
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    size_t line=(size_t)4<<((ctr>>16)&15);
    if(line>4096) {enable_preemption();return kIOReturnUnsupported;}
    uintptr_t address=(uintptr_t)mapping,end=address+bytes;
    __asm__ volatile("dsb sy":::"memory");
    for(;address<end;address+=line) {
        if(op==NativeAdrenoClean)__asm__ volatile("dc cvac, %0"::"r"(address):"memory");
        else if(op==NativeAdrenoCleanInvalidate)__asm__ volatile("dc civac, %0"::"r"(address):"memory");
        else __asm__ volatile("dc ivac, %0"::"r"(address):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
    enable_preemption();return kIOReturnSuccess;
}
IOReturn NativeAdrenoMaintainOwnedPages(void *mapping,size_t bytes,NativeAdrenoCacheOperation op)
{
    const uint64_t began=mach_absolute_time();
    const IOReturn status=maintainOwnedPages(mapping,bytes,op);
    if(!status)noteMaintenance(began,bytes);
    return status;
}
IOReturn NativeAdrenoMaintainPhysicalPages(uint64_t physical,size_t bytes,NativeAdrenoCacheOperation op)
{
    if(!physical || !bytes || ((physical|bytes)&4095) || physical>UINT64_MAX-bytes || op>NativeAdrenoInvalidate || op<NativeAdrenoClean)
        return kIOReturnBadArgument;
    const uint64_t began=mach_absolute_time();
    /* One barrier pair per run of up to 64 pages instead of per page (an imported 3048x2032 surface is 5952 pages): preemption stays off
     * for a bounded run, and the closing DSB on the issuing core completes every operation of that run. */
    for(size_t offset=0;offset<bytes;) {
        disable_preemption();
        uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
        const size_t line=(size_t)4<<((ctr>>16)&15);
        if(line>4096) {enable_preemption();return kIOReturnUnsupported;}
        __asm__ volatile("dsb sy":::"memory");
        for(unsigned run=0;run<64 && offset<bytes;++run,offset+=4096) {
            /* A page outside the kernel's DRAM is device memory (the firmware framebuffer the display hand-off imports): the CPU maps it
             * uncached (kIOMapInhibitCache), no cache line of it exists to maintain, and phystokv would panic on it (int-080: illegal PA
             * 0xb8000000). */
            const uint64_t page=physical+offset;
            if(page<gPhysBase || page-gPhysBase>=gPhysSize)continue;
            const vm_offset_t alias=phystokv(page);
            if(!alias) {__asm__ volatile("dsb sy":::"memory");enable_preemption();return kIOReturnUnsupported;}
            for(uintptr_t address=alias,end=alias+4096;address<end;address+=line) {
                if(op==NativeAdrenoClean)__asm__ volatile("dc cvac, %0"::"r"(address):"memory");
                else if(op==NativeAdrenoCleanInvalidate)__asm__ volatile("dc civac, %0"::"r"(address):"memory");
                else __asm__ volatile("dc ivac, %0"::"r"(address):"memory");
            }
        }
        __asm__ volatile("dsb sy":::"memory");
        enable_preemption();
    }
    noteMaintenance(began,bytes);
    return kIOReturnSuccess;
}
/* A user BO's pages become Normal non-cacheable (write-combined) for every CPU mapping the pmap tracks: the kernel's own mapping and each
 * client view. The views did not get the cache mode their mapping asked for (int-070: Turnip's write-combine view left IB words in a CPU
 * cache line), but they do take the page's own attribute. That is the Linux msm model for its WC BOs: CPU writes reach DRAM, the GPU's
 * non-cacheable reads see them and CPU reads see GPU writes without per-submission maintenance. The physical aperture keeps its cacheable
 * mapping on the reference kernel; the candidate still requires live alias qualification.
 * Nothing writes through that alias, and the caller cleans and invalidates the pages first.
 * Before the pages go back to the VM they get the default attribute again. */
/* Validate the whole list before mutating any pmap page. These spans are
 * complete exclusive backing pages, not clipped 4 KiB GPU-visible ranges. */
static IOReturn cpuCacheSpans(const NativeAdrenoPhysicalSpan *spans,size_t count,
    NativeAdrenoCpuState *cpu,uint64_t *pages)
{
    if(!spans || !count || !cpu || !pages)return kIOReturnBadArgument;
    *pages=0;
    const IOReturn status=NativeAdrenoQueryCpuPages(cpu);if(status)return status;
    const uint64_t bytes=cpu->kernel_page_bytes,mask=bytes-1;
    for(size_t i=0;i<count;++i) {
        const NativeAdrenoPhysicalSpan &span=spans[i];
        if(!span.bytes || ((span.offset|span.physical|span.bytes)&mask) ||
            span.physical>UINT64_MAX-span.bytes || span.physical<gPhysBase ||
            span.physical+span.bytes-gPhysBase>gPhysSize ||
            (uint64_t)(ppnum_t)((span.physical+span.bytes-bytes)>>cpu->cpu_page_shift)!=
                ((span.physical+span.bytes-bytes)>>cpu->cpu_page_shift))return kIOReturnBadArgument;
        if(span.bytes/bytes>UINT64_MAX-*pages)return kIOReturnBadArgument;
        *pages+=span.bytes/bytes;
    }
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoSetPagesCacheMode(const NativeAdrenoPhysicalSpan *spans,size_t count,bool write_combined)
{
    NativeAdrenoCpuState cpu{};uint64_t pages=0;
    IOReturn status=cpuCacheSpans(spans,count,&cpu,&pages);if(status)return status;
    const unsigned normal=VM_WIMG_DEFAULT&VM_WIMG_MASK;
    const unsigned wanted=write_combined?VM_WIMG_WCOMB:normal;
    /* Exclusive fresh IOBuffer backing must begin with its known default
     * attribute. Refuse unknown modes rather than claiming to restore them. */
    for(size_t i=0;i<count;++i)for(uint64_t offset=0;offset<spans[i].bytes;offset+=cpu.kernel_page_bytes) {
        const ppnum_t pn=(ppnum_t)((spans[i].physical+offset)>>cpu.cpu_page_shift);
        const unsigned current=pmap_cache_attributes(pn)&VM_WIMG_MASK;
        if(write_combined?current!=normal:(current!=normal && current!=VM_WIMG_WCOMB))return kIOReturnUnsupported;
    }
    for(size_t i=0;i<count;++i)for(uint64_t offset=0;offset<spans[i].bytes;offset+=cpu.kernel_page_bytes) {
        const ppnum_t pn=(ppnum_t)((spans[i].physical+offset)>>cpu.cpu_page_shift);
        pmap_set_cache_attributes(pn,write_combined?VM_WIMG_WCOMB:VM_WIMG_USE_DEFAULT);
        if((pmap_cache_attributes(pn)&VM_WIMG_MASK)!=wanted)return kIOReturnIOError;
    }
    if(write_combined)__atomic_fetch_add(&native_adreno_cache_stats[4],pages,__ATOMIC_RELAXED);
    else __atomic_fetch_sub(&native_adreno_cache_stats[4],pages,__ATOMIC_RELAXED);
    return kIOReturnSuccess;
}
IOReturn NativeAdrenoReassertWriteCombined(const NativeAdrenoPhysicalSpan *spans,size_t count,uint32_t *reset)
{
    if(!reset)return kIOReturnBadArgument;
    NativeAdrenoCpuState cpu{};uint64_t pages=0;
    IOReturn status=cpuCacheSpans(spans,count,&cpu,&pages);if(status)return status;
    const uint64_t began=mach_absolute_time();
    for(size_t i=0;i<count;++i)for(uint64_t offset=0;offset<spans[i].bytes;offset+=cpu.kernel_page_bytes) {
        const ppnum_t pn=(ppnum_t)((spans[i].physical+offset)>>cpu.cpu_page_shift);
        const unsigned current=pmap_cache_attributes(pn)&VM_WIMG_MASK;
        if(current!=VM_WIMG_WCOMB) {
            if(current!=(VM_WIMG_DEFAULT&VM_WIMG_MASK))return kIOReturnUnsupported;
            pmap_set_cache_attributes(pn,VM_WIMG_WCOMB);
            if((pmap_cache_attributes(pn)&VM_WIMG_MASK)!=VM_WIMG_WCOMB)return kIOReturnIOError;
            ++*reset;
        }
    }
    __atomic_fetch_add(&native_adreno_reassert_ticks,mach_absolute_time()-began,__ATOMIC_RELAXED);
    return kIOReturnSuccess;
}
