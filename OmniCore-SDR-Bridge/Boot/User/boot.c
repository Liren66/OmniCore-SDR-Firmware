/*
 * OmniCore V5F bootloader: flash update engine.
 *
 * The USB interrupt validates and queues one operation at a time; this file
 * runs it from the main loop and reports the result through ocfw_info_t.
 * Page 0 of the application is held in RAM and programmed only after the
 * whole image matched the header CRC, so an interrupted update leaves page 0
 * erased and stage-0 keeps starting the bootloader.
 */
#include <string.h>
#include "debug.h"
#include "boot.h"
#include "ocfw.h"
#include "ch32h417_usbss_device.h"
#include "ch32h417_usbhs_device.h"

/* Operations queued by the USB interrupt and run by Boot_Task(). */
#define BOOT_OP_NONE     0U
#define BOOT_OP_ERASE    1U
#define BOOT_OP_WRITE    2U
#define BOOT_OP_COMMIT   3U
#define BOOT_OP_REBOOT   4U

#define APP_FLASH(off)  (OCFW_FLASH_BASE + OCFW_APP_ADDR + (off))

static volatile uint8_t  Boot_Op = BOOT_OP_NONE;
static volatile uint8_t  Boot_State = OCFW_STATE_IDLE;
static volatile uint8_t  Boot_Error = OCFW_ERR_NONE;
static volatile uint32_t Boot_Progress;
static volatile uint32_t Boot_AppStatus;
static volatile uint32_t Boot_OpOffset;
static volatile uint32_t Boot_OpLength;
static __attribute__((aligned(4))) uint8_t Boot_Data[OCFW_CHUNK_MAX];

static uint32_t Page0[OCFW_PAGE_SIZE / 4];
static uint32_t PageBuf[OCFW_PAGE_SIZE / 4];
static uint8_t  Erased;
static uint8_t  Page0Valid;
static uint32_t WrittenEnd;
static uint8_t  Out_Request;
static uint16_t Out_Value, Out_Index, Out_Length;

/* Either controller finished enumeration (SET_CONFIGURATION). */
static uint8_t Host_Configured(void)
{
    return USBSS_DevEnumStatus || USBHS_DevEnumStatus;
}

/*********************************************************************
 * @fn      Boot_Init
 *
 * @brief   Reads the application state. The boot flag is consumed later,
 *          once a host has configured the device (Boot_Task).
 *
 * @return  none
 */
void Boot_Init(void)
{
    Boot_AppStatus = ocfw_app_status(1);
}

static uint8_t Program(uint32_t offset, const uint32_t *data, uint32_t length)
{
    if(FLASH_ROM_WRITE(APP_FLASH(offset), (uint32_t *)data, length) != FLASH_COMPLETE)
    {
        return OCFW_ERR_FLASH;
    }
    return memcmp((const void *)APP_FLASH(offset), data, length) ? OCFW_ERR_FLASH : OCFW_ERR_NONE;
}

static uint8_t Do_Erase(void)
{
    Erased = Page0Valid = 0;
    WrittenEnd = 0;
    Boot_Progress = 0;
    if(FLASH_ROM_ERASE(APP_FLASH(0), OCFW_APP_REGION_SIZE) != FLASH_COMPLETE)
    {
        return OCFW_ERR_FLASH;
    }
    Boot_AppStatus = ocfw_app_status(0);
    if(Boot_AppStatus & (OCFW_APP_PROGRAMMED | OCFW_APP_HEADER_OK))
    {
        return OCFW_ERR_FLASH;
    }
    Erased = 1;
    return OCFW_ERR_NONE;
}

static uint8_t Do_Write(uint32_t offset, uint32_t length)
{
    const uint8_t *data = Boot_Data;
    uint32_t end = offset + length;
    uint8_t err;

    if(!Erased)
    {
        return OCFW_ERR_SEQUENCE;
    }
    if(offset == 0)
    {
        memcpy(Page0, data, OCFW_PAGE_SIZE);
        Page0Valid = 1;
        data += OCFW_PAGE_SIZE;
        offset += OCFW_PAGE_SIZE;
        length -= OCFW_PAGE_SIZE;
    }
    if(length)
    {
        err = Program(offset, (const uint32_t *)data, length);
        if(err)
        {
            return err;
        }
    }
    if(end > WrittenEnd)
    {
        WrittenEnd = end;
    }
    Boot_Progress += (uint32_t)(data - Boot_Data) + length;
    return OCFW_ERR_NONE;
}

static uint8_t Do_Commit(void)
{
    ocfw_header_t h;
    uint32_t crc;
    uint8_t err;

    memcpy(&h, Boot_Data, sizeof(h));
    if(!ocfw_header_check(&h))
    {
        return OCFW_ERR_HEADER;
    }
    if(!Erased || !Page0Valid || h.image_size > WrittenEnd)
    {
        return OCFW_ERR_SEQUENCE;
    }
    crc = ocfw_crc32(0, Page0, h.image_size < OCFW_PAGE_SIZE ? h.image_size : OCFW_PAGE_SIZE);
    if(h.image_size > OCFW_PAGE_SIZE)
    {
        crc = ocfw_crc32(crc, (const void *)APP_FLASH(OCFW_PAGE_SIZE), h.image_size - OCFW_PAGE_SIZE);
    }
    if(crc != h.image_crc32)
    {
        return OCFW_ERR_CRC;
    }

    /* Page 0 makes the image bootable; the header only describes it. */
    err = Program(0, Page0, OCFW_PAGE_SIZE);
    if(err)
    {
        return err;
    }
    memset(PageBuf, 0xFF, sizeof(PageBuf));
    memcpy(PageBuf, &h, sizeof(h));
    err = Program(OCFW_HEADER_OFFSET, PageBuf, OCFW_PAGE_SIZE);
    if(err)
    {
        return err;
    }
    Erased = 0;
    Boot_AppStatus = ocfw_app_status(1);
    return (Boot_AppStatus & OCFW_APP_IMAGE_OK) ? OCFW_ERR_NONE : OCFW_ERR_CRC;
}

/*********************************************************************
 * @fn      Boot_Task
 *
 * @brief   Runs the queued operation outside the USB interrupt.
 *
 * @return  none
 */
void Boot_Task(void)
{
    static uint8_t flag_cleared;
    uint8_t op = Boot_Op;
    uint8_t err = OCFW_ERR_NONE;

    /* Consume the boot request once a host has configured us: a power cycle
     * without an update returns to a programmed application. */
    if(!flag_cleared && Host_Configured())
    {
        BOOT_TRACE(BOOT_TRACE_CONFIGURED);
        Delay_Ms(200);
        BOOT_TRACE(BOOT_TRACE_FLAG_CLEAR);
        ocfw_flag_clear();
        BOOT_TRACE(BOOT_TRACE_FLAG_DONE);
        flag_cleared = 1;
    }

    switch(op)
    {
    case BOOT_OP_NONE:
        return;
    case BOOT_OP_ERASE:
        ocfw_watchdog_feed();
        err = Do_Erase();
        break;
    case BOOT_OP_WRITE:
        err = Do_Write(Boot_OpOffset, Boot_OpLength);
        break;
    case BOOT_OP_COMMIT:
        err = Do_Commit();
        break;
    case BOOT_OP_REBOOT:
        Delay_Ms(20);
        ocfw_system_reset();
    default:
        err = OCFW_ERR_REQUEST;
        break;
    }
    Boot_Error = err;
    Boot_Op = BOOT_OP_NONE;
    Boot_State = err ? OCFW_STATE_ERROR : OCFW_STATE_DONE;
}

/*********************************************************************
 * @fn      Boot_Timeout
 *
 * @brief   Returns to an installed application when no host configures the
 *          bootloader within 10 s (no USB3 link and USB2 unusable, e.g. D+/D-
 *          wired to WCH-Link: they are PB8/PB9 = SWCLK/SWDIO). Clearing the
 *          flag makes the way back independent of the watchdog and RAM.
 *
 * @return  none
 */
void Boot_Timeout(void)
{
    static uint32_t idle_ms;

    if(Host_Configured() || !(Boot_AppStatus & OCFW_APP_PROGRAMMED))
    {
        return;
    }
    Delay_Ms(1);
    if(++idle_ms < 10000)
    {
        return;
    }
    BOOT_TRACE(BOOT_TRACE_TIMEOUT);
    ocfw_flag_clear();
    ocfw_system_reset();
}

/*********************************************************************
 * @fn      Boot_Accept
 *
 * @brief   SETUP-stage check of a vendor OUT request (interrupt context).
 *
 * @return  1 to accept, 0 to STALL
 */
static uint8_t Boot_Accept(uint8_t request, uint16_t value, uint16_t index, uint16_t length)
{
    uint32_t offset = ((uint32_t)index << 16) | value;

    if(Boot_State == OCFW_STATE_BUSY || Boot_Op != BOOT_OP_NONE)
    {
        return 0;
    }
    switch(request)
    {
    case OCFW_REQ_ENTER_BOOT:
    case OCFW_REQ_REBOOT:
        return value == OCFW_KEY && length == 0;
    case OCFW_REQ_ERASE:
        return length == 0;
    case OCFW_REQ_WRITE:
        return length && length <= OCFW_CHUNK_MAX &&
               !(offset % OCFW_PAGE_SIZE) && !(length % OCFW_PAGE_SIZE) &&
               offset + length <= OCFW_APP_MAX_SIZE;
    case OCFW_REQ_COMMIT:
        return length == sizeof(ocfw_header_t);
    default:
        return 0;
    }
}

/*********************************************************************
 * @fn      Boot_Queue
 *
 * @brief   Hands an accepted request to Boot_Task() after its data stage.
 *
 * @return  none
 */
static void Boot_Queue(uint8_t request, uint16_t value, uint16_t index, uint16_t length)
{
    static const uint8_t ops[] =
    {
        BOOT_OP_NONE, BOOT_OP_NONE, BOOT_OP_ERASE, BOOT_OP_WRITE, BOOT_OP_COMMIT, BOOT_OP_REBOOT,
    };

    if(request < OCFW_REQ_ENTER_BOOT || request > OCFW_REQ_REBOOT || !ops[request - OCFW_REQ_ENTER_BOOT])
    {
        return;
    }
    Boot_OpOffset = ((uint32_t)index << 16) | value;
    Boot_OpLength = length;
    Boot_Error = OCFW_ERR_NONE;
    Boot_State = OCFW_STATE_BUSY;
    Boot_Op = ops[request - OCFW_REQ_ENTER_BOOT];
}

static uint16_t Boot_Info(uint8_t *out)
{
    ocfw_info_t *info = (ocfw_info_t *)out;

    ocfw_fill_info(info, OCFW_MODE_BOOT, Boot_AppStatus);
    info->state = Boot_State;
    info->error = Boot_Error;
    info->progress = Boot_Progress;
    return sizeof(*info);
}

/*********************************************************************
 * @fn      Ocfw_Setup
 *
 * @brief   Firmware update vendor requests from the USB2/USB3 handlers
 *          (interrupt context, see ocfw.h).
 *
 * @return  1 to accept, 0 to STALL
 */
uint8_t Ocfw_Setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length,
                   uint8_t *in, uint16_t *in_len)
{
    if(type == 0xC0 && request == OCFW_REQ_INFO && length)
    {
        *in_len = Boot_Info(in);
        return 1;
    }
    if(type != 0x40 || !Boot_Accept(request, value, index, length))
    {
        return 0;
    }
    if(length == 0)
    {
        Boot_Queue(request, value, index, 0);
        return 1;
    }
    Out_Request = request;
    Out_Value = value;
    Out_Index = index;
    Out_Length = length;
    return 1;
}

uint8_t *Ocfw_OutBuffer(void)
{
    return Boot_Data;
}

void Ocfw_OutDone(uint16_t length)
{
    if(length != Out_Length)
    {
        Boot_Error = OCFW_ERR_REQUEST;
        Boot_State = OCFW_STATE_ERROR;
        return;
    }
    Boot_Queue(Out_Request, Out_Value, Out_Index, length);
}
