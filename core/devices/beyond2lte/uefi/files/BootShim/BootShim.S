/*
 * Project Silicium's Mu-Silicium BootShim/BootShim.S with HakiOS's payload copy (uefi/hakios.patch), and for bring-up, where nothing
 * else can show how far a run got: with FB_PROBE, 200 lines of colour in S-Boot's framebuffer (1440 pixels of 4 bytes a line).
 * Red: S-Boot started this image. Green: the UEFI image and the payload are in place, the jump to UEFI is next.
 */
#ifdef FB_PROBE
.macro fb_band color, line
  ldr   x7, =(FB_PROBE + \line * 5760)
  ldr   w8, =\color
  ldr   x9, =(200 * 1440)
1:
  str   w8, [x7], #4
  subs  x9, x9, #1
  b.ne  1b
  dsb   sy
.endm
#else
.macro fb_band color, line
.endm
#endif

_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE         /* Image Size */
  .quad 0               /* Flags */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  fb_band 0xFFFF0000, 1000
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop

#ifdef PAYLOAD_BASE
  /* x4 now points just past the FD: an optional "XNUP" payload may follow */
  ldr   w2, [x4]
  movz  w3, #0x4E58
  movk  w3, #0x5055, lsl #16
  cmp   w2, w3
  b.ne  _NoPayload
  ldr   w6, [x4, #4]    /* Payload Size */
  add   x6, x6, #23     /* Header + Round Up to 16 */
  and   x6, x6, #~15
  movz  x5, #(PAYLOAD_BASE >> 16), lsl #16

_PayloadLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _PayloadLoop

_NoPayload:
#endif
  ldr   x5, =FD_BASE

_Entry:
  fb_band 0xFF00FF00, 1200
  br    x5

_Dead:
  b     _Dead

/* fb_band's constants, or the assembler puts them after _Payload, where the UEFI image is taken from */
.ltorg

.text
.align 4

_Payload:
