/* SPDX-License-Identifier: MIT
 * Registry compatibility base built from source, no original-kext address or
 * original IOGPU vtable imported. IOAcceleratorES has only IOService ancestry
 * and no added fields or operations, matching the independently inspected
 * iOS16 registry base. This class belongs to the houji native driver.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSSymbol.h>
class IOAcceleratorES:public IOService{OSDeclareDefaultStructors(IOAcceleratorES)};
OSDefineMetaClassAndStructors(IOAcceleratorES,IOService)
extern "C" IOReturn native_adreno_new_user_client(IOService*,task_t,void*,UInt32,OSDictionary*,IOUserClient**,unsigned);
extern "C" void native_adreno_note_property_read(const char*);
class AGXAccelerator:public IOAcceleratorES{
 OSDeclareDefaultStructors(AGXAccelerator)
 public:IOReturn newUserClient(task_t t,void*s,UInt32 type,OSDictionary*p,IOUserClient**h) APPLE_KEXT_OVERRIDE{return native_adreno_new_user_client(this,t,s,type,p,h,8);}
 OSObject*copyProperty(const char*k)const APPLE_KEXT_OVERRIDE{native_adreno_note_property_read(k);return IORegistryEntry::copyProperty(k);}
};
OSDefineMetaClassAndStructors(AGXAccelerator,IOAcceleratorES)
static_assert(sizeof(IOService)==136 && sizeof(IOAcceleratorES)==136 && sizeof(AGXAccelerator)==136,"source registry ancestry extent");
extern "C" IOReturn houji_native_accelerator_register(void){static bool once;if(once)return kIOReturnBusy;auto*a=new AGXAccelerator;if(!a || !a->init() || !a->attach(IOService::getPlatform())){if(a)a->release();return kIOReturnNoMemory;}a->setName("HoujiAdreno750");a->setProperty("MetalPluginName","NativeAdrenoMetal");a->setProperty("MetalPluginClassName","NativeAdrenoMetalDevice");a->setProperty("model","Adreno 750 (houji)");a->registerService();once=true;return kIOReturnSuccess;}
