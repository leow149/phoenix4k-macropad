/********************************** (C) COPYRIGHT *******************************
* File Name          : CDC.C
* Author             : WCH
* Version            : V1.0
* Date               : 2017/03/01
* Description        : CH554 as CDC device to serial port, select serial port 1
*******************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ch554.h>
#include <ch554_usb.h>
#include <debug.h>

__xdata __at (0x0000) uint8_t  Ep0Buffer[DEFAULT_ENDP0_SIZE];      // Endpoint 0 OUT & IN buffer, must be an even address
__xdata __at (0x0040) uint8_t  Ep1Buffer[DEFAULT_ENDP1_SIZE];       //Endpoint 1 upload buffer
__xdata __at (0x0080) uint8_t  Ep2Buffer[2*MAX_PACKET_SIZE];        //Endpoint 2 IN & OUT buffer, must be an even address

uint16_t SetupLen;
uint8_t   SetupReq,Count,UsbConfig;
const uint8_t *  pDescr;                                                       //USB configuration flag
USB_SETUP_REQ   SetupReqBuf;                                                   //Temporary Setup package
#define UsbSetupBuf     ((PUSB_SETUP_REQ)Ep0Buffer)

#define  SET_LINE_CODING                0X20            // Configures DTE rate, stop-bits, parity, and number-of-character
#define  GET_LINE_CODING                0X21            // This request allows the host to find out the currently configured line coding.
#define  SET_CONTROL_LINE_STATE         0X22            // This request generates RS-232/V.24 style control signals.

// ---- Descriptors: composite device, interface 0 = HID (2 report IDs: keyboard + consumer
//      control, both on EP1 IN), interface 1 = HID vendor-defined config channel (EP2 IN/OUT,
//      interrupt) - same trick SayoDevice's own firmware uses (usage page 0xFFA0) so the OS's
//      built-in HID class driver binds it with zero extra drivers on every platform ----

__code uint8_t DevDesc[] = {
    0x12, 0x01,             // bLength=18, bDescriptorType=DEVICE
    0x10, 0x01,             // bcdUSB = 1.10
    0x00,                   // bDeviceClass = 0 (defined per-interface)
    0x00, 0x00,             // bDeviceSubClass, bDeviceProtocol
    DEFAULT_ENDP0_SIZE,     // bMaxPacketSize0
    0x86, 0x1a,             // idVendor  = 0x1A86 (WCH)
    0x22, 0x57,             // idProduct = 0x5722
    0x01, 0x02,             // bcdDevice = 2.01 (v2 firmware)
    0x01, 0x02, 0x03,       // iManufacturer, iProduct, iSerialNumber
    0x01                    // bNumConfigurations
};

__code uint8_t KeyboardReportDesc[] = {
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x06,             // Usage (Keyboard)
    0xA1, 0x01,             // Collection (Application)
    0x85, 0x01,             //   Report ID (1)
    0x05, 0x07,             //   Usage Page (Key Codes)
    0x19, 0xE0,             //   Usage Minimum (224)
    0x29, 0xE7,             //   Usage Maximum (231)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x01,             //   Logical Maximum (1)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x08,             //   Report Count (8)
    0x81, 0x02,             //   Input (Data,Var,Abs)   -- modifier byte
    0x95, 0x01,             //   Report Count (1)
    0x75, 0x08,             //   Report Size (8)
    0x81, 0x01,             //   Input (Const)          -- reserved byte
    0x95, 0x06,             //   Report Count (6)
    0x75, 0x08,             //   Report Size (8)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0xFF,             //   Logical Maximum (255)
    0x05, 0x07,             //   Usage Page (Key Codes)
    0x19, 0x00,             //   Usage Minimum (0)
    0x29, 0xFF,             //   Usage Maximum (255)
    0x81, 0x00,             //   Input (Data,Array)     -- 6-byte keycode array
    0xC0,                    // End Collection

    0x05, 0x0C,             // Usage Page (Consumer)
    0x09, 0x01,             // Usage (Consumer Control)
    0xA1, 0x01,             // Collection (Application)
    0x85, 0x02,             //   Report ID (2)
    0x19, 0x00,             //   Usage Minimum (0)
    0x2A, 0x3C, 0x02,        //   Usage Maximum (0x023C)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0x3C, 0x02,        //   Logical Maximum (0x023C)
    0x95, 0x01,             //   Report Count (1)
    0x75, 0x10,             //   Report Size (16)
    0x81, 0x00,             //   Input (Data,Array,Abs)
    0xC0                     // End Collection
};
#define KEYBOARD_REPORT_DESC_LEN 72

__code uint8_t HidDesc[] = {0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, KEYBOARD_REPORT_DESC_LEN, 0x00};

// Vendor-defined HID collection for the config channel: usage page 0xFF00, report ID 3,
// one 64-byte output report (host->device) and one 64-byte input report (device->host).
// Carries the exact same 0xB0-0xB3 command protocol as before, just inside HID reports
// instead of raw bulk transfers - every byte after the (HID-stripped) report ID is identical.
__code uint8_t VendorReportDesc[] = {
    0x06, 0x00, 0xFF,        // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,              // Usage (Vendor Usage 1)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x03,              //   Report ID (3)
    0x09, 0x02,              //   Usage (Vendor Usage 2) -- output
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x40,              //   Report Count (64)
    0x91, 0x02,              //   Output (Data,Var,Abs)
    0x09, 0x03,              //   Usage (Vendor Usage 3) -- input
    0x81, 0x02,              //   Input (Data,Var,Abs)
    0xC0                      // End Collection
};
#define VENDOR_REPORT_DESC_LEN 27

__code uint8_t VendorHidDesc[] = {0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, VENDOR_REPORT_DESC_LEN, 0x00};

__code uint8_t CfgDesc[] = {
    0x09, 0x02, 0x42, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, KEYBOARD_REPORT_DESC_LEN, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x09, 0x00, 0x0A,
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, VENDOR_REPORT_DESC_LEN, 0x00,
    0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
};

unsigned char __code LangDes[] = {0x04, 0x03, 0x09, 0x04};
unsigned char __code SerDes[] = {
    0x10, 0x03, 0x32, 0x00, 0x30, 0x00, 0x32, 0x00, 0x36, 0x00, 0x2D, 0x00,
    0x31, 0x00, 0x30, 0x00,   // "2026-10"
};
unsigned char __code Prod_Des[] = {
    0x14, 0x03, 0x50, 0x00, 0x68, 0x00, 0x6F, 0x00, 0x65, 0x00, 0x6E, 0x00,
    0x69, 0x00, 0x78, 0x00, 0x34, 0x00, 0x4B, 0x00,   // "Phoenix4K"
};
unsigned char __code Manuf_Des[] = {
    0x08, 0x03, 0x44, 0x00, 0x49, 0x00, 0x59, 0x00,   // "DIY"
};
__xdata uint8_t LineCoding[7] = {0x00, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x08};
volatile __idata uint8_t USBByteCount = 0;
volatile __idata uint8_t USBBufOutPoint = 0;
volatile __idata uint8_t UpPoint2_Busy = 0;
volatile __idata uint8_t Uart_Input_Point = 0, Uart_Output_Point = 0, UartByteCount = 0;

void Config_Uart1(uint8_t *cfg_uart) { (void)cfg_uart; }

uint8_t WriteDataFlash(uint8_t Addr, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    SAFE_MOD = 0x55;
    SAFE_MOD = 0xAA;
    GLOBAL_CFG |= bDATA_WE;
    SAFE_MOD = 0;
    ROM_ADDR_H = DATA_FLASH_ADDR >> 8;
    Addr <<= 1;
    for (i = 0; i < len; i++)
    {
        ROM_ADDR_L = Addr + i * 2;
        ROM_DATA_L = *(buf + i);
        if (ROM_STATUS & bROM_ADDR_OK)
        {
            ROM_CTRL = ROM_CMD_WRITE;
        }
        if ((ROM_STATUS ^ bROM_ADDR_OK) > 0) return i;
    }
    SAFE_MOD = 0x55;
    SAFE_MOD = 0xAA;
    GLOBAL_CFG &= ~bDATA_WE;
    SAFE_MOD = 0;
    return i;
}

uint8_t ReadDataFlash(uint8_t Addr, uint8_t len, uint8_t *buf)
{
    uint8_t i;
    ROM_ADDR_H = DATA_FLASH_ADDR >> 8;
    Addr <<= 1;
    for (i = 0; i < len; i++)
    {
        ROM_ADDR_L = Addr + i * 2;
        ROM_CTRL = ROM_CMD_READ;
        *(buf + i) = ROM_DATA_L;
    }
    return i;
}

#define NUM_KEYS   4
#define NUM_LAYERS 2
#define EE_MAGIC_ADDR   0
#define EE_VERSION_ADDR 1
#define EE_FNKEY_ADDR   2
#define EE_FLAGS_ADDR   3
#define EE_CONSUMER_BASE 4
#define EE_SLOTS_BASE    20
#define EE_SLOT_SIZE     13
#define EE_SIZE          128
#define EE_MAGIC_VAL     0xA5
#define EE_LAYOUT_VERSION 2
#define FW_VERSION_MAJOR 2
#define FW_VERSION_MINOR 0

#define consumer_addr(layer, key) (EE_CONSUMER_BASE + ((layer) * NUM_KEYS + (key)) * 2)
#define slot_addr(layer, key)     (EE_SLOTS_BASE + ((layer) * NUM_KEYS + (key)) * EE_SLOT_SIZE)

__xdata uint8_t EE[EE_SIZE];

__code const uint8_t KeyBit[NUM_KEYS] = {0x04, 0x10, 0x08, 0x02};

void FactoryReset(void)
{
    uint8_t i;
    for (i = 0; i < EE_SIZE; i++) { EE[i] = 0; }
    EE[EE_MAGIC_ADDR] = EE_MAGIC_VAL;
    EE[EE_VERSION_ADDR] = EE_LAYOUT_VERSION;
    EE[EE_FNKEY_ADDR] = 0xFF;
    for (i = 0; i < NUM_KEYS; i++)
    {
        uint8_t a = slot_addr(0, i);
        EE[a] = 1;
        EE[a + 1] = 0;
        EE[a + 2] = 0x1E + i;
    }
    for (i = 0; i < NUM_KEYS; i++)
    {
        uint8_t a = slot_addr(1, i);
        EE[a] = 1;
        EE[a + 1] = 0;
        EE[a + 2] = 0x68 + i;
    }
    WriteDataFlash(0, EE, EE_SIZE);
}

void LoadConfig(void)
{
    ReadDataFlash(0, EE_SIZE, EE);
    if (EE[EE_MAGIC_ADDR] != EE_MAGIC_VAL || EE[EE_VERSION_ADDR] != EE_LAYOUT_VERSION)
    {
        FactoryReset();
    }
}
void USBDeviceCfg()
{
    USB_CTRL = 0x00;                                                           //Clear USB control register
    USB_CTRL &= ~bUC_HOST_MODE;                                                //This bit selects the device mode
    USB_CTRL |=  bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;                    //USB device and internal pull-up enable, automatically return to NAK before interrupt flag is cleared
    USB_DEV_AD = 0x00;                                                         //Device address initialization
    //     USB_CTRL |= bUC_LOW_SPEED;
    //     UDEV_CTRL |= bUD_LOW_SPEED;                                                //Select low speed 1.5M mode
    USB_CTRL &= ~bUC_LOW_SPEED;
    UDEV_CTRL &= ~bUD_LOW_SPEED;                                             //Select full speed 12M mode, the default mode
    UDEV_CTRL = bUD_PD_DIS;  // Disable DP / DM pull-down resistor
    UDEV_CTRL |= bUD_PORT_EN;                                                  //Enable physical port
}
/*******************************************************************************
* Function Name  : USBDeviceIntCfg()
* Description    : USB device mode interrupt initialization
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void USBDeviceIntCfg()
{
    USB_INT_EN |= bUIE_SUSPEND;                                               //Enable device suspend interrupt
    USB_INT_EN |= bUIE_TRANSFER;                                              //Enable USB transfer completion interrupt
    USB_INT_EN |= bUIE_BUS_RST;                                               //Enable device mode USB bus reset interrupt
    USB_INT_FG |= 0x1F;                                                       //Clear interrupt flag
    IE_USB = 1;                                                               //Enable USB interrupt
    EA = 1;                                                                   //Allow microcontroller interrupt
}
/*******************************************************************************
* Function Name  : USBDeviceEndPointCfg()
* Description    : USB device mode endpoint configuration, simulation compatible HID device, in addition to endpoint 0 control transmission, also includes endpoint 2 batch upload
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
void USBDeviceEndPointCfg()
{
    // TODO: Is casting the right thing here? What about endianness?
    UEP1_DMA = (uint16_t) Ep1Buffer;                                                      //Endpoint 1 sends data transfer address
    UEP2_DMA = (uint16_t) Ep2Buffer;                                                      //Endpoint 2 IN data transfer address
    UEP2_3_MOD = 0xCC;                                                         //Endpoint 2/3 single buffer transceiver enable
    UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;                 //Endpoint 2 automatically flips the synchronization flag, IN transaction returns NAK, OUT returns ACK

    UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;                                 //Endpoint 1 automatically flips the synchronization flag, IN transaction returns NAK
    UEP0_DMA = (uint16_t) Ep0Buffer;                                                      //Endpoint 0 data transfer address
    UEP4_1_MOD = 0X40;                                                         //Endpoint 1 upload buffer; endpoint 0 single 64-byte send and receive buffer
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;                                 //Manual flip, OUT transaction returns ACK, IN transaction returns NAK
}
void DeviceInterrupt(void) __interrupt (INT_NO_USB)                       //USB interrupt service routine, using register set 1
{
    uint16_t len;
    if(UIF_TRANSFER)                                                            //USB transfer complete flag
    {
        switch (USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
        {
        case UIS_TOKEN_IN | 1:                                                  //endpoint 1# 端点中断上传
            UEP1_T_LEN = 0;
            UEP1_CTRL = UEP1_CTRL & ~ MASK_UEP_T_RES | UEP_T_RES_NAK;           //默认应答NAK
            break;
        case UIS_TOKEN_IN | 2:                                                  //endpoint 2# 端点批量上传
        {
            UEP2_T_LEN = 0;                                                    //预使用发送长度一定要清空
            UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_T_RES | UEP_T_RES_NAK;           //默认应答NAK
            UpPoint2_Busy = 0;                                                  //清除忙标志
        }
            break;
        case UIS_TOKEN_OUT | 2:                                                 //endpoint 3# 端点批量下传
            if ( U_TOG_OK )                                                     // 不同步的数据包将丢弃
            {
                USBByteCount = USB_RX_LEN;                          // Grads length of recieved data
                USBBufOutPoint = 0;                                             //Get data pointer reset
                UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_R_RES | UEP_R_RES_NAK;       //NAK after receiving a packet of data, the main function finishes processing, and the main function modifies the response mode
            }
            break;
        case UIS_TOKEN_SETUP | 0:                                                //SETUP事务
            len = USB_RX_LEN;
            if(len == (sizeof(USB_SETUP_REQ)))
            {
                SetupLen = ((uint16_t)UsbSetupBuf->wLengthH<<8) | (UsbSetupBuf->wLengthL);
                len = 0;                                                      // 默认为成功并且上传0长度
                SetupReq = UsbSetupBuf->bRequest;
                if ( ( UsbSetupBuf->bRequestType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )//非标准请求
                {
                    switch( SetupReq )
                    {
                    case GET_LINE_CODING:   //0x21  currently configured
                        pDescr = LineCoding;
                        len = sizeof(LineCoding);
                        len = SetupLen >= DEFAULT_ENDP0_SIZE ? DEFAULT_ENDP0_SIZE : SetupLen;  // 本次传输长度
                        memcpy(Ep0Buffer,pDescr,len);
                        SetupLen -= len;
                        pDescr += len;
                        break;
                    case SET_CONTROL_LINE_STATE:  //0x22  generates RS-232/V.24 style control signals
                        break;
                    case SET_LINE_CODING:      //0x20  Configure
                        break;
                    default:
                        len = 0xFF;  								 					                 /*Command not supported*/
                        break;
                    }
                }
                else                                                             //Standard request
                {
                    switch(SetupReq)                                             //Request code
                    {
                    case USB_GET_DESCRIPTOR:
                        switch(UsbSetupBuf->wValueH)
                        {
                        case 1:                                                       //设备描述符
                            pDescr = DevDesc;                                         //把设备描述符送到要发送的缓冲区
                            len = sizeof(DevDesc);
                            break;
                        case 2:                                                        //配置描述符
                            pDescr = CfgDesc;                                          //把设备描述符送到要发送的缓冲区
                            len = sizeof(CfgDesc);
                            break;
                        case 3:
                            if(UsbSetupBuf->wValueL == 0)
                            {
                                pDescr = LangDes;
                                len = sizeof(LangDes);
                            }
                            else if(UsbSetupBuf->wValueL == 1)
                            {
                                pDescr = Manuf_Des;
                                len = sizeof(Manuf_Des);
                            }
                            else if(UsbSetupBuf->wValueL == 2)
                            {
                                pDescr = Prod_Des;
                                len = sizeof(Prod_Des);
                            }
                            else
                            {
                                pDescr = SerDes;
                                len = sizeof(SerDes);
                            }
                            break;
                        case 0x22:                                              // HID report descriptor
                            if (UsbSetupBuf->wIndexL == 1) { pDescr = VendorReportDesc; len = sizeof(VendorReportDesc); }
                            else { pDescr = KeyboardReportDesc; len = sizeof(KeyboardReportDesc); }
                            break;
                        case 0x21:                                              // HID descriptor (standalone request)
                            if (UsbSetupBuf->wIndexL == 1) { pDescr = VendorHidDesc; len = sizeof(VendorHidDesc); }
                            else { pDescr = HidDesc; len = sizeof(HidDesc); }
                            break;
                        default:
                            len = 0xff;                                                //Unsupported command or error
                            break;
                        }
                        if ( SetupLen > len )
                        {
                            SetupLen = len;    //Limit total length
                        }
                        len = SetupLen >= DEFAULT_ENDP0_SIZE ? DEFAULT_ENDP0_SIZE : SetupLen;                            //This transmission length
                        memcpy(Ep0Buffer,pDescr,len);                                  //Load upload data
                        SetupLen -= len;
                        pDescr += len;
                        break;
                    case USB_SET_ADDRESS:
                        SetupLen = UsbSetupBuf->wValueL;                              //Temporary storage of USB device address
                        break;
                    case USB_GET_CONFIGURATION:
                        Ep0Buffer[0] = UsbConfig;
                        if ( SetupLen >= 1 )
                        {
                            len = 1;
                        }
                        break;
                    case USB_SET_CONFIGURATION:
                        UsbConfig = UsbSetupBuf->wValueL;
                        break;
                    case USB_GET_INTERFACE:
                        break;
                    case USB_CLEAR_FEATURE:                                            //Clear Feature
                        if( ( UsbSetupBuf->bRequestType & 0x1F ) == USB_REQ_RECIP_DEVICE )                  /* Remove device */
                        {
                            if( ( ( ( uint16_t )UsbSetupBuf->wValueH << 8 ) | UsbSetupBuf->wValueL ) == 0x01 )
                            {
                                if( CfgDesc[ 7 ] & 0x20 )
                                {
                                    /* 唤醒 */
                                }
                                else
                                {
                                    len = 0xFF;                                        /* 操作失败 */
                                }
                            }
                            else
                            {
                                len = 0xFF;                                            /* 操作失败 */
                            }
                        }
                        else if ( ( UsbSetupBuf->bRequestType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )// 端点
                        {
                            switch( UsbSetupBuf->wIndexL )
                            {
                            case 0x83:
                                UEP3_CTRL = UEP3_CTRL & ~ ( bUEP_T_TOG | MASK_UEP_T_RES ) | UEP_T_RES_NAK;
                                break;
                            case 0x03:
                                UEP3_CTRL = UEP3_CTRL & ~ ( bUEP_R_TOG | MASK_UEP_R_RES ) | UEP_R_RES_ACK;
                                break;
                            case 0x82:
                                UEP2_CTRL = UEP2_CTRL & ~ ( bUEP_T_TOG | MASK_UEP_T_RES ) | UEP_T_RES_NAK;
                                break;
                            case 0x02:
                                UEP2_CTRL = UEP2_CTRL & ~ ( bUEP_R_TOG | MASK_UEP_R_RES ) | UEP_R_RES_ACK;
                                break;
                            case 0x81:
                                UEP1_CTRL = UEP1_CTRL & ~ ( bUEP_T_TOG | MASK_UEP_T_RES ) | UEP_T_RES_NAK;
                                break;
                            case 0x01:
                                UEP1_CTRL = UEP1_CTRL & ~ ( bUEP_R_TOG | MASK_UEP_R_RES ) | UEP_R_RES_ACK;
                                break;
                            default:
                                len = 0xFF;                                         // 不支持的端点
                                break;
                            }
                        }
                        else
                        {
                            len = 0xFF;                                                // 不是端点不支持
                        }
                        break;
                    case USB_SET_FEATURE:                                          /* Set Feature */
                        if( ( UsbSetupBuf->bRequestType & 0x1F ) == USB_REQ_RECIP_DEVICE )                  /* 设置设备 */
                        {
                            if( ( ( ( uint16_t )UsbSetupBuf->wValueH << 8 ) | UsbSetupBuf->wValueL ) == 0x01 )
                            {
                                if( CfgDesc[ 7 ] & 0x20 )
                                {
                                    /* 休眠 */
#ifdef DE_PRINTF
                                    printf( "suspend\n" );                                                             //睡眠状态
#endif
                                    while ( XBUS_AUX & bUART0_TX )
                                    {
                                        ;    //等待发送完成
                                    }
                                    SAFE_MOD = 0x55;
                                    SAFE_MOD = 0xAA;
                                    WAKE_CTRL = bWAK_BY_USB | bWAK_RXD0_LO | bWAK_RXD1_LO;                      //USB或者RXD0/1有信号时可被唤醒
                                    PCON |= PD;                                                                 //睡眠
                                    SAFE_MOD = 0x55;
                                    SAFE_MOD = 0xAA;
                                    WAKE_CTRL = 0x00;
                                }
                                else
                                {
                                    len = 0xFF;                                        /* 操作失败 */
                                }
                            }
                            else
                            {
                                len = 0xFF;                                            /* 操作失败 */
                            }
                        }
                        else if( ( UsbSetupBuf->bRequestType & 0x1F ) == USB_REQ_RECIP_ENDP )             /* 设置端点 */
                        {
                            if( ( ( ( uint16_t )UsbSetupBuf->wValueH << 8 ) | UsbSetupBuf->wValueL ) == 0x00 )
                            {
                                switch( ( ( uint16_t )UsbSetupBuf->wIndexH << 8 ) | UsbSetupBuf->wIndexL )
                                {
                                case 0x83:
                                    UEP3_CTRL = UEP3_CTRL & (~bUEP_T_TOG) | UEP_T_RES_STALL;/* 设置端点3 IN STALL */
                                    break;
                                case 0x03:
                                    UEP3_CTRL = UEP3_CTRL & (~bUEP_R_TOG) | UEP_R_RES_STALL;/* 设置端点3 OUT Stall */
                                    break;
                                case 0x82:
                                    UEP2_CTRL = UEP2_CTRL & (~bUEP_T_TOG) | UEP_T_RES_STALL;/* 设置端点2 IN STALL */
                                    break;
                                case 0x02:
                                    UEP2_CTRL = UEP2_CTRL & (~bUEP_R_TOG) | UEP_R_RES_STALL;/* 设置端点2 OUT Stall */
                                    break;
                                case 0x81:
                                    UEP1_CTRL = UEP1_CTRL & (~bUEP_T_TOG) | UEP_T_RES_STALL;/* 设置端点1 IN STALL */
                                    break;
                                case 0x01:
                                    UEP1_CTRL = UEP1_CTRL & (~bUEP_R_TOG) | UEP_R_RES_STALL;/* 设置端点1 OUT Stall */
                                default:
                                    len = 0xFF;                                    /* 操作失败 */
                                    break;
                                }
                            }
                            else
                            {
                                len = 0xFF;                                      /* 操作失败 */
                            }
                        }
                        else
                        {
                            len = 0xFF;                                          /* 操作失败 */
                        }
                        break;
                    case USB_GET_STATUS:
                        Ep0Buffer[0] = 0x00;
                        Ep0Buffer[1] = 0x00;
                        if ( SetupLen >= 2 )
                        {
                            len = 2;
                        }
                        else
                        {
                            len = SetupLen;
                        }
                        break;
                    default:
                        len = 0xff;                                                    //操作失败
                        break;
                    }
                }
            }
            else
            {
                len = 0xff;                                                         //包长度错误
            }
            if(len == 0xff)
            {
                SetupReq = 0xFF;
                UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;//STALL
            }
            else if(len <= DEFAULT_ENDP0_SIZE)                                                       //上传数据或者状态阶段返回0长度包
            {
                UEP0_T_LEN = len;
                UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;//默认数据包是DATA1，返回应答ACK
            }
            else
            {
                UEP0_T_LEN = 0;  //虽然尚未到状态阶段，但是提前预置上传0长度数据包以防主机提前进入状态阶段
                UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;//默认数据包是DATA1,返回应答ACK
            }
            break;
        case UIS_TOKEN_IN | 0:                                                      //endpoint0 IN
            switch(SetupReq)
            {
            case USB_GET_DESCRIPTOR:
                len = SetupLen >= DEFAULT_ENDP0_SIZE ? DEFAULT_ENDP0_SIZE : SetupLen;                                 //本次传输长度
                memcpy( Ep0Buffer, pDescr, len );                                   //加载上传数据
                SetupLen -= len;
                pDescr += len;
                UEP0_T_LEN = len;
                UEP0_CTRL ^= bUEP_T_TOG;                                             //同步标志位翻转
                break;
            case USB_SET_ADDRESS:
                USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | SetupLen;
                UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            default:
                UEP0_T_LEN = 0;                                                      //状态阶段完成中断或者是强制上传0长度数据包结束控制传输
                UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            }
            break;
        case UIS_TOKEN_OUT | 0:  // endpoint0 OUT
            if(SetupReq ==SET_LINE_CODING)  //设置串口属性
            {
                if( U_TOG_OK )
                {
                    memcpy(LineCoding,UsbSetupBuf,USB_RX_LEN);
                    Config_Uart1(LineCoding);
                    UEP0_T_LEN = 0;
                    UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_ACK;  // 准备上传0包
                }
            }
            else
            {
                UEP0_T_LEN = 0;
                UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_NAK;  //状态阶段，对IN响应NAK
            }
            break;



        default:
            break;
        }
        UIF_TRANSFER = 0;                                                           //写0清空中断
    }
    if(UIF_BUS_RST)                                                                 //设备模式USB总线复位中断
    {
#ifdef DE_PRINTF
        printf( "reset\n" );                                                             //睡眠状态
#endif
        UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
        UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
        USB_DEV_AD = 0x00;
        UIF_SUSPEND = 0;
        UIF_TRANSFER = 0;
        UIF_BUS_RST = 0;                                                             //清中断标志
        Uart_Input_Point = 0;   //循环缓冲区输入指针
        Uart_Output_Point = 0;  //循环缓冲区读出指针
        UartByteCount = 0;      //当前缓冲区剩余待取字节数
        USBByteCount = 0;       //USB端点收到的长度
        UsbConfig = 0;          //清除配置值
        UpPoint2_Busy = 0;
    }
    if (UIF_SUSPEND)                                                                 //USB总线挂起/唤醒完成
    {
        UIF_SUSPEND = 0;
        if ( USB_MIS_ST & bUMS_SUSPEND )                                             //挂起
        {
#ifdef DE_PRINTF
            printf( "suspend\n" );                                                             //睡眠状态
#endif
            while ( XBUS_AUX & bUART0_TX )
            {
                ;    //等待发送完成
            }
            SAFE_MOD = 0x55;
            SAFE_MOD = 0xAA;
            WAKE_CTRL = bWAK_BY_USB | bWAK_RXD0_LO | bWAK_RXD1_LO;                      //USB或者RXD0/1有信号时可被唤醒
            PCON |= PD;                                                                 //睡眠
            SAFE_MOD = 0x55;
            SAFE_MOD = 0xAA;
            WAKE_CTRL = 0x00;
        }
    }
    else {                                                                             //意外的中断,不可能发生的情况
        USB_INT_FG = 0xFF;                                                             //清中断标志

    }
}
#define DEBOUNCE_COUNT 4

uint8_t DebounceCtr[NUM_KEYS];
uint8_t KeyState[NUM_KEYS];

void SendKeyboardReport(uint8_t mod, uint8_t *keys6)
{
    while (UEP1_T_LEN != 0) { }
    Ep1Buffer[0] = 1;
    Ep1Buffer[1] = mod;
    Ep1Buffer[2] = 0;
    memcpy(Ep1Buffer + 3, keys6, 6);
    UEP1_T_LEN = 9;
    UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

void SendConsumerReport(uint16_t code)
{
    while (UEP1_T_LEN != 0) { }
    Ep1Buffer[0] = 2;
    Ep1Buffer[1] = code & 0xFF;
    Ep1Buffer[2] = code >> 8;
    UEP1_T_LEN = 3;
    UEP1_CTRL = UEP1_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
}

void PlayMacro(uint8_t slot_a)
{
    uint8_t len = EE[slot_a];
    uint8_t zeros[6] = {0, 0, 0, 0, 0, 0};
    uint8_t step, p[6];
    for (step = 0; step < len && step < 6; step++)
    {
        uint8_t mod = EE[slot_a + 1 + step * 2];
        uint8_t key = EE[slot_a + 2 + step * 2];
        memset(p, 0, 6);
        p[0] = key;
        SendKeyboardReport(mod, p);
        mDelaymS(8);
        SendKeyboardReport(0, zeros);
        mDelaymS(8);
    }
}

main()
{
    uint8_t i, p3, pressed, layer, fnkey;
    uint8_t held_report[6] = {0, 0, 0, 0, 0, 0};
    uint8_t last_held[6] = {0, 0, 0, 0, 0, 0};
    uint8_t last_mod = 0;
    uint8_t ReportDirty = 0;

    CfgFsys();
    mDelaymS(5);

    P3_MOD_OC |= 0x3F;
    P3_DIR_PU |= 0x3F;

    LoadConfig();
    for (i = 0; i < NUM_KEYS; i++) { DebounceCtr[i] = 0; KeyState[i] = 0; }

    USBDeviceCfg();
    USBDeviceEndPointCfg();
    USBDeviceIntCfg();
    UEP0_T_LEN = 0;
    UEP1_T_LEN = 0;
    UEP2_T_LEN = 0;

    while (1)
    {
        p3 = P3;
        fnkey = EE[EE_FNKEY_ADDR];
        layer = (fnkey < NUM_KEYS && KeyState[fnkey]) ? 1 : 0;

        for (i = 0; i < NUM_KEYS; i++)
        {
            pressed = (p3 & KeyBit[i]) ? 0 : 1;
            if (pressed != KeyState[i])
            {
                if (++DebounceCtr[i] >= DEBOUNCE_COUNT)
                {
                    KeyState[i] = pressed;
                    DebounceCtr[i] = 0;

                    if (i != fnkey && UsbConfig)
                    {
                        uint8_t ca = consumer_addr(layer, i);
                        uint16_t ccode = EE[ca] | (EE[ca + 1] << 8);
                        if (ccode != 0)
                        {
                            SendConsumerReport(pressed ? ccode : 0);
                        }
                        else if (pressed)
                        {
                            uint8_t sa = slot_addr(layer, i);
                            if (EE[sa] > 1) { PlayMacro(sa); }
                        }
                    }
                    ReportDirty = 1;
                }
            }
            else
            {
                DebounceCtr[i] = 0;
            }
        }

        if (ReportDirty && UsbConfig && UEP1_T_LEN == 0)
        {
            uint8_t mod = 0, slot = 0;
            memset(held_report, 0, 6);
            for (i = 0; i < NUM_KEYS && slot < 6; i++)
            {
                if (i == fnkey || !KeyState[i]) continue;
                {
                    uint8_t ca = consumer_addr(layer, i);
                    if (EE[ca] != 0 || EE[ca + 1] != 0) continue;
                }
                {
                    uint8_t sa = slot_addr(layer, i);
                    if (EE[sa] == 1)
                    {
                        mod |= EE[sa + 1];
                        held_report[slot++] = EE[sa + 2];
                    }
                }
            }
            ReportDirty = 0;
            if (mod != last_mod || memcmp(held_report, last_held, 6) != 0)
            {
                last_mod = mod;
                memcpy(last_held, held_report, 6);
                SendKeyboardReport(mod, held_report);
            }
        }

        if (USBByteCount > 0)
        {
            // Every transfer on this HID collection is prefixed with its report ID (3) per the
            // HID spec - Ep2Buffer[0] is that ID (ignored), the actual command starts at [1].
            // Replies are likewise prefixed with the report ID at Ep2Buffer[MAX_PACKET_SIZE+0].
            uint8_t cmd = Ep2Buffer[1];
            if (cmd == 0xB0 && USBByteCount >= 4 && !UpPoint2_Busy)
            {
                uint8_t addr = Ep2Buffer[2], len = Ep2Buffer[3];
                if ((uint16_t)addr + len <= EE_SIZE && len <= 32)
                {
                    Ep2Buffer[MAX_PACKET_SIZE] = 0x03;
                    memcpy(Ep2Buffer + MAX_PACKET_SIZE + 1, EE + addr, len);
                    UEP2_T_LEN = len + 1;
                    UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
                    UpPoint2_Busy = 1;
                }
            }
            else if (cmd == 0xB1 && USBByteCount >= 4)
            {
                uint8_t addr = Ep2Buffer[2], len = Ep2Buffer[3];
                uint8_t ok = ((uint16_t)addr + len <= EE_SIZE) && (len <= 32) && (USBByteCount >= (uint8_t)(4 + len));
                if (ok)
                {
                    memcpy(EE + addr, Ep2Buffer + 4, len);
                    WriteDataFlash(addr, EE + addr, len);
                }
                if (!UpPoint2_Busy)
                {
                    Ep2Buffer[MAX_PACKET_SIZE] = 0x03;
                    Ep2Buffer[MAX_PACKET_SIZE + 1] = ok ? 0x01 : 0x00;
                    UEP2_T_LEN = 2;
                    UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
                    UpPoint2_Busy = 1;
                }
            }
            else if (cmd == 0xB2 && !UpPoint2_Busy)
            {
                FactoryReset();
                Ep2Buffer[MAX_PACKET_SIZE] = 0x03;
                Ep2Buffer[MAX_PACKET_SIZE + 1] = 0x01;
                UEP2_T_LEN = 2;
                UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
                UpPoint2_Busy = 1;
            }
            else if (cmd == 0xB3 && !UpPoint2_Busy)
            {
                uint8_t *r = Ep2Buffer + MAX_PACKET_SIZE;
                r[0] = 0x03;
                r[1] = NUM_KEYS; r[2] = NUM_LAYERS; r[3] = 6; r[4] = EE_SIZE;
                r[5] = EE_LAYOUT_VERSION; r[6] = FW_VERSION_MAJOR; r[7] = FW_VERSION_MINOR;
                UEP2_T_LEN = 8;
                UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
                UpPoint2_Busy = 1;
            }
            USBByteCount = 0;
            UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_ACK;
        }

        mDelaymS(1);
    }
}
