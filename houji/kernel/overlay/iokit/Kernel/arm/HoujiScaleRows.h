#ifndef HOUJI_SCALE_ROWS_H
#define HOUJI_SCALE_ROWS_H
/* Normal cached row buffers, non-overlapping; output is 8-byte aligned.
 * Exact nearest-neighbour 15:16 expansion: [a..o] becomes [a,a..o]. */
static inline uint64_t houji_scale_load64(const uint32_t *input)
{
 uint64_t value;__builtin_memcpy(&value,input,sizeof(value));
 /* Force the unaligned-safe load result to an integer register. */
 __asm__ volatile("" : "+r"(value));return value;
}
static inline bool houji_scale_row_15_16(uint32_t *output,const uint32_t *input,
    uint32_t inputWidth,uint32_t outputWidth)
{
 if(!inputWidth || inputWidth%15 || ((uintptr_t)output&7) ||
    (uint64_t)inputWidth*16!=(uint64_t)outputWidth*15)return false;
 for(uint32_t x=0;x<inputWidth;x+=15){
  const uint32_t *s=input+x;
  const uint64_t a=houji_scale_load64(s),b=houji_scale_load64(s+2),c=houji_scale_load64(s+4),
   d=houji_scale_load64(s+6),e=houji_scale_load64(s+8),f=houji_scale_load64(s+10),g=houji_scale_load64(s+12);
  volatile uint64_t *o=(volatile uint64_t *)output;
  o[0]=(a&0xffffffffull)|(a<<32);o[1]=(a>>32)|(b<<32);
  o[2]=(b>>32)|(c<<32);o[3]=(c>>32)|(d<<32);
  o[4]=(d>>32)|(e<<32);o[5]=(e>>32)|(f<<32);
  o[6]=(f>>32)|(g<<32);o[7]=(g>>32)|((uint64_t)s[14]<<32);output+=16;
 }
 return true;
}
#endif
