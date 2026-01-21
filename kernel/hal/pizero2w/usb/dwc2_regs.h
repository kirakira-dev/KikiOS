/*
 * DWC2 USB Controller Register Definitions
 * Synopsys DesignWare USB 2.0 OTG Controller
 *
 * Used on Raspberry Pi Zero 2W (BCM2710)
 */

#ifndef DWC2_REGS_H
#define DWC2_REGS_H

#include <stdint.h>

// Peripheral base for Pi Zero 2W (BCM2710)
#define PERI_BASE       0x3F000000

// DWC2 USB Controller base
#define USB_BASE        (PERI_BASE + 0x980000)

// ============================================================================
// Global Registers (0x000 - 0x3FF)
// ============================================================================

// OTG Control and Status
#define GOTGCTL         (*(volatile uint32_t *)(USB_BASE + 0x000))
#define GOTGINT         (*(volatile uint32_t *)(USB_BASE + 0x004))

// AHB Configuration
#define GAHBCFG         (*(volatile uint32_t *)(USB_BASE + 0x008))
#define GAHBCFG_GLBL_INTR_EN    (1 << 0)    // Global interrupt enable
#define GAHBCFG_DMA_EN          (1 << 5)    // DMA enable
#define GAHBCFG_AHB_SINGLE      (1 << 23)   // AHB single transfer

// USB Configuration
#define GUSBCFG         (*(volatile uint32_t *)(USB_BASE + 0x00C))
#define GUSBCFG_PHYIF           (1 << 3)    // PHY Interface (0=8bit, 1=16bit)
#define GUSBCFG_ULPI_UTMI_SEL   (1 << 4)    // 0=UTMI+, 1=ULPI
#define GUSBCFG_PHYSEL          (1 << 6)    // 0=HS, 1=FS
#define GUSBCFG_FORCEHOSTMODE   (1 << 29)   // Force host mode
#define GUSBCFG_FORCEDEVMODE    (1 << 30)   // Force device mode

// Reset Control
#define GRSTCTL         (*(volatile uint32_t *)(USB_BASE + 0x010))
#define GRSTCTL_CSFTRST         (1 << 0)    // Core soft reset
#define GRSTCTL_RXFFLSH         (1 << 4)    // RxFIFO flush
#define GRSTCTL_TXFFLSH         (1 << 5)    // TxFIFO flush
#define GRSTCTL_TXFNUM_SHIFT    6           // TxFIFO number for flush
#define GRSTCTL_TXFNUM_ALL      (0x10 << 6) // Flush all TxFIFOs
#define GRSTCTL_AHBIDLE         (1 << 31)   // AHB master idle

// Interrupt Status and Mask
#define GINTSTS         (*(volatile uint32_t *)(USB_BASE + 0x014))
#define GINTMSK         (*(volatile uint32_t *)(USB_BASE + 0x018))
#define GINTSTS_CURMODE         (1 << 0)    // Current mode (0=device, 1=host)
#define GINTSTS_MODEMIS         (1 << 1)    // Mode mismatch
#define GINTSTS_SOF             (1 << 3)    // Start of frame
#define GINTSTS_RXFLVL          (1 << 4)    // RxFIFO non-empty
#define GINTSTS_NPTXFE          (1 << 5)    // Non-periodic TxFIFO empty
#define GINTSTS_USBSUSP         (1 << 11)   // USB suspend
#define GINTSTS_PRTINT          (1 << 24)   // Port interrupt
#define GINTSTS_HCHINT          (1 << 25)   // Host channel interrupt
#define GINTSTS_CONIDSTSCHNG    (1 << 28)   // Connector ID status change
#define GINTSTS_DISCONNINT      (1 << 29)   // Disconnect detected

// Receive Status (Read/Pop)
#define GRXSTSR         (*(volatile uint32_t *)(USB_BASE + 0x01C))  // Read
#define GRXSTSP         (*(volatile uint32_t *)(USB_BASE + 0x020))  // Pop

// FIFO Sizes
#define GRXFSIZ         (*(volatile uint32_t *)(USB_BASE + 0x024))  // Receive FIFO size
#define GNPTXFSIZ       (*(volatile uint32_t *)(USB_BASE + 0x028))  // Non-periodic Tx FIFO size
#define GNPTXSTS        (*(volatile uint32_t *)(USB_BASE + 0x02C))  // Non-periodic Tx FIFO status

// Hardware Configuration (Read-Only)
#define GHWCFG1         (*(volatile uint32_t *)(USB_BASE + 0x044))
#define GHWCFG2         (*(volatile uint32_t *)(USB_BASE + 0x048))
#define GHWCFG3         (*(volatile uint32_t *)(USB_BASE + 0x04C))
#define GHWCFG4         (*(volatile uint32_t *)(USB_BASE + 0x050))

// Host Periodic Tx FIFO Size
#define HPTXFSIZ        (*(volatile uint32_t *)(USB_BASE + 0x100))

// ============================================================================
// Host Mode Registers (0x400 - 0x7FF)
// ============================================================================

// Host Configuration
#define HCFG            (*(volatile uint32_t *)(USB_BASE + 0x400))
#define HCFG_FSLSPCLKSEL_30_60  0           // 30/60 MHz PHY clock
#define HCFG_FSLSPCLKSEL_48     1           // 48 MHz PHY clock
#define HCFG_FSLSUPP            (1 << 2)    // FS/LS only support

// Host Frame Interval/Number
#define HFIR            (*(volatile uint32_t *)(USB_BASE + 0x404))
#define HFNUM           (*(volatile uint32_t *)(USB_BASE + 0x408))

// Host All Channels Interrupt
#define HAINT           (*(volatile uint32_t *)(USB_BASE + 0x414))
#define HAINTMSK        (*(volatile uint32_t *)(USB_BASE + 0x418))

// Host Port Control and Status (Root Hub Port)
#define HPRT0           (*(volatile uint32_t *)(USB_BASE + 0x440))
#define HPRT0_PRTCONNSTS        (1 << 0)    // Port connect status
#define HPRT0_PRTCONNDET        (1 << 1)    // Port connect detected (W1C)
#define HPRT0_PRTENA            (1 << 2)    // Port enable
#define HPRT0_PRTENCHNG         (1 << 3)    // Port enable changed (W1C)
#define HPRT0_PRTOVRCURRACT     (1 << 4)    // Port overcurrent active
#define HPRT0_PRTOVRCURRCHNG    (1 << 5)    // Port overcurrent changed (W1C)
#define HPRT0_PRTRES            (1 << 6)    // Port resume
#define HPRT0_PRTSUSP           (1 << 7)    // Port suspend
#define HPRT0_PRTRST            (1 << 8)    // Port reset
#define HPRT0_PRTLNSTS_SHIFT    10          // Port line status
#define HPRT0_PRTLNSTS_MASK     (3 << 10)
#define HPRT0_PRTPWR            (1 << 12)   // Port power
#define HPRT0_PRTTSTCTL_SHIFT   13          // Port test control
#define HPRT0_PRTSPD_SHIFT      17          // Port speed
#define HPRT0_PRTSPD_MASK       (3 << 17)
#define HPRT0_PRTSPD_HIGH       0
#define HPRT0_PRTSPD_FULL       1
#define HPRT0_PRTSPD_LOW        2

// ============================================================================
// Host Channel Registers (0x500 + n*0x20, n=0-15)
// ============================================================================

#define HCCHAR(n)       (*(volatile uint32_t *)(USB_BASE + 0x500 + (n)*0x20))
#define HCSPLT(n)       (*(volatile uint32_t *)(USB_BASE + 0x504 + (n)*0x20))
#define HCINT(n)        (*(volatile uint32_t *)(USB_BASE + 0x508 + (n)*0x20))
#define HCINTMSK(n)     (*(volatile uint32_t *)(USB_BASE + 0x50C + (n)*0x20))
#define HCTSIZ(n)       (*(volatile uint32_t *)(USB_BASE + 0x510 + (n)*0x20))
#define HCDMA(n)        (*(volatile uint32_t *)(USB_BASE + 0x514 + (n)*0x20))

// HCCHAR bits
#define HCCHAR_MPS_MASK         0x7FF       // Max packet size
#define HCCHAR_EPNUM_SHIFT      11          // Endpoint number
#define HCCHAR_EPDIR            (1 << 15)   // Endpoint direction (1=IN)
#define HCCHAR_LSDEV            (1 << 17)   // Low-speed device
#define HCCHAR_EPTYPE_SHIFT     18          // Endpoint type
#define HCCHAR_EPTYPE_CTRL      0
#define HCCHAR_EPTYPE_ISOC      1
#define HCCHAR_EPTYPE_BULK      2
#define HCCHAR_EPTYPE_INTR      3
#define HCCHAR_MC_SHIFT         20          // Multi-count
#define HCCHAR_DEVADDR_SHIFT    22          // Device address
#define HCCHAR_ODDFRM           (1 << 29)   // Odd frame
#define HCCHAR_CHDIS            (1 << 30)   // Channel disable
#define HCCHAR_CHENA            (1 << 31)   // Channel enable

// HCSPLT bits (split transaction support for FS/LS devices behind HS hubs)
#define HCSPLT_PRTADDR_MASK     0x7F        // bits [6:0] - hub port address
#define HCSPLT_HUBADDR_SHIFT    7           // bits [13:7] - hub device address
#define HCSPLT_HUBADDR_MASK     (0x7F << 7)
#define HCSPLT_XACTPOS_SHIFT    14          // bits [15:14] - transaction position
#define HCSPLT_XACTPOS_ALL      0           // All of payload (< max packet)
#define HCSPLT_XACTPOS_BEGIN    1           // Beginning of payload
#define HCSPLT_XACTPOS_MID      2           // Middle of payload
#define HCSPLT_XACTPOS_END      3           // End of payload
#define HCSPLT_COMPSPLT         (1U << 16)  // Complete split (0=start, 1=complete)
#define HCSPLT_SPLITENA         0x80000000U // Split enable (bit 31)

// HCINT bits (channel interrupts)
#define HCINT_XFERCOMPL         (1 << 0)    // Transfer complete
#define HCINT_CHHLTD            (1 << 1)    // Channel halted
#define HCINT_AHBERR            (1 << 2)    // AHB error
#define HCINT_STALL             (1 << 3)    // STALL response
#define HCINT_NAK               (1 << 4)    // NAK response
#define HCINT_ACK               (1 << 5)    // ACK response
#define HCINT_NYET              (1 << 6)    // NYET response (split transactions)
#define HCINT_XACTERR           (1 << 7)    // Transaction error
#define HCINT_BBLERR            (1 << 8)    // Babble error
#define HCINT_FRMOVRUN          (1 << 9)    // Frame overrun
#define HCINT_DATATGLERR        (1 << 10)   // Data toggle error

// HCTSIZ bits
#define HCTSIZ_XFERSIZE_MASK    0x7FFFF     // Transfer size
#define HCTSIZ_PKTCNT_SHIFT     19          // Packet count
#define HCTSIZ_PKTCNT_MASK      (0x3FF << 19)
#define HCTSIZ_PID_SHIFT        29          // PID
#define HCTSIZ_PID_DATA0        0
#define HCTSIZ_PID_DATA1        2
#define HCTSIZ_PID_DATA2        1
#define HCTSIZ_PID_SETUP        3

// ============================================================================
// Power and Clock Gating
// ============================================================================

#define PCGCCTL         (*(volatile uint32_t *)(USB_BASE + 0xE00))

// ============================================================================
// Data FIFOs (0x1000 + n*0x1000)
// ============================================================================

#define FIFO(n)         (*(volatile uint32_t *)(USB_BASE + 0x1000 + (n)*0x1000))

// ============================================================================
// Mailbox for USB power control
// ============================================================================

#define MAILBOX_BASE    (PERI_BASE + 0x00B880)
#define MAILBOX_READ    (*(volatile uint32_t *)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS  (*(volatile uint32_t *)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE   (*(volatile uint32_t *)(MAILBOX_BASE + 0x20))

#define MAILBOX_FULL    0x80000000
#define MAILBOX_EMPTY   0x40000000
#define MAILBOX_CH_PROP 8

// USB device ID for mailbox power control
#define DEVICE_ID_USB_HCD   3

#endif // DWC2_REGS_H
