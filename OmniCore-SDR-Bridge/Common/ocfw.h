/*
 * OmniCore firmware update (OCFW): flash layout, image header and the USB
 * vendor requests shared by the V3F stage-0, the V5F bootloader and the V5F
 * application. Keep in sync with OmniCore-SDR-Host src/core/firmware.cpp.
 *
 * Flash (0x0 alias; data reads use FLASH_BASE + offset):
 *   0x00000  V3F stage-0: picks the V5F entry point, never updated over USB
 *   0x20000  V5F application (up to 0x1FF00 bytes)
 *   0x3FF00  application header, written last by the bootloader
 *   0x40000  V5F bootloader (56 KiB), never updated over USB
 *   0x4E000  boot request flag (8 KiB sector)
 * V5F cannot execute below 0x20000 (V3F zero-wait window), so the
 * bootloader sits above the application.
 */
#ifndef OCFW_H
#define OCFW_H

#include <stdint.h>

#define OCFW_FLASH_BASE         0x08000000UL
#define OCFW_BOOT_ADDR          0x00040000UL
#define OCFW_FLAG_ADDR          0x0004E000UL
#define OCFW_FLAG_SIZE          0x00002000UL
#define OCFW_APP_ADDR           0x00020000UL
#define OCFW_APP_REGION_SIZE    0x00020000UL
#define OCFW_HEADER_OFFSET      0x0001FF00UL  /* from OCFW_APP_ADDR */
#define OCFW_APP_MAX_SIZE       OCFW_HEADER_OFFSET
#define OCFW_PAGE_SIZE          256U
#define OCFW_CHUNK_MAX          512U          /* largest WRITE data stage: one USB3 EP0 packet */

/* Erased CH32H417 flash reads back as this pattern, not 0xFFFFFFFF. */
#define OCFW_ERASED_WORD        0xE339E339UL

#define OCFW_FLAG_MAGIC         0x544F4F42UL  /* "BOOT" */
#define OCFW_HEADER_MAGIC       0x5746434FUL  /* "OCFW" */
#define OCFW_HEADER_VERSION     1U
#define OCFW_HW_ID              0x48417B01UL  /* OmniCore SDR, CH32H417 V5F app */
#define OCFW_INFO_MAGIC         0x4946434FUL  /* "OCFI" */
#define OCFW_BOOTID_MAGIC       0x4C42434FUL  /* "OCBL" */
#define OCFW_BOOTID_OFFSET      0x40UL        /* ocfw_bootid_t inside the bootloader */
#define OCFW_INFO_VERSION       1U
#define OCFW_KEY                0x5AA5U       /* wValue of ENTER_BOOT / REBOOT */

/* Vendor requests (device recipient). INFO is IN; all others are OUT. */
#define OCFW_REQ_ENTER_BOOT     0xB0  /* app: set flag and reset into the bootloader */
#define OCFW_REQ_INFO           0xB1  /* app + bootloader: 64-byte ocfw_info_t */
#define OCFW_REQ_ERASE          0xB2  /* bootloader: erase the application region */
#define OCFW_REQ_WRITE          0xB3  /* bootloader: offset = wIndex:wValue, 256-byte aligned */
#define OCFW_REQ_COMMIT         0xB4  /* bootloader: 64-byte header, verify then write it */
#define OCFW_REQ_REBOOT         0xB5  /* bootloader: reset; stage-0 then picks the app */

#define OCFW_MODE_APP           1U
#define OCFW_MODE_BOOT          2U

#define OCFW_STATE_IDLE         0U
#define OCFW_STATE_BUSY         1U
#define OCFW_STATE_DONE         2U
#define OCFW_STATE_ERROR        3U

#define OCFW_ERR_NONE           0U
#define OCFW_ERR_REQUEST        1U  /* malformed request */
#define OCFW_ERR_RANGE          2U  /* offset/length outside the application */
#define OCFW_ERR_SEQUENCE       3U  /* WRITE before ERASE, COMMIT before data */
#define OCFW_ERR_FLASH          4U  /* erase/program failed or read-back mismatch */
#define OCFW_ERR_HEADER         5U  /* header magic/hw id/size/crc rejected */
#define OCFW_ERR_CRC            6U  /* flash image does not match header crc */

/* Application status bits (ocfw_info_t.app_status). */
#define OCFW_APP_PROGRAMMED     0x01U  /* first word is not erased */
#define OCFW_APP_HEADER_OK      0x02U  /* header at 0x3FF00 is valid */
#define OCFW_APP_IMAGE_OK       0x04U  /* image crc matches the header */

typedef struct __attribute__((packed))
{
    uint32_t magic;            /* OCFW_HEADER_MAGIC */
    uint32_t header_version;   /* OCFW_HEADER_VERSION */
    uint32_t hw_id;            /* OCFW_HW_ID */
    uint32_t image_size;       /* bytes from OCFW_APP_ADDR */
    uint32_t image_crc32;      /* CRC-32/ISO-HDLC of the image */
    uint32_t fw_version;       /* OMNICORE_VERSION_PACK */
    uint32_t build_unix;       /* build time, seconds since 1970 */
    char     build_id[24];     /* NUL-terminated git describe */
    uint32_t reserved[2];
    uint32_t header_crc32;     /* over the 60 bytes above */
} ocfw_header_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;            /* OCFW_INFO_MAGIC */
    uint16_t info_version;     /* OCFW_INFO_VERSION */
    uint8_t  mode;             /* OCFW_MODE_* */
    uint8_t  state;            /* OCFW_STATE_*, bootloader operations */
    uint32_t running_version;  /* version of the code answering */
    uint32_t boot_version;     /* bootloader version, 0 if unknown */
    uint32_t app_version;      /* from the application header, 0 if none */
    uint32_t app_status;       /* OCFW_APP_* */
    uint32_t app_addr;         /* OCFW_APP_ADDR */
    uint32_t app_max_size;     /* OCFW_APP_MAX_SIZE */
    uint32_t chunk_max;        /* OCFW_CHUNK_MAX */
    uint32_t error;            /* OCFW_ERR_* of the last bootloader operation */
    uint32_t progress;         /* bytes programmed since ERASE */
    uint32_t hw_id;            /* OCFW_HW_ID */
    char     build_id[16];     /* running build, NUL-padded */
} ocfw_info_t;

/* Placed by the bootloader linker script at OCFW_BOOT_ADDR + OCFW_BOOTID_OFFSET
 * so stage-0 can see a bootloader is present and the app can report it. */
typedef struct __attribute__((packed))
{
    uint32_t magic;            /* OCFW_BOOTID_MAGIC */
    uint32_t version;          /* OMNICORE_BOOT_VERSION */
    char     build_id[16];
} ocfw_bootid_t;

/* RAM shared by stage-0 and the bootloader: the gap between the V3F RAM
 * (ends 0x200C0A00) and the V5F RAM (starts 0x200C0C00). No startup code
 * clears it and it survives a watchdog reset, so a bootloader that hung
 * leaves its last trace step here for the debugger. */
#define OCFW_SHARED_RAM         0x200C0B00UL
#define OCFW_ATTEMPT_MAGIC      0x4D545441UL  /* "ATTM" */
typedef struct
{
    uint32_t attempt;          /* OCFW_ATTEMPT_MAGIC while the bootloader runs */
    uint32_t trace;            /* last bootloader step reached (BOOT_TRACE_*) */
    uint32_t loops;            /* bootloader main-loop iterations */
    uint32_t fallbacks;        /* watchdog recoveries into the application */
} ocfw_shared_t;
#define OCFW_SHARED             ((volatile ocfw_shared_t *)OCFW_SHARED_RAM)

/* EP0 vendor requests 0xB0..0xB5, called by both USB handlers (USB2 and
 * USB3). Implemented by the application (hardware.c: INFO, ENTER_BOOT) and
 * by the bootloader (boot.c: all). Ocfw_Setup runs in the SETUP stage and
 * returns 0 to STALL; for IN requests it fills in[] and *in_len. OUT data
 * (at most OCFW_CHUNK_MAX bytes) is collected into Ocfw_OutBuffer(), then
 * Ocfw_OutDone() runs before the status stage. */
#define OCFW_IS_REQUEST(type, req)     (((type) & 0x7FU) == 0x40U && (req) >= OCFW_REQ_ENTER_BOOT && (req) <= OCFW_REQ_REBOOT)
uint8_t Ocfw_Setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length,
                   uint8_t *in, uint16_t *in_len);
uint8_t *Ocfw_OutBuffer(void);
void Ocfw_OutDone(uint16_t length);

uint32_t ocfw_crc32(uint32_t crc, const void *data, uint32_t len);
int ocfw_header_check(const ocfw_header_t *h);
const ocfw_header_t *ocfw_app_header(void);
uint32_t ocfw_app_status(int check_image);
int ocfw_boot_requested(void);
uint32_t ocfw_boot_version(void);
void ocfw_fill_info(ocfw_info_t *info, uint8_t mode, uint32_t app_status);
int ocfw_flag_write(uint32_t value);
int ocfw_flag_clear(void);
void ocfw_system_reset(void) __attribute__((noreturn));
/* About 3 s; stage-0 starts it before the bootloader, which must feed it. */
void ocfw_watchdog_start(void);
void ocfw_watchdog_feed(void);

#endif
