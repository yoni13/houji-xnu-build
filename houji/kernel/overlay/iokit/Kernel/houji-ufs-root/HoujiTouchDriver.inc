// SPDX-License-Identifier: BSD-2-Clause-Patent
// GPIO SPI/TouchCom adapters derived from the pinned UniXNU Ace 5 reference.
// Houji only: six DT-confirmed pins, ldob9/12 supplied by UEFI, no GPIO108.
// GENI/GPI/SMMU registers are read only. No firmware upload or persistent commands.
#include <kern/sched_prim.h>
#include "HoujiTouchReport.h"
#include "HoujiTouchWire.h"
#include "HoujiTouchGpio.h"
#include "HoujiTouchDescriptor.h"
extern "C" void sweet_hid_touch(int,int,int,int);
static struct {
  uintptr_t pin[6],gpi;
  OSSharedPtr<IOMemoryMap> maps[6],gpiMap;
  uint32_t cfg[6],io[6];
  struct gt_state contacts;
  bool changed,started;
  unsigned stage,error,frames,events,irqPolls,deviceMaxWrite,effectiveMaxWrite,configBytes,lastHeader,discarded;
  unsigned workerPhase,loops,irqLevel,hidEntered,hidCompleted;
  uint8_t reportConfig[512];
  unsigned descriptorBytes;
  uint8_t largeReport[8199]; // Fixed BSS scratch, never on the 16 KiB thread stack.
} ht;
static int htWaitChannel;
static uint32_t htRead(uintptr_t base,unsigned off) {
  uint32_t v=*(volatile uint32_t *)(base+off);
  __asm__ volatile("dmb oshld" ::: "memory");return v;
}
static void htPinWrite(unsigned index,unsigned offset,uint32_t value) {
  if(index>=6 || (offset!=0 && offset!=4))panic("HOUJI touch write boundary");
  *(volatile uint32_t *)(ht.pin[index]+offset)=value;
  __asm__ volatile("dsb osh" ::: "memory");
}
static void htWait(unsigned ms) {
  uint64_t deadline;clock_interval_to_deadline(ms,NSEC_PER_MSEC,&deadline);
  (void)assert_wait_deadline(&htWaitChannel,THREAD_UNINT,deadline);
  thread_block(THREAD_CONTINUE_NULL);
}
static void htSet(unsigned pin,unsigned high) {
  // Only the four board-confirmed outputs are ever driven.
  if(pin!=1 && pin!=2 && pin!=3 && pin!=4)panic("HOUJI touch output boundary");
  htPinWrite(pin,4,(htRead(ht.pin[pin],4)&~2u)|(high?2u:0));
}
static unsigned htSample(void) {return htRead(ht.pin[0],4)&1;}
static void htPause(void) {IODelay(1);}
static const struct gg_bus htGpioBus={htSet,htSample,htPause};
static int htBusRead(uint8_t *data,unsigned bytes) {
  int result=gg_transfer(&htGpioBus,nullptr,data,bytes);
  if(!result && bytes==4) {
    uint32_t header=data[0]|uint32_t(data[1])<<8|uint32_t(data[2])<<16|uint32_t(data[3])<<24;
    __atomic_store_n(&ht.lastHeader,header,__ATOMIC_RELAXED);
  }
  return result;
}
static int htBusWrite(const uint8_t *data,unsigned bytes) {
  return gg_transfer(&htGpioBus,data,nullptr,bytes);
}
static int htDiscardReport(unsigned bytes) {
  if(bytes>8192 || htBusRead(ht.largeReport,bytes+7))return 1;
  if(ht.largeReport[0]!=0xa5 || ht.largeReport[1]!=3)return 2;
  for(unsigned i=bytes+2;i<bytes+7;i++)if(ht.largeReport[i]!=0x5a)return 3;
  __atomic_add_fetch(&ht.discarded,1,__ATOMIC_RELAXED);return 0;
}
static void htStage(unsigned stage) {
  __atomic_store_n(&ht.stage,stage,__ATOMIC_RELEASE);
  IOLog("HOUJI TOUCH: stage=%u\n",stage);
}
static void htRestore(void) {
  if(!ht.changed)return;
  htSet(3,1);htSet(2,0);
  for(unsigned i=0;i<6;i++) {htPinWrite(i,4,ht.io[i]);htPinWrite(i,0,ht.cfg[i]);}
  ht.changed=false;
}
static int htPrepare(void) {
  if(!gTouchProbeEnabled || gTouchProbe.result!=0)return 20;
  if(((gTouchProbe.firmware>>8)&255)!=1 || (gTouchProbe.status&0x1001) ||
      (gTouchProbe.dma_interface&3) || (gTouchProbe.tx_fifo&0xfffffff) ||
      (gTouchProbe.rx_fifo&0x1ffffff))return 21;
  ht.gpi=mapPhys(0xa00000,0x60000,ht.gpiMap);if(!ht.gpi)return 22;
  houji_ufs_checkpoint("before touch AP GPI ownership check",0);
  // Live DT explicitly assigns dynamic GPI instances 2/3 to the AP.
  for(unsigned n=2;n<=3;n++)for(unsigned channel=0;channel<2;channel++) {
    unsigned off=0x10000+0x4000*n+0x80*channel;
    uint32_t cfg=htRead(ht.gpi,off),scratch=htRead(ht.gpi,off+0x60);
    unsigned state=(cfg>>20)&15;
    IOLog("HOUJI TOUCH: GPI%u ch%u cfg=%x scratch=%x\n",n,channel,cfg,scratch);
    if(cfg==0xffffffffu || scratch==0xffffffffu)return 23;
    if((scratch&15)==4 && state!=0 && state!=1 && state!=3)return 24;
  }
  for(unsigned i=0;i<6;i++) {
    ht.pin[i]=mapPhys(0xf100000+ht_gpio[i]*0x1000,0x1000,ht.maps[i]);
    if(!ht.pin[i])return 25;
    ht.cfg[i]=htRead(ht.pin[i],0);ht.io[i]=htRead(ht.pin[i],4);
    if(ht.cfg[i]==0xffffffffu || !gg_pin_owned(ht.cfg[i]))return 26;
  }
  htStage(2);ht.changed=true;
  htSet(1,1);htSet(2,0);htSet(3,1);htSet(4,1);
  for(unsigned i=0;i<6;i++) {
    bool output=i!=0 && i!=5;
    uint32_t cfg=(ht.cfg[i]&~0x3ffu)|(i<4?2u<<6:0)|(output?1u<<9:0);
    htPinWrite(i,0,cfg);
    if(htRead(ht.pin[i],0)!=cfg)return 27;
  }
  htWait(50);htSet(4,0);htWait(10);htSet(4,1);htWait(200);
  return 0;
}
static int htRun(void) {
  htStage(1);int err=htPrepare();if(err)return err;
  htStage(3);
  struct gw_bus bus={htBusRead,htBusWrite,htWait,64,htDiscardReport};struct gw_message msg={0};
  err=gw_read(&bus,&msg);
  for(unsigned retry=0;err==2 && retry<100;retry++) {htWait(10);err=gw_read(&bus,&msg);}
  IOLog("HOUJI TOUCH: first read result=%d code=%x bytes=%u\n",err,msg.code,msg.size);
  if(err)return 100+err;
  if(msg.code!=0x10) {err=gw_command(&bus,2,nullptr,0,&msg);if(err)return 120+err;}
  if(msg.size<24 || msg.data[1]!=1 || memcmp(msg.data+2,"S3910",5))return 140;
  uint32_t build=msg.data[18]|uint32_t(msg.data[19])<<8|uint32_t(msg.data[20])<<16|uint32_t(msg.data[21])<<24;
  IOLog("HOUJI TOUCH: TouchCom v1 S3910 application firmware=%u\n",build);
  if(build!=4323384)return 141;
  unsigned advertised=gw_le16(msg.data+22);
  bus.max_write=advertised && advertised<GW_MAX+3 ? advertised : GW_MAX+3;
  __atomic_store_n(&ht.deviceMaxWrite,advertised,__ATOMIC_RELAXED);
  __atomic_store_n(&ht.effectiveMaxWrite,bus.max_write,__ATOMIC_RELAXED);
  IOLog("HOUJI TOUCH: advertised write=%u bounded host write=%u\n",advertised,bus.max_write);
  if(bus.max_write<sizeof(gt_report_config)+3)return 142;
  err=gw_command(&bus,0x20,nullptr,0,&msg);if(err)return 160+err;
  if(msg.size<40 || gw_le16(msg.data+2) || gw_le16(msg.data+32)!=1199 ||
      gw_le16(msg.data+34)!=2669 || gw_le16(msg.data+36)!=10)return 180;
  unsigned size=gw_le16(msg.data+12);
  __atomic_store_n(&ht.configBytes,size,__ATOMIC_RELAXED);
  IOLog("HOUJI TOUCH: app coordinates verified; report config bytes=%u\n",size);
  if(size<sizeof(gt_report_config) || size>GW_MAX || sizeof(gt_report_config)+3>bus.max_write)return 181;
  htStage(4);
  // Stop optional raw frame streams; an unsupported report ID is nonfatal.
  uint8_t raw=0xc0;err=gw_command(&bus,6,&raw,1,&msg);
  IOLog("HOUJI TOUCH: disable THP result=%d\n",err);
  raw=0x23;err=gw_command(&bus,6,&raw,1,&msg);
  IOLog("HOUJI TOUCH: disable raw result=%d\n",err);
  // Read the controller's native descriptor; do not force a custom layout.
  err=gw_command(&bus,0x25,nullptr,0,&msg);if(err)return 220+err;
  if(!msg.size || msg.size>sizeof(ht.reportConfig))return 239;
  memcpy(ht.reportConfig,msg.data,msg.size);
  __atomic_store_n(&ht.descriptorBytes,msg.size,__ATOMIC_RELEASE);
  struct hd_layout layout;
  if(hd_layout_parse(ht.reportConfig,msg.size,&layout))return 240;
  uint8_t touch=0x11;err=gw_command(&bus,5,&touch,1,&msg);if(err)return 260+err;
  htStage(5);IOLog("HOUJI TOUCH: native contact descriptor validated; physical polling enabled\n");
  unsigned errors=0;
  for(;;) {
    __atomic_store_n(&ht.workerPhase,1,__ATOMIC_RELEASE);
    htWait(10);
    __atomic_add_fetch(&ht.loops,1,__ATOMIC_RELAXED);
    __atomic_store_n(&ht.workerPhase,2,__ATOMIC_RELEASE);
    unsigned irqLevel=htRead(ht.pin[5],4)&1;
    __atomic_store_n(&ht.irqLevel,irqLevel,__ATOMIC_RELAXED);
    if(irqLevel)continue;
    __atomic_add_fetch(&ht.irqPolls,1,__ATOMIC_RELAXED);
    __atomic_store_n(&ht.workerPhase,3,__ATOMIC_RELEASE);
    err=gw_read(&bus,&msg);
    if(!err && msg.code==0x10)return 280;
    if(!err && msg.code==0x11) {
      __atomic_store_n(&ht.workerPhase,4,__ATOMIC_RELEASE);
      struct gt_frame frame;
      err=hd_decode(ht.reportConfig,ht.descriptorBytes,msg.data,msg.size,1199,2669,&ht.contacts,&frame);
      if(!err) {
        for(unsigned i=0;i<frame.count;i++) {
          const struct gt_event &e=frame.events[i];
          __atomic_add_fetch(&ht.hidEntered,1,__ATOMIC_RELAXED);
          __atomic_store_n(&ht.workerPhase,5,__ATOMIC_RELEASE);
          sweet_hid_touch(e.id,e.down,e.x,e.y);
          __atomic_add_fetch(&ht.hidCompleted,1,__ATOMIC_RELAXED);
          __atomic_store_n(&ht.workerPhase,6,__ATOMIC_RELEASE);
          __atomic_add_fetch(&ht.events,1,__ATOMIC_RELAXED);
        }
        __atomic_add_fetch(&ht.frames,1,__ATOMIC_RELAXED);
      }
    }
    if(err) {if(++errors>=3)return 300+err;} else errors=0;
  }
}
static void htThread(void *,wait_result_t) {
  htWait(1000);int err=htRun();
  __atomic_store_n(&ht.workerPhase,7,__ATOMIC_RELEASE);
  __atomic_store_n(&ht.error,unsigned(err),__ATOMIC_RELEASE);
  for(unsigned i=0;i<GT_CONTACTS;i++)if(ht.contacts.active&(1u<<i))
    sweet_hid_touch(i,0,ht.contacts.x[i],ht.contacts.y[i]);
  htRestore();IOLog("HOUJI TOUCH: worker stopped result=%d; GUI continues\n",err);
  thread_terminate(current_thread());
}
static void houjiTouchStart(void) {
  int enabled=0;PE_parse_boot_argn("houji_touch",&enabled,sizeof(enabled));
  if(enabled!=1 || ht.started)return;ht.started=true;
  thread_t thread; kern_return_t kr=kernel_thread_start(htThread,nullptr,&thread);
  if(kr==KERN_SUCCESS)thread_deallocate(thread);else ht.error=unsigned(kr);
}
extern "C" void houji_touch_driver_report(void) {
  if(!ht.started)return;
  IOLog("HOUJI TOUCH: stage=%u error=%u frames=%u events=%u irq-polls=%u device-write=%u host-write=%u config=%u last-header=%08x discarded=%u; GPIO SPI, no DMA or firmware writes\n",
    __atomic_load_n(&ht.stage,__ATOMIC_ACQUIRE),__atomic_load_n(&ht.error,__ATOMIC_ACQUIRE),
    __atomic_load_n(&ht.frames,__ATOMIC_RELAXED),__atomic_load_n(&ht.events,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.irqPolls,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.deviceMaxWrite,__ATOMIC_RELAXED),__atomic_load_n(&ht.effectiveMaxWrite,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.configBytes,__ATOMIC_RELAXED),__atomic_load_n(&ht.lastHeader,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.discarded,__ATOMIC_RELAXED));
  // Cached worker state only: the sampler performs no additional GPIO or bus reads.
  IOLog("HOUJI TOUCH WORKER: phase=%u loops=%u irq-level=%u HID-entered=%u HID-completed=%u; phases 1=sleep 2=IRQ 3=wire 4=decode 5=HID 6=done 7=stopped\n",
    __atomic_load_n(&ht.workerPhase,__ATOMIC_ACQUIRE),__atomic_load_n(&ht.loops,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.irqLevel,__ATOMIC_RELAXED),__atomic_load_n(&ht.hidEntered,__ATOMIC_RELAXED),
    __atomic_load_n(&ht.hidCompleted,__ATOMIC_RELAXED));
  unsigned descriptorBytes=__atomic_load_n(&ht.descriptorBytes,__ATOMIC_ACQUIRE);
  for(unsigned at=0;at<descriptorBytes;at+=32) {
    char text[100];unsigned used=0;
    for(unsigned j=at;j<descriptorBytes && j<at+32;j++)
      used+=snprintf(text+used,sizeof(text)-used,"%02x ",ht.reportConfig[j]);
    IOLog("HOUJI TOUCH CONFIG +%u/%u: %s\n",at,descriptorBytes,text);
  }
}
