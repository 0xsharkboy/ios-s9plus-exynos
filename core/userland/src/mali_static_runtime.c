/*
 * Minimal single-thread runtime for mali_static.c and Apple's generated MIG
 * user stubs. No dyld, libc, launchd, writable filesystem or GPU emulation.
 * Unexpected MIG cleanup paths stop the diagnostic instead of ignoring them.
 */
#include <mach/mach.h>
#include <mach/ndr.h>
#include <stddef.h>
#include <stdint.h>
#undef memcpy
#undef memset

static int console = -1;
static mach_port_t reply_port;
mach_port_t mach_task_self_;
NDR_record_t NDR_record = { .int_rep = NDR_INT_LITTLE_ENDIAN,
    .char_rep = NDR_CHAR_ASCII, .float_rep = NDR_FLOAT_IEEE };

static long
trap(long number, long a, long b, long c, long d, long e, long f, long g)
{
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x6 __asm__("x6") = g;
    __asm__ volatile ("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2),
        "r"(x3), "r"(x4), "r"(x5), "r"(x6) : "memory", "cc");
    return x0;
}

void
probe_put(const char *s)
{
    size_t length = 0;
    while (s[length]) length++;
    (void)trap(4, console, (long)s, length, 0, 0, 0, 0);
}

void
probe_hex(uint64_t value)
{
    char buffer[19];
    unsigned at = sizeof(buffer) - 1;
    buffer[at] = 0;
    do {
        buffer[--at] = "0123456789abcdef"[value & 15];
        value >>= 4;
    } while (value);
    probe_put("0x");
    probe_put(buffer + at);
}

void
probe_sleep(void)
{
    long interval[2] = { 0, 200000 };
    (void)trap(93, 0, 0, 0, 0, (long)interval, 0, 0);
}

__attribute__((noreturn)) void
probe_park(void)
{
    /* PID 1 must never exit. The loader ends this bounded hardware probe. */
    for (;;) probe_sleep();
}

static __attribute__((noreturn)) void
unexpected(const char *message)
{
    probe_put("mali_static: RUNTIME_FAILURE: ");
    probe_put(message);
    probe_put("\n");
    probe_park();
}

void
probe_runtime_init(void)
{
    console = (int)trap(5, (long)"/dev/console", 2, 0, 0, 0, 0, 0);
    mach_task_self_ = (mach_port_t)trap(-28, 0, 0, 0, 0, 0, 0, 0);
}

mach_port_t
probe_host_self(void)
{
    return (mach_port_t)trap(-29, 0, 0, 0, 0, 0, 0, 0);
}

void
probe_port_release(mach_port_t port)
{
    if (port) (void)trap(-18, mach_task_self_, port, 0, 0, 0, 0, 0);
}

void *
memcpy(void *to, const void *from, size_t length)
{
    unsigned char *d = to;
    const unsigned char *s = from;
    for (size_t i = 0; i < length; i++) d[i] = s[i];
    return to;
}

void *
memset(void *to, int value, size_t length)
{
    unsigned char *d = to;
    for (size_t i = 0; i < length; i++) d[i] = (unsigned char)value;
    return to;
}

void *
__memcpy_chk(void *to, const void *from, size_t length, size_t capacity)
{
    if (length > capacity) unexpected("MIG copy exceeds its buffer");
    return memcpy(to, from, length);
}

int
mig_strncpy(char *to, const char *from, int capacity)
{
    if (capacity <= 0) return 0;
    int i = 0;
    while (i < capacity - 1 && from[i]) { to[i] = from[i]; i++; }
    to[i++] = 0;
    return i;
}

int
mig_strncpy_zerofill(char *to, const char *from, int capacity)
{
    int used = mig_strncpy(to, from, capacity);
    for (int i = used; i < capacity; i++) to[i] = 0;
    return used;
}

mach_port_t
mig_get_reply_port(void)
{
    if (!reply_port) reply_port = (mach_port_t)trap(-26, 0, 0, 0, 0, 0, 0, 0);
    return reply_port;
}

void mig_put_reply_port(mach_port_t port) { (void)port; }
void mig_dealloc_reply_port(mach_port_t port) { (void)port; unexpected("MIG reply port rejected"); }
void
mach_msg_destroy(mach_msg_header_t *message)
{
    /* Generated MIG stubs also call this for ordinary simple error replies.
     * They have no descriptors or remote/voucher rights to destroy.
     */
    if (!(message->msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
        !message->msgh_remote_port && !message->msgh_voucher_port) return;
    probe_put("mali_static: rejected reply id="); probe_hex((uint32_t)message->msgh_id);
    probe_put(" bits="); probe_hex(message->msgh_bits);
    probe_put(" size="); probe_hex(message->msgh_size); probe_put("\n");
    const uint32_t *words = (const uint32_t *)message;
    unsigned count = message->msgh_size / 4;
    if (count > 16) count = 16;
    for (unsigned i = 0; i < count; i++) { probe_hex(words[i]); probe_put(" "); }
    probe_put("\n");
    unexpected("MIG reply requires unexpected cleanup");
}

boolean_t
voucher_mach_msg_set(mach_msg_header_t *message)
{
    /* This single-thread PID 1 has no voucher subsystem or inherited voucher. */
    (void)message;
    return FALSE;
}

mach_msg_return_t
mach_msg(mach_msg_header_t *message, mach_msg_option_t options,
    mach_msg_size_t send_size, mach_msg_size_t receive_size,
    mach_port_name_t receive_port, mach_msg_timeout_t timeout,
    mach_port_name_t notify)
{
    (void)timeout;
    /* XNU 8796 rejects the legacy trap for this SDK. Match Apple's scalar
     * mach_msg2 packing (osfmk/mach/message.h). All destinations in this probe
     * are kernel objects: host, IOKit main, iterator, service or user client.
     */
    uint32_t descriptors = 0;
    if ((options & MACH_SEND_MSG) && (message->msgh_bits & MACH_MSGH_BITS_COMPLEX))
        descriptors = ((mach_msg_base_t *)message)->body.msgh_descriptor_count;
    register uint64_t x16 __asm__("x16") = (uint64_t)-47;
    register uint64_t x0 __asm__("x0") = (uintptr_t)message;
    register uint64_t x1 __asm__("x1") = (uint64_t)options | MACH_SEND_TIMEOUT |
        MACH_RCV_TIMEOUT | 0x0000000200000000ull; /* MACH64_SEND_KOBJECT_CALL */
    register uint64_t x2 __asm__("x2") = ((uint64_t)send_size << 32) | message->msgh_bits;
    register uint64_t x3 __asm__("x3") = ((uint64_t)message->msgh_local_port << 32) | message->msgh_remote_port;
    register uint64_t x4 __asm__("x4") = ((uint64_t)(uint32_t)message->msgh_id << 32) | message->msgh_voucher_port;
    register uint64_t x5 __asm__("x5") = ((uint64_t)receive_port << 32) | descriptors;
    register uint64_t x6 __asm__("x6") = ((uint64_t)notify << 32) | receive_size;
    register uint64_t x7 __asm__("x7") = 2000;
    __asm__ volatile ("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2),
        "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x7) : "memory", "cc");
    return (mach_msg_return_t)x0;
}
