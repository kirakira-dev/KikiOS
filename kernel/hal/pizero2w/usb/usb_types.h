/*
 * USB Types and Descriptors
 * Standard USB structures and constants
 */

#ifndef USB_TYPES_H
#define USB_TYPES_H

#include <stdint.h>

// ============================================================================
// USB Descriptors and Structures
// ============================================================================

// Standard USB request
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

// Device descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

// Configuration descriptor header
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

// Interface descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

// Endpoint descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

// HID descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__((packed)) usb_hid_descriptor_t;

// Hub descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;        // Time in 2ms intervals
    uint8_t  bHubContrCurrent;
    uint8_t  DeviceRemovable[8];    // Variable length, max 8 bytes for 64 ports
} __attribute__((packed)) usb_hub_descriptor_t;

// ============================================================================
// USB Standard Requests
// ============================================================================

#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_DESCRIPTOR      7
#define USB_REQ_GET_CONFIGURATION   8
#define USB_REQ_SET_CONFIGURATION   9

// ============================================================================
// USB Descriptor Types
// ============================================================================

#define USB_DESC_DEVICE             1
#define USB_DESC_CONFIGURATION      2
#define USB_DESC_STRING             3
#define USB_DESC_INTERFACE          4
#define USB_DESC_ENDPOINT           5
#define USB_DESC_HID                0x21
#define USB_DESC_HID_REPORT         0x22
#define USB_DESC_HUB                0x29

// ============================================================================
// USB Class Codes
// ============================================================================

#define USB_CLASS_HID               3
#define USB_CLASS_HUB               9

// ============================================================================
// HID Subclass/Protocol
// ============================================================================

#define USB_HID_SUBCLASS_BOOT       1
#define USB_HID_PROTOCOL_KEYBOARD   1
#define USB_HID_PROTOCOL_MOUSE      2

// HID class requests
#define USB_HID_SET_PROTOCOL        0x0B
#define USB_HID_SET_IDLE            0x0A
#define USB_HID_PROTOCOL_BOOT       0
#define USB_HID_PROTOCOL_REPORT     1

// ============================================================================
// Hub Class Requests and Features
// ============================================================================

#define USB_REQ_GET_HUB_STATUS      0
#define USB_REQ_GET_PORT_STATUS     0
#define USB_REQ_SET_PORT_FEATURE    3
#define USB_REQ_CLEAR_PORT_FEATURE  1

// Hub port features
#define USB_PORT_FEAT_CONNECTION    0
#define USB_PORT_FEAT_ENABLE        1
#define USB_PORT_FEAT_SUSPEND       2
#define USB_PORT_FEAT_OVER_CURRENT  3
#define USB_PORT_FEAT_RESET         4
#define USB_PORT_FEAT_POWER         8
#define USB_PORT_FEAT_LOWSPEED      9
#define USB_PORT_FEAT_C_CONNECTION  16
#define USB_PORT_FEAT_C_ENABLE      17
#define USB_PORT_FEAT_C_SUSPEND     18
#define USB_PORT_FEAT_C_OVER_CURRENT 19
#define USB_PORT_FEAT_C_RESET       20

// Hub port status bits
#define USB_PORT_STAT_CONNECTION    (1 << 0)
#define USB_PORT_STAT_ENABLE        (1 << 1)
#define USB_PORT_STAT_SUSPEND       (1 << 2)
#define USB_PORT_STAT_OVERCURRENT   (1 << 3)
#define USB_PORT_STAT_RESET         (1 << 4)
#define USB_PORT_STAT_POWER         (1 << 8)
#define USB_PORT_STAT_LOW_SPEED     (1 << 9)
#define USB_PORT_STAT_HIGH_SPEED    (1 << 10)

// ============================================================================
// USB Device State
// ============================================================================

// USB device info
typedef struct {
    int address;
    int speed;                  // 0=HS, 1=FS, 2=LS
    int max_packet_size;
    int is_hub;
    int hub_ports;              // Number of ports if hub
    int parent_hub;             // Address of parent hub (0 = root)
    int parent_port;            // Port on parent hub
} usb_device_t;

#define MAX_USB_DEVICES 8

// USB controller state
typedef struct {
    int initialized;
    int num_channels;
    int device_connected;
    int device_speed;           // 0=HS, 1=FS, 2=LS
    int next_address;           // Next address to assign
    uint8_t data_toggle[16];    // Data toggle for each endpoint

    // Device tracking
    usb_device_t devices[MAX_USB_DEVICES];
    int num_devices;

    // Keyboard info (if found)
    int keyboard_addr;
    int keyboard_ep;            // Interrupt endpoint
    int keyboard_mps;           // Max packet size for interrupt EP
    int keyboard_interval;      // Polling interval

    // Mouse info (if found)
    int mouse_addr;
    int mouse_ep;               // Interrupt endpoint
    int mouse_mps;              // Max packet size for interrupt EP
    int mouse_interval;         // Polling interval

    // Address-0 routing during enumeration behind a hub
    // (needed for split transactions when enumerating FS/LS devices behind HS hubs)
    int enum_parent_hub;        // Hub address (0 = root port)
    int enum_parent_port;       // Port on hub (0 = root)
    int enum_speed;             // Speed of device being enumerated (0=HS, 1=FS, 2=LS)
} usb_state_t;

// Global USB state (defined in dwc2_core.c)
extern usb_state_t usb_state;

#endif // USB_TYPES_H
