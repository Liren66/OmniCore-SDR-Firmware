/*
 * OmniCore SDR bridge firmware version.
 *
 * Bump OMNICORE_BRIDGE_VERSION_* for every CH32 application release. The
 * bootloader has its own version because it is only updated with WCH-Link.
 * Versions are reported by vendor request 0xB1, in bcdDevice and in the
 * .ocfw image header.
 */
#ifndef FW_VERSION_H
#define FW_VERSION_H

#define OMNICORE_BRIDGE_VERSION_MAJOR   1
#define OMNICORE_BRIDGE_VERSION_MINOR   0
#define OMNICORE_BRIDGE_VERSION_PATCH   0

#define OMNICORE_BOOT_VERSION_MAJOR     1
#define OMNICORE_BOOT_VERSION_MINOR     0
#define OMNICORE_BOOT_VERSION_PATCH     0

#define OMNICORE_VERSION_PACK(a, b, c)  (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))
/* bcdDevice: one BCD digit each for minor and patch, e.g. 1.2.3 -> 0x0123. */
#define OMNICORE_VERSION_BCD(a, b, c)   ((uint16_t)((((a) / 10) << 12) | (((a) % 10) << 8) | ((b) << 4) | (c)))

#define OMNICORE_BRIDGE_VERSION  OMNICORE_VERSION_PACK(OMNICORE_BRIDGE_VERSION_MAJOR, \
                                                       OMNICORE_BRIDGE_VERSION_MINOR, \
                                                       OMNICORE_BRIDGE_VERSION_PATCH)
#define OMNICORE_BOOT_VERSION    OMNICORE_VERSION_PACK(OMNICORE_BOOT_VERSION_MAJOR, \
                                                       OMNICORE_BOOT_VERSION_MINOR, \
                                                       OMNICORE_BOOT_VERSION_PATCH)

#define OMNICORE_STR_(x)  #x
#define OMNICORE_STR(x)   OMNICORE_STR_(x)
#define OMNICORE_BRIDGE_VERSION_STR  OMNICORE_STR(OMNICORE_BRIDGE_VERSION_MAJOR) "." \
                                     OMNICORE_STR(OMNICORE_BRIDGE_VERSION_MINOR) "." \
                                     OMNICORE_STR(OMNICORE_BRIDGE_VERSION_PATCH)
#define OMNICORE_BOOT_VERSION_STR    OMNICORE_STR(OMNICORE_BOOT_VERSION_MAJOR) "." \
                                     OMNICORE_STR(OMNICORE_BOOT_VERSION_MINOR) "." \
                                     OMNICORE_STR(OMNICORE_BOOT_VERSION_PATCH)

/* Short build identifier (git describe). The Makefile passes it; IDE builds
 * report "dev". */
#ifndef OMNICORE_BUILD_ID
#define OMNICORE_BUILD_ID "dev"
#endif

#endif
