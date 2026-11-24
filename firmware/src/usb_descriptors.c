#include "tusb.h"
#include "usb_link.h"
#include "stm32f4xx.h"

_Static_assert(USB2BOX_VID >= 0 && USB2BOX_VID <= 0xffff &&
               USB2BOX_PID >= 0 && USB2BOX_PID <= 0xffff,
               "VID and PID must fit in 16 bits");

enum { USB_CONFIGURATION_LENGTH = 9 + 9 + 7 + 7 + 9 + 7, MS_VENDOR_CODE = 0x20 };

static const tusb_desc_device_t usb2_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, .bMaxPacketSize0 = 64, .bcdDevice = 0x0100,
    .idVendor = USB2BOX_VID, .idProduct = USB2BOX_PID,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
static const uint8_t usb2_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, USB_CONFIGURATION_LENGTH, 0, 100),
    TUD_VENDOR_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
    9, TUSB_DESC_INTERFACE, 1, 0, 1, TUSB_CLASS_VENDOR_SPECIFIC, 0, 0, 0,
    7, TUSB_DESC_ENDPOINT, 0x82, TUSB_XFER_BULK, 64, 0, 0,
};
_Static_assert(sizeof usb2_configuration == USB_CONFIGURATION_LENGTH, "USB configuration length");

typedef struct __attribute__((packed)) {
    uint16_t length, type;
    uint8_t interface, reserved;
    uint16_t total;
    uint16_t compatible_length, compatible_type;
    char compatible[8], subcompatible[8];
    uint16_t property_length, property_type, datatype, name_length;
    uint16_t name[21];
    uint16_t data_length;
    uint16_t data[40];
} ms_function;

#define MS_FUNCTION(number, guid) { \
    .length = 8, .type = 2, .interface = (number), .total = sizeof(ms_function), \
    .compatible_length = 20, .compatible_type = 3, .compatible = "WINUSB", \
    .property_length = 132, .property_type = 4, .datatype = 7, .name_length = 42, \
    .name = u"DeviceInterfaceGUIDs", .data_length = 80, .data = guid, \
}

typedef struct __attribute__((packed)) {
    uint16_t length, type;
    uint32_t windows;
    uint16_t total;
    uint16_t config_length, config_type;
    uint8_t config, reserved;
    uint16_t config_total;
    ms_function function[2];
} ms_descriptor;

static const ms_descriptor usb2_winusb = {
    .length = 10, .type = 0, .windows = 0x06030000, .total = sizeof(ms_descriptor),
    .config_length = 8, .config_type = 1, .config = 0, .config_total = 8 + 2 * sizeof(ms_function),
    .function = {
        MS_FUNCTION(0, u"{B94E5E67-8362-43F1-8C19-C9BF0C5426B8}"),
        MS_FUNCTION(1, u"{BB82C96E-6386-4C71-BEDA-B038E173FB07}"),
    },
};
static const uint8_t usb2_bos[] = {
    TUD_BOS_DESCRIPTOR(5 + TUD_BOS_MICROSOFT_OS_DESC_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(sizeof(usb2_winusb), MS_VENDOR_CODE),
};

bool usb_identity_configured(void)
{
    return usb2_device_descriptor.idVendor != 0 && usb2_device_descriptor.idProduct != 0;
}

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&usb2_device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    return index == 0 ? usb2_configuration : NULL;
}

const uint8_t *tud_descriptor_bos_cb(void)
{
    return usb2_bos;
}

bool tud_vendor_control_xfer_cb(uint8_t port, uint8_t stage,
                                const tusb_control_request_t *request)
{
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType != 0xc0 || request->bRequest != MS_VENDOR_CODE ||
        request->wIndex != 7 || request->wValue != 0) return false;
    return tud_control_xfer(port, request, (void *)&usb2_winusb, sizeof usb2_winusb);
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t lang)
{
    static uint16_t value[25];
    static const char *const names[] = {NULL, "USB2Box", "USB2XXX"};
    static const char hex[] = "0123456789ABCDEF";
    unsigned n = 0;
    if (index == 0) {
        value[++n] = 0x0409;
    } else if (lang == 0x0409 && index <= 2) {
        for (const char *p = names[index]; *p; ++p) value[++n] = (uint16_t)*p;
    } else if (lang == 0x0409 && index == 3) {
        const uint32_t *uid = (const uint32_t *)UID_BASE;
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned shift = 32; shift; shift -= 4)
                value[++n] = (uint16_t)hex[(uid[i] >> (shift - 4)) & 15u];
        }
    } else {
        return NULL;
    }
    value[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * (n + 1u)));
    return value;
}
