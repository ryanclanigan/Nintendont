#ifndef ASIX_TEST_PLATFORM
#define ASIX_TEST_PLATFORM
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int32_t s32;typedef int8_t s8;
#define ALIGNED(x) __attribute__((aligned(x)))
#define HW_TIMER 0
struct ipcmessage {u32 result;};
extern u32 host_ms;
u32 test_read32(u32 addr);
#define read32 test_read32
void mdelay(int ms);
int mqueue_create(void *store,int n);
int mqueue_recv(int q,struct ipcmessage **m,int flags);
int mqueue_send(int q,struct ipcmessage *m,int flags);
void *heap_alloc_aligned(int heap,int n,int alignment);
void Shutdown(void) __attribute__((noreturn));
int IOSC_GenerateRand(u8 *data,u32 len);
#define dbgprintf(...) ((void)0)
typedef struct {unsigned pos;} FIL;
typedef int FRESULT;
#define FR_OK 0
#define FA_READ 1
#define FA_OPEN_EXISTING 0
int f_open_main_drive(FIL *f,const char *path,int mode);
int f_read(FIL *f,void *data,unsigned max,unsigned *size);
int f_close(FIL *f);
#endif
