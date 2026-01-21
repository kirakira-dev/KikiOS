/*
 * USB Enumeration
 * Device discovery, descriptor parsing, hub support
 */

#ifndef USB_ENUM_H
#define USB_ENUM_H

#include <stdint.h>
#include "usb_types.h"

// Standard device requests
int usb_get_device_descriptor(int addr, usb_device_descriptor_t *desc);
int usb_set_address(int addr);
int usb_set_configuration(int addr, int config);
int usb_get_configuration_descriptor(int addr, uint8_t *buf, int len);

// Hub-specific requests
int usb_get_hub_descriptor(int addr, usb_hub_descriptor_t *desc);
int usb_get_port_status(int hub_addr, int port, uint32_t *status);
int usb_set_port_feature(int hub_addr, int port, int feature);
int usb_clear_port_feature(int hub_addr, int port, int feature);

// Enumeration
int usb_enumerate_hub(int hub_addr, int num_ports);
int usb_enumerate_device_at(int parent_addr, int port, int speed);
int usb_enumerate_device(void);

#endif // USB_ENUM_H
