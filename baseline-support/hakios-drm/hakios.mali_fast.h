/* Versioned, opt-in transport for the existing 40-byte Panfrost SUBMIT.
 * Capability probe has no side effects. A failed submission must NEVER be
 * replayed via the message interface: enqueue may have succeeded already.
 */
#ifndef HAKIOS_MALI_FAST_H
#define HAKIOS_MALI_FAST_H
#define HAKIOS_MALI_FAST_ABI          0x48465431u /* HFT1 */
#define HAKIOS_MALI_FAST_CAPS_TRAP    0u
#define HAKIOS_MALI_FAST_SUBMIT_TRAP  1u
#define HAKIOS_MALI_FAST_TRAPS        2u
#define HAKIOS_MALI_FAST_SUBMIT_BYTES 40u
#endif
