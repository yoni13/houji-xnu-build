// SPDX-License-Identifier: MIT
// Included after the bridge's textureDescriptor/lastError helpers. The method
// ABI is from the owner's iOS16 20H392 MTLTextureLayout protocol, not an AGX
// instance-variable layout. The bytes are our explicit linear CPU format.
#include "BlockTextureMemory.h"
#include <unistd.h>
typedef struct {uint64_t words[2];} NBLinearWatermark;
@interface NBLinearTextureLayout : NSObject {
@public native_linear::Layout linear;
@public uint64_t format;
@public bool compressed;
@public native_linear::BlockLayout block;
}
@end
@implementation NBLinearTextureLayout
+ (void)initialize {
    if(self==[NBLinearTextureLayout class]){
        Protocol *protocol=objc_getProtocol("MTLTextureLayout");
        if(protocol)class_addProtocol(self,protocol);
    }
}
- (NSUInteger)size {return NSUInteger(linear.bytes);}
- (NSUInteger)alignment {return NSUInteger(linear.alignment);}
- (MTLSize)copyGranularity {return compressed?MTLSizeMake(4,4,1):MTLSizeMake(1,1,1);}
- (NBLinearWatermark)watermark {
    // A distinct, versioned native-linear identity prevents an Apple tiled
    // layout from being reused as this device's layout in a caller's cache.
    uint64_t hash=1469598103934665603ull;
    for(uint64_t v:{format,linear.width,linear.height,linear.row,linear.bytes,compressed?block.width:linear.width,compressed?block.height:linear.height}){
        for(unsigned i=0;i<8;i++){hash^=uint8_t(v>>(i*8));hash*=1099511628211ull;}
    }
    return {{0x4e424c494e313602ull,hash}};
}
- (void)initializeTextureMemory:(void *)memory {if(memory)memset(memory,0,size_t(linear.bytes));}
- (void)finalizeTextureMemory:(void *)memory {if(memory)sys_dcache_flush(memory,size_t(linear.bytes));}
- (void)copyFromLinearBytes:(const void *)bytes linearOffset:(NSUInteger)offset
         linearBytesPerRow:(NSUInteger)row linearBytesPerImage:(NSUInteger)image
           toTextureMemory:(void *)memory textureSlice:(NSUInteger)slice
              textureLevel:(NSUInteger)level textureRegion:(MTLRegion)region {
    native_linear::Region r={region.origin.x,region.origin.y,region.origin.z,
                            region.size.width,region.size.height,region.size.depth};
    if(!(compressed?native_linear::blockCopy(block,memory,const_cast<void *>(bytes),offset,row,image,slice,level,r,true):native_linear::copy(linear,memory,const_cast<void *>(bytes),offset,row,image,slice,level,r,true)))
        NBLOG(@"native-metal: native linear layout refused invalid upload span");
}
- (void)copyFromTextureMemory:(const void *)memory textureSlice:(NSUInteger)slice
                 textureLevel:(NSUInteger)level textureRegion:(MTLRegion)region
                toLinearBytes:(void *)bytes linearOffset:(NSUInteger)offset
            linearBytesPerRow:(NSUInteger)row linearBytesPerImage:(NSUInteger)image {
    native_linear::Region r={region.origin.x,region.origin.y,region.origin.z,
                            region.size.width,region.size.height,region.size.depth};
    if(!(compressed?native_linear::blockCopy(block,const_cast<void *>(memory),bytes,offset,row,image,slice,level,r,false):native_linear::copy(linear,const_cast<void *>(memory),bytes,offset,row,image,slice,level,r,false)))
        NBLOG(@"native-metal: native linear layout refused invalid download span");
}
@end

static NBLinearTextureLayout *linearTextureLayout(MTLTextureDescriptor *d,BOOL backed) {
    // Own uncompressed rows and ASTC 4x4 block rows. The distinct watermark
    // invalidates cached AGX layouts. Heap, mip and array layouts stay refused.
    if(!d || backed || d.textureType!=MTLTextureType2D || d.depth!=1 ||
       d.arrayLength!=1 || d.mipmapLevelCount!=1 || d.sampleCount!=1)return nil;
    if(d.pixelFormat==MTLPixelFormatASTC_4x4_LDR || d.pixelFormat==MTLPixelFormatASTC_4x4_sRGB){
        if(d.usage & ~(MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView))return nil;
        native_linear::BlockLayout block;
        if(!native_linear::blockPlan(d.width,d.height,4,4,16,uint64_t(getpagesize()),block))return nil;
        NBLinearTextureLayout *layout=[NBLinearTextureLayout new];
        layout->linear=block.blocks;layout->block=block;layout->compressed=true;layout->format=uint64_t(d.pixelFormat);
        return layout;
    }
    uint64_t texel=0;
    switch(d.pixelFormat){
        case MTLPixelFormatR8Unorm:texel=1;break;
        case MTLPixelFormatRG8Unorm:texel=2;break;
        case MTLPixelFormatR16Float:texel=2;break;
        case MTLPixelFormatRG16Float:case MTLPixelFormatR32Float:texel=4;break;
        case MTLPixelFormatRGBA16Float:case MTLPixelFormatRG32Float:texel=8;break;
        case MTLPixelFormatRGBA32Float:texel=16;break;
        case MTLPixelFormatRGBA8Unorm:case MTLPixelFormatRGBA8Unorm_sRGB:
        case MTLPixelFormatBGRA8Unorm:case MTLPixelFormatBGRA8Unorm_sRGB:texel=4;break;
        default:return nil;
    }
    const uint64_t row=nmb_device_linear_bytes_per_row(gDevice,uint64_t(d.pixelFormat),d.width);
    native_linear::Layout plan;
    if(!native_linear::plan(d.width,d.height,texel,row,uint64_t(getpagesize()),plan))return nil;
    NBLinearTextureLayout *layout=[NBLinearTextureLayout new];layout->linear=plan;layout->format=uint64_t(d.pixelFormat);
    return layout;
}
