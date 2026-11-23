#ifndef USB2BOX_USB_LINK_H
#define USB2BOX_USB_LINK_H

#include "device.h"

bool usb_identity_configured(void);
bool usb_link_start(device *dev);
void usb_link_shutdown(void);
void usb_link_poll(uint32_t now);
#endif
