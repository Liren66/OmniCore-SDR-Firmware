/*
 * OmniCore firmware update helpers shared by stage-0, bootloader and app.
 * See ocfw.h for the flash layout and protocol.
 */
#include "ocfw.h"
#include "fw_version.h"
#include "ch32h417.h"
#include <stddef.h>
#include <string.h>

#define FLASH_PTR(addr)  ((const volatile uint32_t *)(OCFW_FLASH_BASE + (addr)))

/* CRC-32/ISO-HDLC (zlib), nibble table: small enough for the 16 KiB V3F. */
uint32_t ocfw_crc32(uint32_t crc, const void *data, uint32_t len)
{
    static const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    const uint8_t *p = (const uint8_t *)data;

    crc = ~crc;
    while (len--)
    {
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 0x0F];
        crc = (crc >> 4) ^ table[crc & 0x0F];
    }
    return ~crc;
}

int ocfw_header_check(const ocfw_header_t *h)
{
    return h->magic == OCFW_HEADER_MAGIC &&
           h->header_version == OCFW_HEADER_VERSION &&
           h->hw_id == OCFW_HW_ID &&
           h->image_size >= OCFW_PAGE_SIZE && h->image_size <= OCFW_APP_MAX_SIZE &&
           h->header_crc32 == ocfw_crc32(0, h, offsetof(ocfw_header_t, header_crc32));
}

const ocfw_header_t *ocfw_app_header(void)
{
    return (const ocfw_header_t *)FLASH_PTR(OCFW_APP_ADDR + OCFW_HEADER_OFFSET);
}

uint32_t ocfw_app_status(int check_image)
{
    const ocfw_header_t *h = ocfw_app_header();
    uint32_t first = *FLASH_PTR(OCFW_APP_ADDR);
    uint32_t status = 0;

    if (first != OCFW_ERASED_WORD && first != 0xFFFFFFFFUL)
        status |= OCFW_APP_PROGRAMMED;
    if (ocfw_header_check(h))
    {
        status |= OCFW_APP_HEADER_OK;
        if (check_image &&
            ocfw_crc32(0, (const void *)FLASH_PTR(OCFW_APP_ADDR), h->image_size) == h->image_crc32)
            status |= OCFW_APP_IMAGE_OK;
    }
    return status;
}

int ocfw_boot_requested(void)
{
    return *FLASH_PTR(OCFW_FLAG_ADDR) == OCFW_FLAG_MAGIC;
}

uint32_t ocfw_boot_version(void)
{
    const ocfw_bootid_t *id = (const ocfw_bootid_t *)FLASH_PTR(OCFW_BOOT_ADDR + OCFW_BOOTID_OFFSET);

    return id->magic == OCFW_BOOTID_MAGIC ? id->version : 0;
}

void ocfw_fill_info(ocfw_info_t *info, uint8_t mode, uint32_t app_status)
{
    const ocfw_header_t *h = ocfw_app_header();

    memset(info, 0, sizeof(*info));
    info->magic = OCFW_INFO_MAGIC;
    info->info_version = OCFW_INFO_VERSION;
    info->mode = mode;
    info->running_version = (mode == OCFW_MODE_BOOT) ? OMNICORE_BOOT_VERSION : OMNICORE_BRIDGE_VERSION;
    info->boot_version = ocfw_boot_version();
    info->app_version = (app_status & OCFW_APP_HEADER_OK) ? h->fw_version : 0;
    info->app_status = app_status;
    info->app_addr = OCFW_APP_ADDR;
    info->app_max_size = OCFW_APP_MAX_SIZE;
    info->chunk_max = OCFW_CHUNK_MAX;
    info->hw_id = OCFW_HW_ID;
    strncpy(info->build_id, OMNICORE_BUILD_ID, sizeof(info->build_id));
}

/* The flag sector is erased whole, then its first page holds the value.
 * Code runs from ITCM, so programming the shared flash is safe here. */
int ocfw_flag_write(uint32_t value)
{
    static uint32_t page[OCFW_PAGE_SIZE / 4];

    if (FLASH_ROM_ERASE(OCFW_FLASH_BASE + OCFW_FLAG_ADDR, OCFW_FLAG_SIZE) != FLASH_COMPLETE)
        return -1;
    memset(page, 0xFF, sizeof(page));
    page[0] = value;
    if (FLASH_ROM_WRITE(OCFW_FLASH_BASE + OCFW_FLAG_ADDR, page, sizeof(page)) != FLASH_COMPLETE)
        return -1;
    return *FLASH_PTR(OCFW_FLAG_ADDR) == value ? 0 : -1;
}

int ocfw_flag_clear(void)
{
    if (!ocfw_boot_requested())
        return 0;
    return FLASH_ROM_ERASE(OCFW_FLASH_BASE + OCFW_FLAG_ADDR, OCFW_FLAG_SIZE) == FLASH_COMPLETE &&
           !ocfw_boot_requested() ? 0 : -1;
}

/* Firmware-initiated restarts use the independent watchdog: it resets the
 * whole chip (both cores, USB PHY), so stage-0 always runs again no matter
 * which core asks. */
void ocfw_system_reset(void)
{
    __disable_irq();
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_4);
    IWDG_SetReload(1);
    IWDG_ReloadCounter();
    IWDG_Enable();
    while (1)
    {
    }
}

void ocfw_watchdog_start(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_128);
    IWDG_SetReload(1000);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void ocfw_watchdog_feed(void)
{
    IWDG_ReloadCounter();
}
