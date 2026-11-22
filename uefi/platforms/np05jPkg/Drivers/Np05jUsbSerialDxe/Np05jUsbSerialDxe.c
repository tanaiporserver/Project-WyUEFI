/** @file
  NP05J USB Composite (CDC-ACM Virtual Serial + Project SunUEFI UEFI Fastboot)
  and selectable Pure Fastboot DXE Driver.

  Exposes over Qualcomm UsbfnDwc3Dxe (EFI_USBFN_IO_PROTOCOL):
    1. USB CDC-ACM serial port (/dev/ttyACM0 / COMx) for boot log streaming
       and UEFI SerialIo / TerminalDxe console TTY (Composite mode)
    2. Resident Project SunUEFI Fastboot 0.4 background service (SunUEFI-np05j)
       active across Splash, Boot Menu, BIOS Setup, UiApp, and UEFI Shell.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Np05jUsbSerialDxe.h"

#define NP05J_BUS_SPEED_SUPER_PLUS  5U

STATIC_ASSERT (sizeof (NP05J_COMP_HS_CONFIG_BUNDLE) == 98, "Composite HS config bundle size mismatch");
STATIC_ASSERT (sizeof (NP05J_COMP_SS_CONFIG_BUNDLE) == 128, "Composite SS config bundle size mismatch");
STATIC_ASSERT (sizeof (NP05J_FB_HS_CONFIG_BUNDLE) == 32, "Fastboot HS config bundle size mismatch");
STATIC_ASSERT (sizeof (NP05J_FB_SS_CONFIG_BUNDLE) == 44, "Fastboot SS config bundle size mismatch");
STATIC_ASSERT (sizeof (NP05J_USB_BOS_BUNDLE) == 22, "BOS bundle size mismatch");
STATIC_ASSERT (sizeof (NP05J_MS_OS_COMPAT_ID_DESC) == 40, "MS OS Compat ID size mismatch");
STATIC_ASSERT (sizeof (NP05J_MS_OS_EXT_PROP_DESC) == 146, "MS OS Ext Prop size mismatch");

STATIC NP05J_USB_SERIAL_DEV  mDev;

//
// Persistent SerialIo Device Path (parent for TerminalDxe)
//
STATIC NP05J_SERIAL_DEVICE_PATH  mSerialDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    NP05J_USB_SERIAL_DEVICE_GUID
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_UART_DP,
      {
        (UINT8)(sizeof (UART_DEVICE_PATH)),
        (UINT8)(sizeof (UART_DEVICE_PATH) >> 8)
      }
    },
    0,
    115200,
    8,
    NoParity,
    OneStopBit
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)(END_DEVICE_PATH_LENGTH >> 8)
    }
  }
};

//
// Full Console Device Path registered into ConIn / ConOut / ErrOut (VT-UTF8)
//
STATIC NP05J_TERMINAL_DEVICE_PATH  mTerminalDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    NP05J_USB_SERIAL_DEVICE_GUID
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_UART_DP,
      {
        (UINT8)(sizeof (UART_DEVICE_PATH)),
        (UINT8)(sizeof (UART_DEVICE_PATH) >> 8)
      }
    },
    0,
    115200,
    8,
    NoParity,
    OneStopBit
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    EFI_VT_UTF8_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)(END_DEVICE_PATH_LENGTH >> 8)
    }
  }
};

//
// Composite USB Device Descriptors (IAD CDC-ACM UART + SunUEFI Fastboot)
//
STATIC EFI_USB_DEVICE_DESCRIPTOR  mCompHsDeviceDesc = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0210,                 // USB 2.1 (BOS capable)
  0xEF,                   // Miscellaneous Device Class
  0x02,                   // Common Class
  0x01,                   // Interface Association Descriptor
  64,
  NP05J_USB_VID,
  NP05J_USB_PID,
  NP05J_USB_BCD_DEVICE,
  1,                      // iManufacturer
  2,                      // iProduct
  3,                      // iSerialNumber
  1                       // bNumConfigurations
};

STATIC EFI_USB_DEVICE_DESCRIPTOR  mCompSsDeviceDesc = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0300,
  0xEF,
  0x02,
  0x01,
  9,                      // 2^9 = 512 bytes for SuperSpeed EP0
  NP05J_USB_VID,
  NP05J_USB_PID,
  NP05J_USB_BCD_DEVICE,
  1,
  2,
  3,
  1
};

STATIC CONST NP05J_USB_DEVICE_QUALIFIER_DESC  mCompDeviceQualifierDesc = {
  sizeof (NP05J_USB_DEVICE_QUALIFIER_DESC),
  NP05J_USB_DESC_DEV_QUALIFIER,
  0x0200,
  0xEF,
  0x02,
  0x01,
  64,
  1,
  0
};

//
// Pure Fastboot USB Device Descriptors (single interface FF/42/03)
//
STATIC EFI_USB_DEVICE_DESCRIPTOR  mFbHsDeviceDesc = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0210,
  0x00,
  0x00,
  0x00,
  64,
  NP05J_USB_VID,
  NP05J_USB_PID,
  NP05J_USB_BCD_DEVICE,
  1,
  2,
  3,
  1
};

STATIC EFI_USB_DEVICE_DESCRIPTOR  mFbSsDeviceDesc = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR),
  USB_DESC_TYPE_DEVICE,
  0x0300,
  0x00,
  0x00,
  0x00,
  9,
  NP05J_USB_VID,
  NP05J_USB_PID,
  NP05J_USB_BCD_DEVICE,
  1,
  2,
  3,
  1
};

STATIC CONST NP05J_USB_DEVICE_QUALIFIER_DESC  mFbDeviceQualifierDesc = {
  sizeof (NP05J_USB_DEVICE_QUALIFIER_DESC),
  NP05J_USB_DESC_DEV_QUALIFIER,
  0x0200,
  0x00,
  0x00,
  0x00,
  64,
  1,
  0
};

STATIC NP05J_USB_BOS_BUNDLE  mBosBundle = {
  {
    sizeof (USB_BOS_DESCRIPTOR),
    USB_DESC_TYPE_BOS,
    sizeof (NP05J_USB_BOS_BUNDLE),
    2
  },
  {
    sizeof (NP05J_USB_20_EXT_CAP_DESC),
    USB_DESC_TYPE_DEV_CAP,
    NP05J_USB_DEV_CAP_USB20_EXT,
    0x00000002            // LPM capable
  },
  {
    sizeof (NP05J_USB_SS_CAP_DESC),
    USB_DESC_TYPE_DEV_CAP,
    NP05J_USB_DEV_CAP_SS_USB,
    0x00,
    0x000E,               // FS | HS | SS supported
    0x01,                 // Lowest full-function speed: Full Speed
    0x0A,                 // U1 exit latency: 10 us
    0x07FF                // U2 exit latency: 2047 us
  }
};

//
// Composite Configuration Bundles (HS, FS, SS)
//
STATIC NP05J_COMP_HS_CONFIG_BUNDLE  mCompHsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_COMP_HS_CONFIG_BUNDLE),
    3,                    // bNumInterfaces (CDC Comm, CDC Data, Fastboot)
    1,                    // bConfigurationValue
    0,
    0xC0,                 // Self-powered
    50                    // 100 mA
  },
  {
    sizeof (NP05J_USB_IAD_DESC),
    NP05J_USB_DESC_IAD,
    0,                    // FirstInterface = 0
    2,                    // InterfaceCount = 2 (Comm + Data)
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,                    // Interface 0: CDC Control
    0,
    1,
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (USB_CDC_HEADER_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_HEADER,
    0x0110
  },
  {
    sizeof (USB_CDC_CALL_MGMT_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_CALL_MGMT,
    0x00,
    1
  },
  {
    sizeof (USB_CDC_ACM_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_ACM,
    0x06
  },
  {
    sizeof (USB_CDC_UNION_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_UNION,
    0,
    1
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_INT_IN_ADDR,
    USB_ENDPOINT_INTERRUPT,
    64,
    9                     // 32 ms at HS
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    1,                    // Interface 1: CDC Data
    0,
    2,
    USB_CLASS_CDC_DATA,
    0x00,
    0x00,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_OUT_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_IN_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    2,                    // Interface 2: SunUEFI Fastboot
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4                     // iInterface = "SunUEFI Fastboot"
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_OUT_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_IN_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  }
};

STATIC NP05J_COMP_HS_CONFIG_BUNDLE  mCompFsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_COMP_HS_CONFIG_BUNDLE),
    3,
    1,
    0,
    0xC0,
    50
  },
  {
    sizeof (NP05J_USB_IAD_DESC),
    NP05J_USB_DESC_IAD,
    0,
    2,
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    1,
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (USB_CDC_HEADER_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_HEADER,
    0x0110
  },
  {
    sizeof (USB_CDC_CALL_MGMT_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_CALL_MGMT,
    0x00,
    1
  },
  {
    sizeof (USB_CDC_ACM_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_ACM,
    0x06
  },
  {
    sizeof (USB_CDC_UNION_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_UNION,
    0,
    1
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_INT_IN_ADDR,
    USB_ENDPOINT_INTERRUPT,
    64,
    32
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    1,
    0,
    2,
    USB_CLASS_CDC_DATA,
    0x00,
    0x00,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_OUT_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_IN_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    2,
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_OUT_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_IN_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  }
};

STATIC NP05J_COMP_SS_CONFIG_BUNDLE  mCompSsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_COMP_SS_CONFIG_BUNDLE),
    3,
    1,
    0,
    0xC0,
    12                    // 96 mA (in 8 mA units for SS)
  },
  {
    sizeof (NP05J_USB_IAD_DESC),
    NP05J_USB_DESC_IAD,
    0,
    2,
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    1,
    USB_CLASS_CDC_COMM,
    USB_SUBCLASS_CDC_ACM,
    USB_PROTO_CDC_AT,
    0
  },
  {
    sizeof (USB_CDC_HEADER_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_HEADER,
    0x0110
  },
  {
    sizeof (USB_CDC_CALL_MGMT_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_CALL_MGMT,
    0x00,
    1
  },
  {
    sizeof (USB_CDC_ACM_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_ACM,
    0x06
  },
  {
    sizeof (USB_CDC_UNION_DESC),
    USB_DESC_TYPE_CS_INTERFACE,
    USB_CDC_SUBTYPE_UNION,
    0,
    1
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_INT_IN_ADDR,
    USB_ENDPOINT_INTERRUPT,
    64,
    9
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    64
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    1,
    0,
    2,
    USB_CLASS_CDC_DATA,
    0x00,
    0x00,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_OUT_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_BULK_IN_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    2,
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_OUT_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_COMP_IN_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  }
};

//
// Pure Fastboot Configuration Bundles (HS, FS, SS)
//
STATIC NP05J_FB_HS_CONFIG_BUNDLE  mFbHsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_FB_HS_CONFIG_BUNDLE),
    1,
    1,
    0,
    0xC0,
    50
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_OUT_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_IN_ADDR,
    USB_ENDPOINT_BULK,
    512,
    0
  }
};

STATIC NP05J_FB_HS_CONFIG_BUNDLE  mFbFsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_FB_HS_CONFIG_BUNDLE),
    1,
    1,
    0,
    0xC0,
    50
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_OUT_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_IN_ADDR,
    USB_ENDPOINT_BULK,
    64,
    0
  }
};

STATIC NP05J_FB_SS_CONFIG_BUNDLE  mFbSsConfigBundle = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),
    USB_DESC_TYPE_CONFIG,
    sizeof (NP05J_FB_SS_CONFIG_BUNDLE),
    1,
    1,
    0,
    0xC0,
    12
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR),
    USB_DESC_TYPE_INTERFACE,
    0,
    0,
    2,
    NP05J_FB_IFC_CLASS,
    NP05J_FB_IFC_SUBCLASS,
    NP05J_FB_IFC_PROTOCOL,
    4
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_OUT_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),
    USB_DESC_TYPE_ENDPOINT,
    NP05J_EP_FB_PURE_IN_ADDR,
    USB_ENDPOINT_BULK,
    1024,
    0
  },
  {
    sizeof (NP05J_USB_SS_EP_COMP_DESC),
    NP05J_USB_DESC_SS_EP_COMP,
    0,
    0,
    0
  }
};

//
// Pointer trees passed to UsbfnIo->ConfigureEnableEndpointsEx() — Composite Mode
//
STATIC EFI_USB_ENDPOINT_DESCRIPTOR  *mCompHsCommEpTable[] = {
  &mCompHsConfigBundle.IntInEp
};

STATIC EFI_USB_ENDPOINT_DESCRIPTOR  *mCompHsDataEpTable[] = {
  &mCompHsConfigBundle.BulkOutEp,
  &mCompHsConfigBundle.BulkInEp
};

STATIC EFI_USB_ENDPOINT_DESCRIPTOR  *mCompHsFbEpTable[] = {
  &mCompHsConfigBundle.FbOutEp,
  &mCompHsConfigBundle.FbInEp
};

STATIC EFI_USB_INTERFACE_INFO  mCompHsCommIfcInfo = {
  &mCompHsConfigBundle.CommIfc,
  mCompHsCommEpTable
};

STATIC EFI_USB_INTERFACE_INFO  mCompHsDataIfcInfo = {
  &mCompHsConfigBundle.DataIfc,
  mCompHsDataEpTable
};

STATIC EFI_USB_INTERFACE_INFO  mCompHsFbIfcInfo = {
  &mCompHsConfigBundle.FbIfc,
  mCompHsFbEpTable
};

STATIC EFI_USB_INTERFACE_INFO  *mCompHsIfcInfoTable[] = {
  &mCompHsCommIfcInfo,
  &mCompHsDataIfcInfo,
  &mCompHsFbIfcInfo
};

STATIC EFI_USB_CONFIG_INFO  mCompHsConfigInfo = {
  &mCompHsConfigBundle.Config,
  mCompHsIfcInfoTable
};

STATIC EFI_USB_CONFIG_INFO  *mCompHsConfigInfoTable[] = {
  &mCompHsConfigInfo
};

STATIC EFI_USB_DEVICE_INFO  mCompHsDeviceInfo = {
  &mCompHsDeviceDesc,
  mCompHsConfigInfoTable
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mCompSsIntInEpDesc = {
  &mCompSsConfigBundle.IntInEp,
  &mCompSsConfigBundle.IntInComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mCompSsBulkOutEpDesc = {
  &mCompSsConfigBundle.BulkOutEp,
  &mCompSsConfigBundle.BulkOutComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mCompSsBulkInEpDesc = {
  &mCompSsConfigBundle.BulkInEp,
  &mCompSsConfigBundle.BulkInComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mCompSsFbOutEpDesc = {
  &mCompSsConfigBundle.FbOutEp,
  &mCompSsConfigBundle.FbOutComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mCompSsFbInEpDesc = {
  &mCompSsConfigBundle.FbInEp,
  &mCompSsConfigBundle.FbInComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  *mCompSsCommEpTable[] = {
  &mCompSsIntInEpDesc
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  *mCompSsDataEpTable[] = {
  &mCompSsBulkOutEpDesc,
  &mCompSsBulkInEpDesc
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  *mCompSsFbEpTable[] = {
  &mCompSsFbOutEpDesc,
  &mCompSsFbInEpDesc
};

STATIC NP05J_USB_SS_INTERFACE_INFO  mCompSsCommIfcInfo = {
  &mCompSsConfigBundle.CommIfc,
  mCompSsCommEpTable
};

STATIC NP05J_USB_SS_INTERFACE_INFO  mCompSsDataIfcInfo = {
  &mCompSsConfigBundle.DataIfc,
  mCompSsDataEpTable
};

STATIC NP05J_USB_SS_INTERFACE_INFO  mCompSsFbIfcInfo = {
  &mCompSsConfigBundle.FbIfc,
  mCompSsFbEpTable
};

STATIC NP05J_USB_SS_INTERFACE_INFO  *mCompSsIfcInfoTable[] = {
  &mCompSsCommIfcInfo,
  &mCompSsDataIfcInfo,
  &mCompSsFbIfcInfo
};

STATIC NP05J_USB_SS_CONFIG_INFO  mCompSsConfigInfo = {
  &mCompSsConfigBundle.Config,
  mCompSsIfcInfoTable
};

STATIC NP05J_USB_SS_CONFIG_INFO  *mCompSsConfigInfoTable[] = {
  &mCompSsConfigInfo
};

STATIC NP05J_USB_SS_DEVICE_INFO  mCompSsDeviceInfo = {
  &mCompSsDeviceDesc,
  mCompSsConfigInfoTable,
  &mBosBundle.Bos
};

//
// Pointer trees passed to UsbfnIo->ConfigureEnableEndpointsEx() — Pure Fastboot Mode
//
STATIC EFI_USB_ENDPOINT_DESCRIPTOR  *mFbHsEpTable[] = {
  &mFbHsConfigBundle.FbOutEp,
  &mFbHsConfigBundle.FbInEp
};

STATIC EFI_USB_INTERFACE_INFO  mFbHsIfcInfo = {
  &mFbHsConfigBundle.FbIfc,
  mFbHsEpTable
};

STATIC EFI_USB_INTERFACE_INFO  *mFbHsIfcInfoTable[] = {
  &mFbHsIfcInfo
};

STATIC EFI_USB_CONFIG_INFO  mFbHsConfigInfo = {
  &mFbHsConfigBundle.Config,
  mFbHsIfcInfoTable
};

STATIC EFI_USB_CONFIG_INFO  *mFbHsConfigInfoTable[] = {
  &mFbHsConfigInfo
};

STATIC EFI_USB_DEVICE_INFO  mFbHsDeviceInfo = {
  &mFbHsDeviceDesc,
  mFbHsConfigInfoTable
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mFbSsOutEpDesc = {
  &mFbSsConfigBundle.FbOutEp,
  &mFbSsConfigBundle.FbOutComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  mFbSsInEpDesc = {
  &mFbSsConfigBundle.FbInEp,
  &mFbSsConfigBundle.FbInComp
};

STATIC NP05J_USB_SS_ENDPOINT_DESC  *mFbSsEpTable[] = {
  &mFbSsOutEpDesc,
  &mFbSsInEpDesc
};

STATIC NP05J_USB_SS_INTERFACE_INFO  mFbSsIfcInfo = {
  &mFbSsConfigBundle.FbIfc,
  mFbSsEpTable
};

STATIC NP05J_USB_SS_INTERFACE_INFO  *mFbSsIfcInfoTable[] = {
  &mFbSsIfcInfo
};

STATIC NP05J_USB_SS_CONFIG_INFO  mFbSsConfigInfo = {
  &mFbSsConfigBundle.Config,
  mFbSsIfcInfoTable
};

STATIC NP05J_USB_SS_CONFIG_INFO  *mFbSsConfigInfoTable[] = {
  &mFbSsConfigInfo
};

STATIC NP05J_USB_SS_DEVICE_INFO  mFbSsDeviceInfo = {
  &mFbSsDeviceDesc,
  mFbSsConfigInfoTable,
  &mBosBundle.Bos
};

//
// USB String & MS OS 1.0 Descriptors
//
STATIC CONST UINT8  mStrDescLangId[] = {
  4, USB_DESC_TYPE_STRING,
  0x09, 0x04              // English (United States)
};

STATIC CONST UINT8  mStrDescMsft100[] = {
  18, USB_DESC_TYPE_STRING,
  'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
  NP05J_USB_REQ_MS_VENDOR, 0x00
};

STATIC CONST NP05J_MS_OS_COMPAT_ID_DESC  mMsOsCompatIdTemplate = {
  sizeof (NP05J_MS_OS_COMPAT_ID_DESC),
  0x0100,
  0x0004,
  1,
  { 0 },
  0,
  1,
  { 'W', 'I', 'N', 'U', 'S', 'B', 0, 0 },
  { 0 },
  { 0 }
};

STATIC CONST NP05J_MS_OS_EXT_PROP_DESC  mMsOsExtPropDesc = {
  sizeof (NP05J_MS_OS_EXT_PROP_DESC),
  0x0100,
  0x0005,
  1,
  136,
  7,
  42,
  L"DeviceInterfaceGUIDs",
  80,
  L"{F72FE0D4-CBCB-407D-8814-9ED673D0DD6B}\0"
};

//
// Forward declarations
//
STATIC
VOID
Np05jUsbSerialPoll (
  IN NP05J_USB_SERIAL_DEV  *Dev
  );

STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  );

STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  );

STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  );

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDriverBinding = {
  Np05jUsbSerialBindingSupported,
  Np05jUsbSerialBindingStart,
  Np05jUsbSerialBindingStop,
  0x10,
  NULL,
  NULL
};

//
// Helper: BIOS Config lookup (prefer runtime protocol if present)
//
STATIC
VOID
GetLiveBiosConfig (
  OUT NP05J_BIOS_CONFIG  *Config
  )
{
  NP05J_BIOS_CONFIG_PROTOCOL  *CfgProto = NULL;

  Np05jBiosConfigGetDefaults (Config);
  if ((gBS != NULL) &&
      (gBS->LocateProtocol (&gNp05jBiosConfigProtocolGuid, NULL, (VOID **)&CfgProto) == EFI_SUCCESS) &&
      (CfgProto != NULL) &&
      (CfgProto->GetConfig != NULL) &&
      !EFI_ERROR (CfgProto->GetConfig (CfgProto, Config)))
  {
    return;
  }

  Np05jBiosConfigLoad (Config);
}

//
// Helper: locate shared RAM boot log ring buffer in Display_Reserved tail
//
STATIC
VOID
LocateLogRing (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR  FbRegion;
  EFI_PHYSICAL_ADDRESS          RingAddr;

  if (Dev->LogRingHeader != NULL) {
    return;
  }

  ZeroMem (&FbRegion, sizeof (FbRegion));
  LocateMemoryRegionByName ("Display Reserved", &FbRegion);
  LocateMemoryRegionByName ("Display_Reserved", &FbRegion);
  if ((FbRegion.Address == 0) || (FbRegion.Length <= NP05J_LOG_RING_TAIL_OFFSET)) {
    return;
  }

  RingAddr           = FbRegion.Address + FbRegion.Length - NP05J_LOG_RING_TAIL_OFFSET;
  Dev->LogRingHeader = (NP05J_LOG_RING_HEADER *)(UINTN)RingAddr;
  Dev->LogRingData   = (UINT8 *)(UINTN)(RingAddr + NP05J_LOG_RING_HEADER_SIZE);
}

STATIC
BOOLEAN
IsLogRingValid (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  if (Dev->LogRingHeader == NULL) {
    LocateLogRing (Dev);
  }

  if (Dev->LogRingHeader == NULL) {
    return FALSE;
  }

  return ((Dev->LogRingHeader->Magic == NP05J_LOG_RING_MAGIC) &&
          (Dev->LogRingHeader->Version == NP05J_LOG_RING_VERSION) &&
          (Dev->LogRingHeader->Capacity == NP05J_LOG_RING_CAPACITY));
}

STATIC
VOID
RewindLogRingToOldest (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINT64  WritePos;

  if (!IsLogRingValid (Dev)) {
    return;
  }

  WritePos         = Dev->LogRingHeader->WritePos;
  Dev->LogReadPos  = (WritePos > NP05J_LOG_RING_CAPACITY) ? (WritePos - NP05J_LOG_RING_CAPACITY) : 0;
  Dev->LogLastChar = 0;
}

//
// Helper: ring buffer push/pop (callers hold TPL_NOTIFY lock)
//
STATIC
VOID
RxRingPushBytes (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN CONST UINT8           *Data,
  IN UINTN                 Len
  )
{
  EFI_TPL  OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (UINTN Index = 0; Index < Len; Index++) {
    if (Dev->RxCount >= NP05J_RX_RING_SIZE) {
      Dev->RxTail = (Dev->RxTail + 1U) % NP05J_RX_RING_SIZE;
      Dev->RxCount--;
    }

    Dev->RxRing[Dev->RxHead] = Data[Index];
    Dev->RxHead              = (Dev->RxHead + 1U) % NP05J_RX_RING_SIZE;
    Dev->RxCount++;
  }

  gBS->RestoreTPL (OldTpl);
}

STATIC
UINTN
RxRingPopBytes (
  IN  NP05J_USB_SERIAL_DEV  *Dev,
  OUT UINT8                 *OutBuf,
  IN  UINTN                 MaxLen
  )
{
  EFI_TPL  OldTpl;
  UINTN    Copied;

  Copied = 0;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  while ((Copied < MaxLen) && (Dev->RxCount > 0)) {
    OutBuf[Copied++] = Dev->RxRing[Dev->RxTail];
    Dev->RxTail      = (Dev->RxTail + 1U) % NP05J_RX_RING_SIZE;
    Dev->RxCount--;
  }

  gBS->RestoreTPL (OldTpl);
  return Copied;
}

STATIC
VOID
TxRingPushBytes (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN CONST UINT8           *Data,
  IN UINTN                 Len
  )
{
  EFI_TPL  OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (UINTN Index = 0; Index < Len; Index++) {
    if (Dev->TxCount >= NP05J_TX_RING_SIZE) {
      Dev->TxTail = (Dev->TxTail + 1U) % NP05J_TX_RING_SIZE;
      Dev->TxCount--;
      Dev->TxDropped++;
    }

    Dev->TxRing[Dev->TxHead] = Data[Index];
    Dev->TxHead              = (Dev->TxHead + 1U) % NP05J_TX_RING_SIZE;
    Dev->TxCount++;
  }

  gBS->RestoreTPL (OldTpl);
}

STATIC
VOID
TxRingPushAscii (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN CONST CHAR8           *Str
  )
{
  UINTN  Len;

  if (Str == NULL) {
    return;
  }

  Len = AsciiStrLen (Str);
  if (Len > 0) {
    TxRingPushBytes (Dev, (CONST UINT8 *)Str, Len);
  }
}

STATIC
UINTN
TxRingPopBytes (
  IN  NP05J_USB_SERIAL_DEV  *Dev,
  OUT UINT8                 *OutBuf,
  IN  UINTN                 MaxLen
  )
{
  EFI_TPL  OldTpl;
  UINTN    Copied;

  Copied = 0;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  while ((Copied < MaxLen) && (Dev->TxCount > 0)) {
    OutBuf[Copied++] = Dev->TxRing[Dev->TxTail];
    Dev->TxTail      = (Dev->TxTail + 1U) % NP05J_TX_RING_SIZE;
    Dev->TxCount--;
  }

  gBS->RestoreTPL (OldTpl);
  return Copied;
}

STATIC
CONST CHAR8 *
BusSpeedName (
  IN EFI_USB_BUS_SPEED  Speed
  )
{
  switch ((UINT32)Speed) {
    case UsbBusSpeedLow:
      return "LS";
    case UsbBusSpeedFull:
      return "FS";
    case UsbBusSpeedHigh:
      return "HS";
    case UsbBusSpeedSuper:
      return "SS";
    case NP05J_BUS_SPEED_SUPER_PLUS:
      return "SSP";
    default:
      return "UNK";
  }
}

STATIC
VOID
OnHostConnected (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN BOOLEAN               IsBreakReplay
  )
{
  CHAR8   Banner[192];
  UINT64  LogBytes;

  if (!Dev->CompositeMode) {
    return;
  }

  LogBytes = IsLogRingValid (Dev) ? Dev->LogRingHeader->WritePos : 0;

  AsciiSPrint (
    Banner,
    sizeof (Banner),
    "\r\n=== NP05J UEFI USB-UART (%a, %a) | Fastboot=SunUEFI-np05j | CapturedBootLog=%lu bytes ===\r\n",
    BusSpeedName (Dev->BusSpeed),
    IsBreakReplay ? "Replay" : "Connected",
    LogBytes
    );
  TxRingPushAscii (Dev, Banner);

  RewindLogRingToOldest (Dev);
}

//
// Fastboot packetized TX queue & RX/TX pumping
//
STATIC
VOID
FbClearTxQueue (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  while (Dev->FbTxCount > 0U) {
    NP05J_FB_TX_SLOT  *Slot = &Dev->FbTxQueue[Dev->FbTxTail];
    if (Slot->IsBulk && (Slot->BulkPtr != NULL)) {
      ZeroMem (Slot->BulkPtr, Slot->TotalLen);
      FreePool (Slot->BulkPtr);
    }

    ZeroMem (Slot, sizeof (*Slot));
    Dev->FbTxTail = (Dev->FbTxTail + 1U) % NP05J_FB_TX_QUEUE_DEPTH;
    Dev->FbTxCount--;
  }

  Dev->FbTxHead        = 0U;
  Dev->FbTxTail        = 0U;
  Dev->FbTxPending     = FALSE;
  Dev->FbTxInFlightLen = 0U;
}

STATIC
EFI_STATUS
FbSendCallback (
  IN VOID        *Context,
  IN CONST VOID  *Data,
  IN UINTN       Bytes
  )
{
  NP05J_USB_SERIAL_DEV  *Dev = (NP05J_USB_SERIAL_DEV *)Context;
  NP05J_FB_TX_SLOT      *Slot;

  if ((Dev == NULL) || ((Data == NULL) && (Bytes != 0U)) || (Bytes == 0U)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Dev->FbTxCount >= NP05J_FB_TX_QUEUE_DEPTH) {
    return EFI_OUT_OF_RESOURCES;
  }

  Slot = &Dev->FbTxQueue[Dev->FbTxHead];
  ZeroMem (Slot, sizeof (*Slot));

  if (Bytes <= sizeof (Slot->Inline)) {
    CopyMem (Slot->Inline, Data, Bytes);
    Slot->IsBulk   = FALSE;
    Slot->TotalLen = Bytes;
  } else {
    Slot->BulkPtr = AllocatePool (Bytes);
    if (Slot->BulkPtr == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (Slot->BulkPtr, Data, Bytes);
    Slot->IsBulk   = TRUE;
    Slot->TotalLen = Bytes;
  }

  Dev->FbTxHead = (Dev->FbTxHead + 1U) % NP05J_FB_TX_QUEUE_DEPTH;
  Dev->FbTxCount++;
  return EFI_SUCCESS;
}

STATIC
VOID
PumpFbTx (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  NP05J_FB_TX_SLOT  *Slot;
  UINTN             Len;
  EFI_STATUS        Status;

  if ((Dev->UsbfnIo == NULL) ||
      !Dev->Configured ||
      Dev->FbTxPending ||
      (Dev->FbTxCount == 0U) ||
      (Dev->FbTxBuf == NULL))
  {
    return;
  }

  Slot = &Dev->FbTxQueue[Dev->FbTxTail];
  if (!Slot->IsBulk) {
    Len = Slot->TotalLen;
    CopyMem (Dev->FbTxBuf, Slot->Inline, Len);
  } else {
    UINTN  Remaining = Slot->TotalLen - Slot->Offset;
    Len = (Remaining > Dev->FbBufSize) ? Dev->FbBufSize : Remaining;
    CopyMem (Dev->FbTxBuf, Slot->BulkPtr + Slot->Offset, Len);
  }

  Dev->FbTxInFlightLen = Len;
  Status = Dev->UsbfnIo->Transfer (
                           Dev->UsbfnIo,
                           Dev->FbEpIndex,
                           EfiUsbEndpointDirectionDeviceTx,
                           &Len,
                           Dev->FbTxBuf
                           );
  if (!EFI_ERROR (Status)) {
    Dev->FbTxPending = TRUE;
  } else {
    Dev->FbTxInFlightLen = 0U;
  }
}

STATIC
VOID
ArmFbRx (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINTN       Mps;
  UINTN       RxLen;
  EFI_STATUS  Status;

  if ((Dev->UsbfnIo == NULL) ||
      !Dev->Configured ||
      Dev->FbRxPending ||
      (Dev->FbRxBuf == NULL) ||
      Dev->Fastboot.FlashExecuting ||
      Np05jFastbootFlashPending (&Dev->Fastboot))
  {
    return;
  }

  Mps = (Dev->BulkMaxPacket >= 64U) ? Dev->BulkMaxPacket : 512U;

  if (Dev->Fastboot.Receiving) {
    UINTN  Remaining = Dev->Fastboot.Expected - Dev->Fastboot.Received;
    UINTN  Want;
    if (Remaining == 0U) {
      return;
    }

    Want  = ((Remaining + Mps - 1U) / Mps) * Mps;
    RxLen = (Want > Dev->FbBufSize) ? Dev->FbBufSize : Want;
    RxLen = (RxLen / Mps) * Mps;
    if (RxLen == 0U) {
      RxLen = Mps;
    }
  } else {
    if ((Dev->FbTxCount > 0U) || Dev->FbTxPending) {
      return;
    }

    RxLen = Mps;
  }

  if (RxLen > Dev->FbBufSize) {
    RxLen = (Dev->FbBufSize / Mps) * Mps;
  }

  Status = Dev->UsbfnIo->Transfer (
                           Dev->UsbfnIo,
                           Dev->FbEpIndex,
                           EfiUsbEndpointDirectionDeviceRx,
                           &RxLen,
                           Dev->FbRxBuf
                           );
  if (!EFI_ERROR (Status)) {
    Dev->FbRxPending = TRUE;
  }
}

//
// SunUEFI Fastboot Query & Diagnostic / Navigation Callbacks
//
STATIC
EFI_STATUS
FbQueryCallback (
  IN  VOID         *Context,
  IN  CONST CHAR8  *Name,
  OUT CHAR8        Value[60]
  )
{
  NP05J_USB_SERIAL_DEV  *Dev = (NP05J_USB_SERIAL_DEV *)Context;

  if ((Dev == NULL) || (Name == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (AsciiStrCmp (Name, "usb-speed") == 0) {
    AsciiSPrint (Value, 60U, "%a", BusSpeedName (Dev->BusSpeed));
    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "usb-mode") == 0) {
    AsciiSPrint (Value, 60U, "%a", Dev->CompositeMode ? "composite-uart-fb" : "pure-fastboot");
    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "display") == 0) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop = NULL;
    if ((gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop) == EFI_SUCCESS) &&
        (Gop != NULL) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL))
    {
      AsciiSPrint (
        Value,
        60U,
        "%ux%u",
        Gop->Mode->Info->HorizontalResolution,
        Gop->Mode->Info->VerticalResolution
        );
    } else {
      AsciiSPrint (Value, 60U, "2400x1504");
    }

    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "touch") == 0) {
    NP05J_TOUCH_INFO_PROTOCOL  *TouchInfo = NULL;
    NP05J_TOUCH_TELEMETRY      Telem;
    ZeroMem (&Telem, sizeof (Telem));
    if ((gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&TouchInfo) == EFI_SUCCESS) &&
        (TouchInfo != NULL) &&
        !EFI_ERROR (TouchInfo->GetTelemetry (TouchInfo, &Telem)) &&
        Telem.Initialized)
    {
      AsciiSPrint (Value, 60U, "%a", (Telem.PartNumber[0] != '\0') ? Telem.PartNumber : "S3930TB0A0");
    } else {
      AsciiSPrint (Value, 60U, "offline");
    }

    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "ramlog-bytes") == 0) {
    UINT64  Bytes = IsLogRingValid (Dev) ? Dev->LogRingHeader->WritePos : 0ULL;
    AsciiSPrint (Value, 60U, "%lu", Bytes);
    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "ufs-partitions") == 0) {
    UINT32  Total    = 0U;
    UINT32  Writable = 0U;
    Np05jFastbootStorageGetStats (&Total, &Writable);
    AsciiSPrint (Value, 60U, "%u (%u writable)", Total, Writable);
    return EFI_SUCCESS;
  }

  if (AsciiStrCmp (Name, "battery-mv") == 0) {
    EFI_CHARGER_EX_PROTOCOL  *Charger = NULL;
    UINT32                   Mv       = 0U;
    if ((gBS->LocateProtocol (&gChargerExProtocolGuid, NULL, (VOID **)&Charger) == EFI_SUCCESS) &&
        (Charger != NULL) && (Charger->GetBatteryVoltage != NULL))
    {
      Charger->GetBatteryVoltage (&Mv);
    }

    AsciiSPrint (Value, 60U, "%u", Mv);
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
FbDiagnosticCallback (
  IN VOID            *Context,
  IN NP05J_FASTBOOT  *State,
  IN CONST CHAR8     *Command
  )
{
  NP05J_USB_SERIAL_DEV  *Dev = (NP05J_USB_SERIAL_DEV *)Context;
  CHAR8                 Line[64];

  if ((Dev == NULL) || (State == NULL) || (Command == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (AsciiStrCmp (Command, "oem status") == 0) {
    NP05J_BIOS_CONFIG          Cfg;
    NP05J_TOUCH_INFO_PROTOCOL  *TouchInfo = NULL;
    NP05J_TOUCH_TELEMETRY      Telem;
    UINT32                     Total      = 0U;
    UINT32                     Writable   = 0U;
    BOOLEAN                    LogValid   = IsLogRingValid (Dev);
    UINT64                     LogBytes   = LogValid ? Dev->LogRingHeader->WritePos : 0ULL;

    GetLiveBiosConfig (&Cfg);
    ZeroMem (&Telem, sizeof (Telem));
    if ((gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&TouchInfo) == EFI_SUCCESS) &&
        (TouchInfo != NULL))
    {
      TouchInfo->GetTelemetry (TouchInfo, &Telem);
    }

    Np05jFastbootStorageGetStats (&Total, &Writable);

    AsciiSPrint (Line, sizeof (Line), "INFOversion: %a (NP05J / SM8750P)", NP05J_FASTBOOT_BOOTLOADER_VER);
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "INFOusb: %a speed=%a serial=%a",
      Dev->CompositeMode ? "composite" : "fastboot",
      BusSpeedName (Dev->BusSpeed),
      NP05J_FASTBOOT_SERIAL
      );
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "INFOstartup: default=%u splash=%u scale=%ux seq=%lu",
      (UINT32)Cfg.DefaultOs,
      (UINT32)Cfg.SplashStyle,
      (UINT32)Cfg.ConsoleScale,
      Cfg.Sequence
      );
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "INFOtouch: %a frames=%u",
      Telem.Initialized ? "S3930TB0A0" : "offline",
      Telem.FrameCount
      );
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "INFOstorage: gpt=%u writable=%u policy=%a",
      Total,
      Writable,
      Np05jFastbootStorageWritable (State) ? "sunuefi" : "ro"
      );
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    AsciiSPrint (
      Line,
      sizeof (Line),
      "INFOramlog: %a written=%lu bytes",
      (LogValid && (LogBytes > 0ULL)) ? "valid" : "empty",
      LogBytes
      );
    FbSendCallback (Dev, Line, AsciiStrLen (Line));

    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem ramlog") == 0) {
    UINT64      WritePos;
    UINT64      Oldest;
    UINTN       CopyBytes;
    UINT8       *Buf;
    EFI_STATUS  Status;

    if (!IsLogRingValid (Dev) || (Dev->LogRingHeader->WritePos == 0ULL)) {
      return FbSendCallback (Dev, "FAILramlog unavailable", AsciiStrLen ("FAILramlog unavailable"));
    }

    WritePos = Dev->LogRingHeader->WritePos;
    if (WritePos > NP05J_LOG_RING_CAPACITY) {
      Oldest    = WritePos - NP05J_LOG_RING_CAPACITY;
      CopyBytes = NP05J_LOG_RING_CAPACITY;
    } else {
      Oldest    = 0ULL;
      CopyBytes = (UINTN)WritePos;
    }

    Buf = AllocatePool (CopyBytes);
    if (Buf == NULL) {
      return FbSendCallback (Dev, "FAILnot enough RAM", AsciiStrLen ("FAILnot enough RAM"));
    }

    for (UINTN I = 0U; I < CopyBytes; I++) {
      Buf[I] = Dev->LogRingData[(UINTN)((Oldest + I) & (NP05J_LOG_RING_CAPACITY - 1U))];
    }

    Status = Np05jFastbootStageCopy (State, Buf, CopyBytes);
    ZeroMem (Buf, CopyBytes);
    FreePool (Buf);

    if (EFI_ERROR (Status)) {
      return FbSendCallback (Dev, "FAILramlog stage failed", AsciiStrLen ("FAILramlog stage failed"));
    }

    AsciiSPrint (Line, sizeof (Line), "INFOramlog staged %u bytes", (UINT32)CopyBytes);
    FbSendCallback (Dev, Line, AsciiStrLen (Line));
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem screenshot") == 0) {
    UINT32      W = 0U;
    UINT32      H = 0U;
    UINT32      Bytes = 0U;
    UINT32      Crc32 = 0U;
    EFI_STATUS  Status;

    Status = Np05jFastbootCaptureScreen (State, &W, &H, &Bytes, &Crc32);
    if (EFI_ERROR (Status)) {
      return FbSendCallback (Dev, "FAILscreenshot failed", AsciiStrLen ("FAILscreenshot failed"));
    }

    AsciiSPrint (Line, sizeof (Line), "INFOscreenshot %ux%u %uB crc32=0x%08x", W, H, Bytes, Crc32);
    FbSendCallback (Dev, Line, AsciiStrLen (Line));
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem setup") == 0) {
    Dev->PendingFbAction = Np05jFbActionOpenSetup;
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem shell") == 0) {
    Dev->PendingFbAction = Np05jFbActionOpenShell;
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem simpleinit") == 0) {
    Dev->PendingFbAction = Np05jFbActionOpenBootMenu;
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  if (AsciiStrCmp (Command, "oem boot-stable") == 0) {
    Dev->PendingFbAction = Np05jFbActionBootStable;
    return FbSendCallback (Dev, "OKAY", 4U);
  }

  return EFI_UNSUPPORTED;
}

//
// Helper: EP0 Transfer submission
//
STATIC
EFI_STATUS
Ep0SubmitStatusIn (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINTN       Size;
  EFI_STATUS  Status;

  Size          = 0;
  Dev->Ep0Stage = Ep0StageStatusIn;
  Status        = Dev->UsbfnIo->Transfer (
                                  Dev->UsbfnIo,
                                  NP05J_EP_CONTROL,
                                  EfiUsbEndpointDirectionDeviceTx,
                                  &Size,
                                  Dev->Ep0Buf
                                  );
  if (EFI_ERROR (Status)) {
    Dev->Ep0Stage = Ep0StageIdle;
  }

  return Status;
}

STATIC
EFI_STATUS
Ep0SubmitStatusOut (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINTN       Size;
  EFI_STATUS  Status;

  Size          = 0;
  Dev->Ep0Stage = Ep0StageStatusOut;
  Status        = Dev->UsbfnIo->Transfer (
                                  Dev->UsbfnIo,
                                  NP05J_EP_CONTROL,
                                  EfiUsbEndpointDirectionDeviceRx,
                                  &Size,
                                  Dev->Ep0Buf
                                  );
  if (EFI_ERROR (Status)) {
    Dev->Ep0Stage = Ep0StageIdle;
  }

  return Status;
}

STATIC
EFI_STATUS
Ep0SubmitDataIn (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN CONST VOID            *Data,
  IN UINTN                 DataLen,
  IN UINT16                HostMaxLen
  )
{
  UINTN       SendLen;
  EFI_STATUS  Status;

  SendLen = (DataLen < HostMaxLen) ? DataLen : HostMaxLen;
  if (SendLen > NP05J_EP0_BUF_SIZE) {
    SendLen = NP05J_EP0_BUF_SIZE;
  }

  if (SendLen == 0) {
    return Ep0SubmitStatusIn (Dev);
  }

  CopyMem (Dev->Ep0Buf, Data, SendLen);
  Dev->Ep0Stage = Ep0StageDataIn;
  Status        = Dev->UsbfnIo->Transfer (
                                  Dev->UsbfnIo,
                                  NP05J_EP_CONTROL,
                                  EfiUsbEndpointDirectionDeviceTx,
                                  &SendLen,
                                  Dev->Ep0Buf
                                  );
  if (EFI_ERROR (Status)) {
    Dev->Ep0Stage = Ep0StageIdle;
  }

  return Status;
}

STATIC
EFI_STATUS
Ep0SubmitDataOut (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN UINT16                Len
  )
{
  UINTN       RxLen;
  EFI_STATUS  Status;

  if ((Len == 0) || (Len > NP05J_EP0_BUF_SIZE)) {
    return EFI_INVALID_PARAMETER;
  }

  RxLen         = Len;
  Dev->Ep0Stage = Ep0StageDataOut;
  Status        = Dev->UsbfnIo->Transfer (
                                  Dev->UsbfnIo,
                                  NP05J_EP_CONTROL,
                                  EfiUsbEndpointDirectionDeviceRx,
                                  &RxLen,
                                  Dev->Ep0Buf
                                  );
  if (EFI_ERROR (Status)) {
    Dev->Ep0Stage = Ep0StageIdle;
  }

  return Status;
}

typedef
EFI_STATUS
(EFIAPI *NP05J_USBFN_SET_STALL_BY_VALUE)(
  IN EFI_USBFN_IO_PROTOCOL         *This,
  IN UINT8                         EndpointIndex,
  IN EFI_USBFN_ENDPOINT_DIRECTION  Direction,
  IN BOOLEAN                       State
  );

STATIC
EFI_STATUS
Np05jSetEndpointStallState (
  IN EFI_USBFN_IO_PROTOCOL         *UsbfnIo,
  IN UINT8                         EndpointIndex,
  IN EFI_USBFN_ENDPOINT_DIRECTION  Direction,
  IN BOOLEAN                       State
  )
{
  //
  // MdePkg UsbFunctionIo.h has a historic header typo (BOOLEAN *State), whereas
  // Qualcomm UsbfnDwc3Dxe (and EFIUsbfnIo.h) take BOOLEAN State by value in w3.
  //
  NP05J_USBFN_SET_STALL_BY_VALUE  SetStall;

  SetStall = (NP05J_USBFN_SET_STALL_BY_VALUE)(VOID *)UsbfnIo->SetEndpointStallState;
  return SetStall (UsbfnIo, EndpointIndex, Direction, State);
}

STATIC
VOID
Ep0Stall (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  EFI_USBFN_ENDPOINT_DIRECTION  Dir;

  Dir           = ((Req->RequestType & 0x80U) != 0) ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;
  Dev->Ep0Stage = Ep0StageIdle;
  Np05jSetEndpointStallState (Dev->UsbfnIo, NP05J_EP_CONTROL, Dir, TRUE);
}

//
// Helper: CDC Bulk OUT RX & Bulk IN TX (CompositeMode only)
//
STATIC
VOID
ArmBulkRx (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINTN       RxLen;
  EFI_STATUS  Status;

  if ((Dev->UsbfnIo == NULL) ||
      !Dev->CompositeMode ||
      !Dev->Configured ||
      Dev->BulkRxPending ||
      (Dev->BulkRxBuf == NULL))
  {
    return;
  }

  RxLen = Dev->BulkMaxPacket;
  if ((RxLen == 0) || (RxLen > NP05J_BULK_RX_BUF_SIZE)) {
    RxLen = 512;
  }

  Status = Dev->UsbfnIo->Transfer (
                           Dev->UsbfnIo,
                           NP05J_EP_BULK_INDEX,
                           EfiUsbEndpointDirectionDeviceRx,
                           &RxLen,
                           Dev->BulkRxBuf
                           );
  if (!EFI_ERROR (Status)) {
    Dev->BulkRxPending = TRUE;
  }
}

STATIC
UINTN
FillChunkFromLogRing (
  IN  NP05J_USB_SERIAL_DEV  *Dev,
  OUT UINT8                 *Dst,
  IN  UINTN                 MaxLen
  )
{
  UINT64  WritePos;
  UINT64  Oldest;
  UINTN   OutLen;
  UINT32  Mask;
  UINT8   Ch;

  if (!IsLogRingValid (Dev) || (MaxLen < 2)) {
    return 0;
  }

  WritePos = Dev->LogRingHeader->WritePos;
  if (WritePos > NP05J_LOG_RING_CAPACITY) {
    Oldest = WritePos - NP05J_LOG_RING_CAPACITY;
    if (Dev->LogReadPos < Oldest) {
      Dev->LogReadPos = Oldest;
    }
  }

  Mask   = NP05J_LOG_RING_CAPACITY - 1U;
  OutLen = 0;

  while ((Dev->LogReadPos < WritePos) && ((OutLen + 2) <= MaxLen)) {
    Ch = Dev->LogRingData[(UINTN)(Dev->LogReadPos & Mask)];
    Dev->LogReadPos++;

    if ((Ch == '\n') && (Dev->LogLastChar != '\r')) {
      Dst[OutLen++] = '\r';
    }

    Dst[OutLen++]    = Ch;
    Dev->LogLastChar = Ch;
  }

  return OutLen;
}

STATIC
VOID
PumpBulkTx (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  UINTN       MaxChunk;
  UINTN       ChunkLen;
  EFI_STATUS  Status;

  if ((Dev->UsbfnIo == NULL) ||
      !Dev->CompositeMode ||
      !Dev->Configured ||
      !Dev->HostReady ||
      Dev->BulkTxPending ||
      (Dev->BulkTxBuf == NULL))
  {
    return;
  }

  MaxChunk = (Dev->BulkMaxPacket > 1U) ? (UINTN)(Dev->BulkMaxPacket - 1U) : 63U;
  if (MaxChunk > NP05J_BULK_TX_BUF_SIZE) {
    MaxChunk = NP05J_BULK_TX_BUF_SIZE;
  }

  ChunkLen = TxRingPopBytes (Dev, (UINT8 *)Dev->BulkTxBuf, MaxChunk);
  if (ChunkLen < MaxChunk) {
    ChunkLen += FillChunkFromLogRing (
                  Dev,
                  ((UINT8 *)Dev->BulkTxBuf) + ChunkLen,
                  MaxChunk - ChunkLen
                  );
  }

  if (ChunkLen == 0) {
    return;
  }

  Status = Dev->UsbfnIo->Transfer (
                           Dev->UsbfnIo,
                           NP05J_EP_BULK_INDEX,
                           EfiUsbEndpointDirectionDeviceTx,
                           &ChunkLen,
                           Dev->BulkTxBuf
                           );
  if (!EFI_ERROR (Status)) {
    Dev->BulkTxPending = TRUE;
  }
}

//
// EP0 Standard, Class & Vendor Request Handlers
//
STATIC
VOID
HandleGetDescriptor (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  UINT8                        DescType;
  UINT8                        DescIndex;
  BOOLEAN                      IsSuperSpeed;
  NP05J_COMP_HS_CONFIG_BUNDLE  CompOtherCfg;
  NP05J_FB_HS_CONFIG_BUNDLE    FbOtherCfg;
  UINT8                        StrBuf[128];
  CONST CHAR16                 *Str;
  UINTN                        StrChars;
  UINTN                        TotalBytes;

  DescType     = (UINT8)(Req->Value >> 8);
  DescIndex    = (UINT8)(Req->Value & 0xFFU);
  IsSuperSpeed = ((UINT32)Dev->BusSpeed >= (UINT32)UsbBusSpeedSuper);

  switch (DescType) {
    case USB_DESC_TYPE_DEVICE:
      if (Dev->CompositeMode) {
        if (IsSuperSpeed) {
          mCompSsDeviceDesc.BcdUSB = ((UINT32)Dev->BusSpeed == NP05J_BUS_SPEED_SUPER_PLUS) ? 0x0310 : 0x0300;
          Ep0SubmitDataIn (Dev, &mCompSsDeviceDesc, sizeof (mCompSsDeviceDesc), Req->Length);
        } else {
          Ep0SubmitDataIn (Dev, &mCompHsDeviceDesc, sizeof (mCompHsDeviceDesc), Req->Length);
        }
      } else {
        if (IsSuperSpeed) {
          mFbSsDeviceDesc.BcdUSB = ((UINT32)Dev->BusSpeed == NP05J_BUS_SPEED_SUPER_PLUS) ? 0x0310 : 0x0300;
          Ep0SubmitDataIn (Dev, &mFbSsDeviceDesc, sizeof (mFbSsDeviceDesc), Req->Length);
        } else {
          Ep0SubmitDataIn (Dev, &mFbHsDeviceDesc, sizeof (mFbHsDeviceDesc), Req->Length);
        }
      }

      return;

    case USB_DESC_TYPE_CONFIG:
      if (DescIndex != 0) {
        Ep0Stall (Dev, Req);
        return;
      }

      if (Dev->CompositeMode) {
        if (IsSuperSpeed) {
          Ep0SubmitDataIn (Dev, &mCompSsConfigBundle, sizeof (mCompSsConfigBundle), Req->Length);
        } else if (Dev->BusSpeed == UsbBusSpeedFull) {
          Ep0SubmitDataIn (Dev, &mCompFsConfigBundle, sizeof (mCompFsConfigBundle), Req->Length);
        } else {
          Ep0SubmitDataIn (Dev, &mCompHsConfigBundle, sizeof (mCompHsConfigBundle), Req->Length);
        }
      } else {
        if (IsSuperSpeed) {
          Ep0SubmitDataIn (Dev, &mFbSsConfigBundle, sizeof (mFbSsConfigBundle), Req->Length);
        } else if (Dev->BusSpeed == UsbBusSpeedFull) {
          Ep0SubmitDataIn (Dev, &mFbFsConfigBundle, sizeof (mFbFsConfigBundle), Req->Length);
        } else {
          Ep0SubmitDataIn (Dev, &mFbHsConfigBundle, sizeof (mFbHsConfigBundle), Req->Length);
        }
      }

      return;

    case USB_DESC_TYPE_STRING:
      if (DescIndex == 0) {
        Ep0SubmitDataIn (Dev, mStrDescLangId, sizeof (mStrDescLangId), Req->Length);
        return;
      }

      if (DescIndex == 0xEEU) {
        Ep0SubmitDataIn (Dev, mStrDescMsft100, sizeof (mStrDescMsft100), Req->Length);
        return;
      }

      switch (DescIndex) {
        case 1:
          Str = L"SunUEFI / Project Silicium";
          break;
        case 2:
          Str = Dev->CompositeMode ? L"NP05J SunUEFI Fastboot + UART" : L"NP05J SunUEFI Fastboot";
          break;
        case 3:
          Str = L"SunUEFI-np05j";
          break;
        case 4:
          Str = L"SunUEFI Fastboot";
          break;
        default:
          Ep0Stall (Dev, Req);
          return;
      }

      StrChars   = StrLen (Str);
      TotalBytes = 2U + (StrChars * 2U);
      if (TotalBytes > sizeof (StrBuf)) {
        TotalBytes = sizeof (StrBuf);
        StrChars   = (TotalBytes - 2U) / 2U;
      }

      StrBuf[0] = (UINT8)TotalBytes;
      StrBuf[1] = USB_DESC_TYPE_STRING;
      CopyMem (&StrBuf[2], Str, StrChars * 2U);
      Ep0SubmitDataIn (Dev, StrBuf, TotalBytes, Req->Length);
      return;

    case NP05J_USB_DESC_DEV_QUALIFIER:
      if (IsSuperSpeed) {
        Ep0Stall (Dev, Req);
      } else if (Dev->CompositeMode) {
        Ep0SubmitDataIn (Dev, &mCompDeviceQualifierDesc, sizeof (mCompDeviceQualifierDesc), Req->Length);
      } else {
        Ep0SubmitDataIn (Dev, &mFbDeviceQualifierDesc, sizeof (mFbDeviceQualifierDesc), Req->Length);
      }

      return;

    case NP05J_USB_DESC_OTHER_SPEED:
      if (IsSuperSpeed || (DescIndex != 0)) {
        Ep0Stall (Dev, Req);
        return;
      }

      if (Dev->CompositeMode) {
        if (Dev->BusSpeed == UsbBusSpeedFull) {
          CopyMem (&CompOtherCfg, &mCompHsConfigBundle, sizeof (CompOtherCfg));
        } else {
          CopyMem (&CompOtherCfg, &mCompFsConfigBundle, sizeof (CompOtherCfg));
        }

        CompOtherCfg.Config.DescriptorType = NP05J_USB_DESC_OTHER_SPEED;
        Ep0SubmitDataIn (Dev, &CompOtherCfg, sizeof (CompOtherCfg), Req->Length);
      } else {
        if (Dev->BusSpeed == UsbBusSpeedFull) {
          CopyMem (&FbOtherCfg, &mFbHsConfigBundle, sizeof (FbOtherCfg));
        } else {
          CopyMem (&FbOtherCfg, &mFbFsConfigBundle, sizeof (FbOtherCfg));
        }

        FbOtherCfg.Config.DescriptorType = NP05J_USB_DESC_OTHER_SPEED;
        Ep0SubmitDataIn (Dev, &FbOtherCfg, sizeof (FbOtherCfg), Req->Length);
      }

      return;

    case USB_DESC_TYPE_BOS:
      Ep0SubmitDataIn (Dev, &mBosBundle, sizeof (mBosBundle), Req->Length);
      return;

    default:
      Ep0Stall (Dev, Req);
      return;
  }
}

STATIC
VOID
HandleStandardSetup (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  UINT8                         Recipient;
  UINT16                        StatusWord;
  BOOLEAN                       Stalled;
  UINT8                         EpIdx;
  EFI_USBFN_ENDPOINT_DIRECTION  EpDir;
  UINT8                         ByteVal;

  Recipient = Req->RequestType & 0x1FU;

  switch (Req->Request) {
    case USB_REQ_GET_STATUS:
      StatusWord = 0;
      if (Recipient == USB_TARGET_DEVICE) {
        StatusWord = 0x0001; // Self-powered
      } else if (Recipient == USB_TARGET_ENDPOINT) {
        EpIdx   = (UINT8)(Req->Index & 0x0FU);
        EpDir   = ((Req->Index & 0x80U) != 0) ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;
        Stalled = FALSE;
        if (!EFI_ERROR (Dev->UsbfnIo->GetEndpointStallState (Dev->UsbfnIo, EpIdx, EpDir, &Stalled)) && Stalled) {
          StatusWord = 0x0001;
        }
      }

      Ep0SubmitDataIn (Dev, &StatusWord, sizeof (StatusWord), Req->Length);
      return;

    case USB_REQ_CLEAR_FEATURE:
      if ((Recipient == USB_TARGET_ENDPOINT) && (Req->Value == 0)) {
        EpIdx = (UINT8)(Req->Index & 0x0FU);
        EpDir = ((Req->Index & 0x80U) != 0) ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;
        Np05jSetEndpointStallState (Dev->UsbfnIo, EpIdx, EpDir, FALSE);
      }

      Ep0SubmitStatusIn (Dev);
      if (Dev->Configured) {
        PumpFbTx (Dev);
        ArmFbRx (Dev);
      }

      return;

    case USB_REQ_SET_FEATURE:
      if ((Recipient == USB_TARGET_ENDPOINT) && (Req->Value == 0)) {
        EpIdx = (UINT8)(Req->Index & 0x0FU);
        EpDir = ((Req->Index & 0x80U) != 0) ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;
        Np05jSetEndpointStallState (Dev->UsbfnIo, EpIdx, EpDir, TRUE);
      }

      Ep0SubmitStatusIn (Dev);
      return;

    case USB_REQ_SET_ADDRESS:
      Dev->UsbAddress = (UINT8)(Req->Value & 0x7FU);
      Ep0SubmitStatusIn (Dev);
      return;

    case USB_REQ_GET_DESCRIPTOR:
      HandleGetDescriptor (Dev, Req);
      return;

    case USB_REQ_GET_CONFIG:
      ByteVal = Dev->ConfigValue;
      Ep0SubmitDataIn (Dev, &ByteVal, 1, Req->Length);
      return;

    case USB_REQ_SET_CONFIG:
      if ((Req->Value & 0xFFU) == 1U) {
        Dev->ConfigValue   = 1;
        Dev->Configured    = TRUE;
        Dev->BulkRxPending = FALSE;
        Dev->BulkTxPending = FALSE;
        Dev->FbRxPending   = FALSE;
        Dev->FbTxPending   = FALSE;
        Ep0SubmitStatusIn (Dev);
        if (Dev->CompositeMode) {
          ArmBulkRx (Dev);
        }

        ArmFbRx (Dev);
      } else if ((Req->Value & 0xFFU) == 0U) {
        Dev->ConfigValue   = 0;
        Dev->Configured    = FALSE;
        Dev->Dtr           = FALSE;
        Dev->Rts           = FALSE;
        Dev->HostReady     = FALSE;
        Dev->BulkRxPending = FALSE;
        Dev->BulkTxPending = FALSE;
        Dev->FbRxPending   = FALSE;
        FbClearTxQueue (Dev);
        Np05jFastbootReset (&Dev->Fastboot);
        Ep0SubmitStatusIn (Dev);
      } else {
        Ep0Stall (Dev, Req);
      }

      return;

    case USB_REQ_GET_INTERFACE:
      ByteVal = 0;
      Ep0SubmitDataIn (Dev, &ByteVal, 1, Req->Length);
      return;

    case USB_REQ_SET_INTERFACE:
      if ((Req->Value == 0U) && (Req->Index <= (Dev->CompositeMode ? 2U : 0U))) {
        Ep0SubmitStatusIn (Dev);
      } else {
        Ep0Stall (Dev, Req);
      }

      return;

    case NP05J_USB_REQ_SET_SEL:
      if (Req->Length > 0) {
        if (EFI_ERROR (Ep0SubmitDataOut (Dev, Req->Length))) {
          Ep0Stall (Dev, Req);
        }
      } else {
        Ep0SubmitStatusIn (Dev);
      }

      return;

    case NP05J_USB_REQ_SET_ISOCH_DELAY:
      Ep0SubmitStatusIn (Dev);
      return;

    default:
      Ep0Stall (Dev, Req);
      return;
  }
}

STATIC
VOID
HandleClassSetup (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  BOOLEAN  NewDtr;

  if (!Dev->CompositeMode) {
    Ep0Stall (Dev, Req);
    return;
  }

  switch (Req->Request) {
    case USB_CDC_REQ_SET_LINE_CODING:
      if ((Req->Length == sizeof (USB_CDC_LINE_CODING)) && ((Req->RequestType & 0x80U) == 0)) {
        if (EFI_ERROR (Ep0SubmitDataOut (Dev, Req->Length))) {
          Ep0Stall (Dev, Req);
        }
      } else {
        Ep0Stall (Dev, Req);
      }

      return;

    case USB_CDC_REQ_GET_LINE_CODING:
      if (((Req->RequestType & 0x80U) != 0) && (Req->Length >= sizeof (USB_CDC_LINE_CODING))) {
        Ep0SubmitDataIn (Dev, &Dev->LineCoding, sizeof (Dev->LineCoding), Req->Length);
      } else {
        Ep0Stall (Dev, Req);
      }

      return;

    case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
      NewDtr   = ((Req->Value & 0x0001U) != 0);
      Dev->Rts = ((Req->Value & 0x0002U) != 0);
      if (NewDtr && !Dev->Dtr) {
        Dev->Dtr       = TRUE;
        Dev->HostReady = TRUE;
        OnHostConnected (Dev, FALSE);
      } else if (!NewDtr) {
        Dev->Dtr       = FALSE;
        Dev->HostReady = FALSE;
      }

      Ep0SubmitStatusIn (Dev);
      return;

    case USB_CDC_REQ_SEND_BREAK:
      if (Req->Value != 0) {
        Dev->HostReady = TRUE;
        OnHostConnected (Dev, TRUE);
      }

      Ep0SubmitStatusIn (Dev);
      return;

    default:
      Ep0Stall (Dev, Req);
      return;
  }
}

STATIC
VOID
HandleVendorSetup (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  if (((Req->RequestType & 0x80U) == 0U) ||
      (Req->Request != NP05J_USB_REQ_MS_VENDOR) ||
      (Req->Length == 0U))
  {
    Ep0Stall (Dev, Req);
    return;
  }

  if ((Req->Index == 0x0000U) && (Req->Value == 0x0000U)) {
    UINT8  Probe[12];
    CopyMem (Probe, "SUNUEFI1", 8U);
    Probe[8]  = Dev->UsbAddress;
    Probe[9]  = Dev->ConfigValue;
    Probe[10] = 1U;
    Probe[11] = Dev->CompositeMode ? 0x43U : 0x41U;
    Ep0SubmitDataIn (Dev, Probe, sizeof (Probe), Req->Length);
    return;
  }

  if (Req->Index == 0x0004U) {
    NP05J_MS_OS_COMPAT_ID_DESC  Compat;
    CopyMem (&Compat, &mMsOsCompatIdTemplate, sizeof (Compat));
    Compat.FirstInterfaceNumber = Dev->CompositeMode ? 2U : 0U;
    Ep0SubmitDataIn (Dev, &Compat, sizeof (Compat), Req->Length);
    return;
  }

  if (Req->Index == 0x0005U) {
    Ep0SubmitDataIn (Dev, &mMsOsExtPropDesc, sizeof (mMsOsExtPropDesc), Req->Length);
    return;
  }

  Ep0Stall (Dev, Req);
}

STATIC
VOID
HandleSetupPacket (
  IN NP05J_USB_SERIAL_DEV          *Dev,
  IN CONST EFI_USB_DEVICE_REQUEST  *Req
  )
{
  UINT8  ReqType;

  CopyMem (&Dev->Ep0PendingReq, Req, sizeof (Dev->Ep0PendingReq));
  Dev->Ep0Stage = Ep0StageIdle;

  ReqType = Req->RequestType & 0x60U;
  if (ReqType == USB_REQ_TYPE_STANDARD) {
    HandleStandardSetup (Dev, Req);
  } else if (ReqType == USB_REQ_TYPE_CLASS) {
    HandleClassSetup (Dev, Req);
  } else if (ReqType == USB_REQ_TYPE_VENDOR) {
    HandleVendorSetup (Dev, Req);
  } else {
    Ep0Stall (Dev, Req);
  }
}

STATIC
VOID
HandleEp0RxComplete (
  IN NP05J_USB_SERIAL_DEV             *Dev,
  IN CONST EFI_USBFN_TRANSFER_RESULT  *Result
  )
{
  if (Result->TransferStatus != UsbTransferStatusComplete) {
    Dev->Ep0Stage = Ep0StageIdle;
    return;
  }

  if (Dev->Ep0Stage == Ep0StageDataOut) {
    if (Dev->CompositeMode &&
        ((Dev->Ep0PendingReq.RequestType & 0x60U) == USB_REQ_TYPE_CLASS) &&
        (Dev->Ep0PendingReq.Request == USB_CDC_REQ_SET_LINE_CODING) &&
        (Result->BytesTransferred >= sizeof (USB_CDC_LINE_CODING)))
    {
      CopyMem (&Dev->LineCoding, Dev->Ep0Buf, sizeof (Dev->LineCoding));
      if (Dev->LineCoding.DteRate != 0) {
        Dev->SerialMode.BaudRate = Dev->LineCoding.DteRate;
      }

      if (Dev->LineCoding.DataBits != 0) {
        Dev->SerialMode.DataBits = Dev->LineCoding.DataBits;
      }
    }

    Ep0SubmitStatusIn (Dev);
  } else {
    Dev->Ep0Stage = Ep0StageIdle;
  }
}

STATIC
VOID
HandleEp0TxComplete (
  IN NP05J_USB_SERIAL_DEV             *Dev,
  IN CONST EFI_USBFN_TRANSFER_RESULT  *Result
  )
{
  if (Result->TransferStatus != UsbTransferStatusComplete) {
    Dev->Ep0Stage = Ep0StageIdle;
    return;
  }

  if (Dev->Ep0Stage == Ep0StageDataIn) {
    Ep0SubmitStatusOut (Dev);
  } else {
    Dev->Ep0Stage = Ep0StageIdle;
  }
}

STATIC
VOID
ResetBusState (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  Dev->Configured    = FALSE;
  Dev->UsbAddress    = 0;
  Dev->ConfigValue   = 0;
  Dev->Dtr           = FALSE;
  Dev->Rts           = FALSE;
  Dev->HostReady     = FALSE;
  Dev->Ep0Stage      = Ep0StageIdle;
  Dev->BulkRxPending = FALSE;
  Dev->BulkTxPending = FALSE;
  Dev->FbRxPending   = FALSE;
  FbClearTxQueue (Dev);
  Np05jFastbootReset (&Dev->Fastboot);
}

STATIC VOID TeardownUsbfn (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN BOOLEAN               CloseProtocol
  );

//
// Main UsbfnIo EventHandler polling loop (always runs at TPL_CALLBACK for USB I/O)
//
STATIC
VOID
Np05jUsbSerialPoll (
  IN NP05J_USB_SERIAL_DEV  *Dev
  )
{
  EFI_TPL                    CurrentTpl;
  EFI_TPL                    OldTpl;
  EFI_USBFN_MESSAGE          Message;
  UINTN                      PayloadSize;
  EFI_USBFN_MESSAGE_PAYLOAD  Payload;
  EFI_STATUS                 Status;
  UINTN                      NoneCount;

  if (Dev->InPoll || Dev->ExitedBootServices || (Dev->UsbfnIo == NULL)) {
    return;
  }

  CurrentTpl = EfiGetCurrentTpl ();
  if (CurrentTpl > TPL_CALLBACK) {
    return;
  }

  OldTpl      = gBS->RaiseTPL (TPL_CALLBACK);
  Dev->InPoll = TRUE;
  NoneCount   = 0;

  for (UINTN Iter = 0; Iter < 32U; Iter++) {
    if (Dev->UsbfnIo == NULL) {
      break;
    }

    Message     = EfiUsbMsgNone;
    PayloadSize = sizeof (Payload);
    ZeroMem (&Payload, sizeof (Payload));

    Status = Dev->UsbfnIo->EventHandler (Dev->UsbfnIo, &Message, &PayloadSize, &Payload);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Message == EfiUsbMsgNone) {
      NoneCount++;
      if (NoneCount >= 2U) {
        break;
      }

      continue;
    }

    NoneCount = 0;

    switch (Message) {
      case EfiUsbMsgBusEventAttach:
        Dev->Attached = TRUE;
        break;

      case EfiUsbMsgBusEventDetach:
        Dev->Attached = FALSE;
        ResetBusState (Dev);
        break;

      case EfiUsbMsgBusEventReset:
        ResetBusState (Dev);
        break;

      case EfiUsbMsgBusEventSpeed:
        Dev->BusSpeed = Payload.ubs;
        if ((UINT32)Dev->BusSpeed >= (UINT32)UsbBusSpeedSuper) {
          Dev->BulkMaxPacket = 1024;
        } else if (Dev->BusSpeed == UsbBusSpeedFull) {
          Dev->BulkMaxPacket = 64;
        } else {
          Dev->BulkMaxPacket = 512;
        }

        break;

      case EfiUsbMsgSetupPacket:
        HandleSetupPacket (Dev, &Payload.udr);
        break;

      case EfiUsbMsgEndpointStatusChangedRx:
        if (Payload.utr.EndpointIndex == NP05J_EP_CONTROL) {
          HandleEp0RxComplete (Dev, &Payload.utr);
        } else if (Payload.utr.EndpointIndex == Dev->FbEpIndex) {
          Dev->FbRxPending = FALSE;
          if (Payload.utr.TransferStatus == UsbTransferStatusComplete) {
            Np05jFastbootPacket (&Dev->Fastboot, Dev->FbRxBuf, Payload.utr.BytesTransferred);
            PumpFbTx (Dev);
            ArmFbRx (Dev);
          }
        } else if (Dev->CompositeMode && (Payload.utr.EndpointIndex == NP05J_EP_BULK_INDEX)) {
          Dev->BulkRxPending = FALSE;
          if (Payload.utr.TransferStatus == UsbTransferStatusComplete) {
            if (Payload.utr.BytesTransferred > 0) {
              RxRingPushBytes (Dev, (CONST UINT8 *)Dev->BulkRxBuf, Payload.utr.BytesTransferred);
              if (!Dev->HostReady) {
                Dev->HostReady = TRUE;
                OnHostConnected (Dev, FALSE);
              }
            }

            ArmBulkRx (Dev);
          }
        }

        break;

      case EfiUsbMsgEndpointStatusChangedTx:
        if (Payload.utr.EndpointIndex == NP05J_EP_CONTROL) {
          HandleEp0TxComplete (Dev, &Payload.utr);
        } else if (Payload.utr.EndpointIndex == Dev->FbEpIndex) {
          Dev->FbTxPending = FALSE;
          if ((Payload.utr.TransferStatus == UsbTransferStatusComplete) && (Dev->FbTxCount > 0U)) {
            NP05J_FB_TX_SLOT  *Slot = &Dev->FbTxQueue[Dev->FbTxTail];
            Slot->Offset += Dev->FbTxInFlightLen;
            Dev->FbTxInFlightLen = 0U;
            if (Slot->Offset >= Slot->TotalLen) {
              if (Slot->IsBulk && (Slot->BulkPtr != NULL)) {
                ZeroMem (Slot->BulkPtr, Slot->TotalLen);
                FreePool (Slot->BulkPtr);
              }

              ZeroMem (Slot, sizeof (*Slot));
              Dev->FbTxTail = (Dev->FbTxTail + 1U) % NP05J_FB_TX_QUEUE_DEPTH;
              Dev->FbTxCount--;
            }

            PumpFbTx (Dev);
            ArmFbRx (Dev);
          } else {
            Dev->FbTxInFlightLen = 0U;
          }
        } else if (Dev->CompositeMode && (Payload.utr.EndpointIndex == NP05J_EP_BULK_INDEX)) {
          Dev->BulkTxPending = FALSE;
          if (Payload.utr.TransferStatus == UsbTransferStatusComplete) {
            PumpBulkTx (Dev);
          }
        }

        break;

      default:
        break;
    }
  }

  PumpBulkTx (Dev);
  ArmBulkRx (Dev);
  PumpFbTx (Dev);
  ArmFbRx (Dev);

  Dev->InPoll = FALSE;
  gBS->RestoreTPL (OldTpl);

  if (Dev->AppServiceSeenTicks < 0xFFFFFFFFU) {
    Dev->AppServiceSeenTicks++;
  }

  // Fallback flash step when UiApp or UEFI Shell is active (no BootMenuApp polling)
  if (Np05jFastbootFlashPending (&Dev->Fastboot) &&
      (Dev->FbTxCount == 0U) &&
      !Dev->FbTxPending &&
      !Dev->Fastboot.FlashExecuting &&
      (Dev->AppServiceSeenTicks >= 60U))
  {
    Np05jFastbootFlashStep (&Dev->Fastboot);
    OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
    PumpFbTx (Dev);
    ArmFbRx (Dev);
    gBS->RestoreTPL (OldTpl);
  }

  // Fallback reboot/continue if BootMenuApp isn't actively consuming ServiceStep
  if ((Dev->Fastboot.RebootRequested || Dev->Fastboot.ExitRequested) &&
      (Dev->FbTxCount == 0U) &&
      !Dev->FbTxPending)
  {
    if (++Dev->RebootGraceTicks >= 20U) {
      TeardownUsbfn (Dev, FALSE);
      Np05jExecuteReset (Np05jResetNormal);
    }
  } else {
    Dev->RebootGraceTicks = 0U;
  }
}

STATIC
VOID
EFIAPI
Np05jUsbSerialTimerCb (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  Np05jUsbSerialPoll ((NP05J_USB_SERIAL_DEV *)Context);
}

//
// NP05J_FASTBOOT_PROTOCOL Implementation
//
STATIC
EFI_STATUS
EFIAPI
Np05jFbGetStatus (
  IN  NP05J_FASTBOOT_PROTOCOL  *This,
  OUT NP05J_FASTBOOT_STATUS    *Status
  )
{
  UINT32  Total    = 0U;
  UINT32  Writable = 0U;

  if ((This == NULL) || (Status == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Status, sizeof (*Status));
  Np05jFastbootStorageGetStats (&Total, &Writable);

  Status->Attached           = mDev.Attached;
  Status->Configured         = mDev.Configured;
  Status->CompositeMode      = mDev.CompositeMode;
  Status->DiagnosticsOnly    = mDev.Fastboot.DiagnosticsOnly;
  Status->FlashActive        = Np05jFastbootFlashPending (&mDev.Fastboot);
  Status->BusSpeed           = (UINT8)mDev.BusSpeed;
  Status->PartitionsFound    = Total;
  Status->WritablePartitions = Writable;
  Status->DownloadBytes      = mDev.Fastboot.Complete ? mDev.Fastboot.Received : 0ULL;
  Status->CommandsHandled    = mDev.Fastboot.CommandsHandled;

  AsciiSPrint (Status->SerialNo, sizeof (Status->SerialNo), "%a", NP05J_FASTBOOT_SERIAL);
  AsciiSPrint (Status->LastCommand, sizeof (Status->LastCommand), "%a", mDev.Fastboot.LastCommand);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jFbServiceStep (
  IN  NP05J_FASTBOOT_PROTOCOL  *This,
  OUT NP05J_FASTBOOT_ACTION    *PendingAction
  )
{
  EFI_TPL  OldTpl;

  if ((This == NULL) || (PendingAction == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *PendingAction           = Np05jFbActionNone;
  mDev.AppServiceSeenTicks = 0U;
  mDev.RebootGraceTicks    = 0U;

  Np05jUsbSerialPoll (&mDev);
  mDev.AppServiceSeenTicks = 0U;

  if (Np05jFastbootFlashPending (&mDev.Fastboot) &&
      (mDev.FbTxCount == 0U) &&
      !mDev.FbTxPending &&
      !mDev.Fastboot.FlashExecuting)
  {
    Np05jFastbootFlashStep (&mDev.Fastboot);
    OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
    PumpFbTx (&mDev);
    ArmFbRx (&mDev);
    gBS->RestoreTPL (OldTpl);
  }

  if ((mDev.FbTxCount == 0U) && !mDev.FbTxPending) {
    if (mDev.Fastboot.RebootRequested) {
      mDev.Fastboot.RebootRequested = FALSE;
      *PendingAction                = Np05jFbActionReboot;
    } else if (mDev.Fastboot.ExitRequested) {
      mDev.Fastboot.ExitRequested = FALSE;
      *PendingAction              = Np05jFbActionContinue;
    } else if (mDev.PendingFbAction != Np05jFbActionNone) {
      *PendingAction       = mDev.PendingFbAction;
      mDev.PendingFbAction = Np05jFbActionNone;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jFbSetDiagnosticsOnly (
  IN NP05J_FASTBOOT_PROTOCOL  *This,
  IN BOOLEAN                  AllowReboot
  )
{
  EFI_STATUS  Status;
  EFI_TPL     OldTpl;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Np05jFastbootRestrictToDiagnostics (&mDev.Fastboot, AllowReboot);
  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  PumpFbTx (&mDev);
  ArmFbRx (&mDev);
  gBS->RestoreTPL (OldTpl);
  return Status;
}

//
// EFI_SERIAL_IO_PROTOCOL Implementation
//
STATIC
EFI_STATUS
EFIAPI
Np05jSerialReset (
  IN EFI_SERIAL_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jSerialSetAttributes (
  IN EFI_SERIAL_IO_PROTOCOL  *This,
  IN UINT64                  BaudRate,
  IN UINT32                  ReceiveFifoDepth,
  IN UINT32                  Timeout,
  IN EFI_PARITY_TYPE         Parity,
  IN UINT8                   DataBits,
  IN EFI_STOP_BITS_TYPE      StopBits
  )
{
  mDev.SerialMode.BaudRate         = (BaudRate == 0) ? 115200 : BaudRate;
  mDev.SerialMode.ReceiveFifoDepth = (ReceiveFifoDepth == 0) ? NP05J_RX_RING_SIZE : ReceiveFifoDepth;
  mDev.SerialMode.Timeout          = Timeout;
  mDev.SerialMode.Parity           = (Parity == DefaultParity) ? NoParity : Parity;
  mDev.SerialMode.DataBits         = (DataBits == 0) ? 8 : DataBits;
  mDev.SerialMode.StopBits         = (StopBits == DefaultStopBits) ? OneStopBit : StopBits;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jSerialSetControl (
  IN EFI_SERIAL_IO_PROTOCOL  *This,
  IN UINT32                  Control
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jSerialGetControl (
  IN  EFI_SERIAL_IO_PROTOCOL  *This,
  OUT UINT32                  *Control
  )
{
  EFI_TPL  OldTpl;
  UINT32   Bits;

  if (Control == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Np05jUsbSerialPoll (&mDev);

  Bits   = EFI_SERIAL_CLEAR_TO_SEND | EFI_SERIAL_DATA_SET_READY | EFI_SERIAL_CARRIER_DETECT |
           EFI_SERIAL_DATA_TERMINAL_READY | EFI_SERIAL_REQUEST_TO_SEND;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mDev.RxCount == 0) {
    Bits |= EFI_SERIAL_INPUT_BUFFER_EMPTY;
  }

  if (mDev.TxCount == 0) {
    Bits |= EFI_SERIAL_OUTPUT_BUFFER_EMPTY;
  }

  gBS->RestoreTPL (OldTpl);

  *Control = Bits;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jSerialWrite (
  IN     EFI_SERIAL_IO_PROTOCOL  *This,
  IN OUT UINTN                   *BufferSize,
  IN     VOID                    *Buffer
  )
{
  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize == 0) {
    return EFI_SUCCESS;
  }

  if (mDev.CompositeMode && mDev.HostReady) {
    TxRingPushBytes (&mDev, (CONST UINT8 *)Buffer, *BufferSize);
    Np05jUsbSerialPoll (&mDev);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jSerialRead (
  IN     EFI_SERIAL_IO_PROTOCOL  *This,
  IN OUT UINTN                   *BufferSize,
  OUT    VOID                    *Buffer
  )
{
  UINTN  ReadCount;

  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize == 0) {
    return EFI_SUCCESS;
  }

  Np05jUsbSerialPoll (&mDev);
  ReadCount   = RxRingPopBytes (&mDev, (UINT8 *)Buffer, *BufferSize);
  *BufferSize = ReadCount;

  return (ReadCount > 0) ? EFI_SUCCESS : EFI_TIMEOUT;
}

//
// Teardown helper shared by DriverBindingStop and ExitBootServices
//
STATIC
VOID
TeardownUsbfn (
  IN NP05J_USB_SERIAL_DEV  *Dev,
  IN BOOLEAN               CloseProtocol
  )
{
  EFI_USBFN_IO_PROTOCOL  *UsbfnIo;
  EFI_HANDLE             ControllerHandle;
  BOOLEAN                WasComposite;
  UINT8                  FbEp;

  if (Dev->PollTimer != NULL) {
    gBS->SetTimer (Dev->PollTimer, TimerCancel, 0);
  }

  if (Dev->DeferredConnectTimer != NULL) {
    gBS->SetTimer (Dev->DeferredConnectTimer, TimerCancel, 0);
  }

  UsbfnIo          = Dev->UsbfnIo;
  ControllerHandle = Dev->UsbfnControllerHandle;
  WasComposite     = Dev->CompositeMode;
  FbEp             = Dev->FbEpIndex;

  Dev->UsbfnIo               = NULL;
  Dev->UsbfnControllerHandle = NULL;
  Dev->Attached              = FALSE;
  ResetBusState (Dev);

  if (UsbfnIo != NULL) {
    UsbfnIo->AbortTransfer (UsbfnIo, NP05J_EP_CONTROL, EfiUsbEndpointDirectionDeviceTx);
    UsbfnIo->AbortTransfer (UsbfnIo, NP05J_EP_CONTROL, EfiUsbEndpointDirectionDeviceRx);
    if (WasComposite) {
      UsbfnIo->AbortTransfer (UsbfnIo, NP05J_EP_BULK_INDEX, EfiUsbEndpointDirectionDeviceRx);
      UsbfnIo->AbortTransfer (UsbfnIo, NP05J_EP_BULK_INDEX, EfiUsbEndpointDirectionDeviceTx);
    }

    if (FbEp != 0U) {
      UsbfnIo->AbortTransfer (UsbfnIo, FbEp, EfiUsbEndpointDirectionDeviceRx);
      UsbfnIo->AbortTransfer (UsbfnIo, FbEp, EfiUsbEndpointDirectionDeviceTx);
    }

    UsbfnIo->StopController (UsbfnIo);

    if (Dev->Ep0Buf != NULL) {
      UsbfnIo->FreeTransferBuffer (UsbfnIo, Dev->Ep0Buf);
      Dev->Ep0Buf = NULL;
    }

    if (Dev->BulkRxBuf != NULL) {
      UsbfnIo->FreeTransferBuffer (UsbfnIo, Dev->BulkRxBuf);
      Dev->BulkRxBuf = NULL;
    }

    if (Dev->BulkTxBuf != NULL) {
      UsbfnIo->FreeTransferBuffer (UsbfnIo, Dev->BulkTxBuf);
      Dev->BulkTxBuf = NULL;
    }

    if (Dev->FbRxBuf != NULL) {
      UsbfnIo->FreeTransferBuffer (UsbfnIo, Dev->FbRxBuf);
      Dev->FbRxBuf = NULL;
    }

    if (Dev->FbTxBuf != NULL) {
      UsbfnIo->FreeTransferBuffer (UsbfnIo, Dev->FbTxBuf);
      Dev->FbTxBuf = NULL;
    }

    if (CloseProtocol && (ControllerHandle != NULL)) {
      gBS->CloseProtocol (
             ControllerHandle,
             &gEfiUsbFunctionIoProtocolGuid,
             mDriverBinding.DriverBindingHandle,
             ControllerHandle
             );
    }
  }
}

//
// EFI_DRIVER_BINDING_PROTOCOL Implementation (binds to UsbfnDwc3Dxe)
//
STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS             Status;
  EFI_USBFN_IO_PROTOCOL  *UsbfnIo;

  if (mDev.UsbfnIo != NULL) {
    return (ControllerHandle == mDev.UsbfnControllerHandle) ? EFI_ALREADY_STARTED : EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiUsbFunctionIoProtocolGuid,
                  (VOID **)&UsbfnIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiUsbFunctionIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ConfigureEndpointsForMode (
  IN EFI_USBFN_IO_PROTOCOL  *UsbfnIo,
  IN BOOLEAN                Composite
  )
{
  NP05J_USBFN_IO_PROTOCOL_EX  *UsbfnIoEx;

  if (Composite) {
    if (UsbfnIo->Revision >= NP05J_USBFN_IO_REV_SS) {
      UsbfnIoEx = (NP05J_USBFN_IO_PROTOCOL_EX *)UsbfnIo;
      return UsbfnIoEx->ConfigureEnableEndpointsEx (UsbfnIo, &mCompHsDeviceInfo, &mCompSsDeviceInfo);
    }

    return UsbfnIo->ConfigureEnableEndpoints (UsbfnIo, &mCompHsDeviceInfo);
  }

  if (UsbfnIo->Revision >= NP05J_USBFN_IO_REV_SS) {
    UsbfnIoEx = (NP05J_USBFN_IO_PROTOCOL_EX *)UsbfnIo;
    return UsbfnIoEx->ConfigureEnableEndpointsEx (UsbfnIo, &mFbHsDeviceInfo, &mFbSsDeviceInfo);
  }

  return UsbfnIo->ConfigureEnableEndpoints (UsbfnIo, &mFbHsDeviceInfo);
}

STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS             Status;
  EFI_USBFN_IO_PROTOCOL  *UsbfnIo;
  NP05J_BIOS_CONFIG      Cfg;
  UINTN                  MaxXfer;

  if (mDev.ExitedBootServices) {
    return EFI_UNSUPPORTED;
  }

  if (mDev.UsbfnIo != NULL) {
    return (ControllerHandle == mDev.UsbfnControllerHandle) ? EFI_ALREADY_STARTED : EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiUsbFunctionIoProtocolGuid,
                  (VOID **)&UsbfnIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  GetLiveBiosConfig (&Cfg);
  mDev.CompositeMode         = (Cfg.UsbMode != NP05J_USB_FASTBOOT);
  mDev.FbEpIndex             = mDev.CompositeMode ? NP05J_EP_FB_COMP_INDEX : NP05J_EP_FB_PURE_INDEX;
  mDev.UsbfnIo               = UsbfnIo;
  mDev.UsbfnControllerHandle = ControllerHandle;
  mDev.BusSpeed              = UsbBusSpeedHigh;
  mDev.BulkMaxPacket         = 512;
  ResetBusState (&mDev);

  UsbfnIo->StopController (UsbfnIo);
  Status = UsbfnIo->StartController (UsbfnIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Np05jUsbSerial: StartController failed: %r\n", Status));
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  MaxXfer = NP05J_FB_BUF_MAX_SIZE;
  if ((UsbfnIo->GetMaxTransferSize != NULL) &&
      !EFI_ERROR (UsbfnIo->GetMaxTransferSize (UsbfnIo, &MaxXfer)) &&
      (MaxXfer >= 4096U))
  {
    mDev.FbBufSize = MIN ((UINTN)NP05J_FB_BUF_MAX_SIZE, (MaxXfer / 1024U) * 1024U);
  } else {
    mDev.FbBufSize = NP05J_FB_BUF_MAX_SIZE;
  }

  if (mDev.FbBufSize < 4096U) {
    mDev.FbBufSize = 4096U;
  }

  Status = UsbfnIo->AllocateTransferBuffer (UsbfnIo, NP05J_EP0_BUF_SIZE, &mDev.Ep0Buf);
  if (EFI_ERROR (Status)) {
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  Status = UsbfnIo->AllocateTransferBuffer (UsbfnIo, mDev.FbBufSize, &mDev.FbRxBuf);
  if (EFI_ERROR (Status)) {
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  Status = UsbfnIo->AllocateTransferBuffer (UsbfnIo, mDev.FbBufSize, &mDev.FbTxBuf);
  if (EFI_ERROR (Status)) {
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  if (mDev.CompositeMode) {
    Status = UsbfnIo->AllocateTransferBuffer (UsbfnIo, NP05J_BULK_RX_BUF_SIZE, &mDev.BulkRxBuf);
    if (EFI_ERROR (Status)) {
      TeardownUsbfn (&mDev, TRUE);
      return Status;
    }

    Status = UsbfnIo->AllocateTransferBuffer (UsbfnIo, NP05J_BULK_TX_BUF_SIZE, &mDev.BulkTxBuf);
    if (EFI_ERROR (Status)) {
      TeardownUsbfn (&mDev, TRUE);
      return Status;
    }

    Status = ConfigureEndpointsForMode (UsbfnIo, TRUE);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "Np05jUsbSerial: Composite ConfigureEnableEndpoints(Ex) failed (%r); falling back to Pure Fastboot\n",
        Status
        ));
      mDev.CompositeMode = FALSE;
      mDev.FbEpIndex     = NP05J_EP_FB_PURE_INDEX;
      UsbfnIo->StopController (UsbfnIo);
      Status = UsbfnIo->StartController (UsbfnIo);
      if (EFI_ERROR (Status)) {
        TeardownUsbfn (&mDev, TRUE);
        return Status;
      }

      Status = ConfigureEndpointsForMode (UsbfnIo, FALSE);
    }
  } else {
    Status = ConfigureEndpointsForMode (UsbfnIo, FALSE);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Np05jUsbSerial: ConfigureEnableEndpoints(Ex) failed: %r\n", Status));
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  Status = gBS->SetTimer (mDev.PollTimer, TimerPeriodic, NP05J_POLL_INTERVAL_100NS);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Np05jUsbSerial: SetTimer failed: %r\n", Status));
    TeardownUsbfn (&mDev, TRUE);
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "Np05jUsbSerial: %a ready (FbBuf=%uB, UsbfnIo rev 0x%08x)\n",
    mDev.CompositeMode ? "Composite (CDC-ACM + SunUEFI Fastboot)" : "Pure SunUEFI Fastboot",
    (UINT32)mDev.FbBufSize,
    UsbfnIo->Revision
    ));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Np05jUsbSerialBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  if (ControllerHandle != mDev.UsbfnControllerHandle) {
    return EFI_SUCCESS;
  }

  TeardownUsbfn (&mDev, TRUE);
  return EFI_SUCCESS;
}

//
// Protocol & Event Notifications
//
STATIC
VOID
TryConnectExistingUsbfnHandles (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       HandleCount;
  EFI_HANDLE  *Handles;

  if (mDev.UsbfnIo != NULL) {
    return;
  }

  Handles     = NULL;
  HandleCount = 0;
  Status      = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gEfiUsbFunctionIoProtocolGuid,
                       NULL,
                       &HandleCount,
                       &Handles
                       );
  if (EFI_ERROR (Status) || (Handles == NULL)) {
    return;
  }

  for (UINTN Index = 0; Index < HandleCount; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    if (mDev.UsbfnIo != NULL) {
      break;
    }
  }

  FreePool (Handles);
}

STATIC
VOID
EFIAPI
OnDeferredConnectTimer (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (!mDev.ExitedBootServices) {
    TryConnectExistingUsbfnHandles ();
  }
}

STATIC
VOID
EFIAPI
OnUsbfnProtocolInstalled (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if ((mDev.UsbfnIo == NULL) && !mDev.ExitedBootServices && (mDev.DeferredConnectTimer != NULL)) {
    gBS->SetTimer (mDev.DeferredConnectTimer, TimerRelative, 500000ULL);
  }
}

STATIC
VOID
EFIAPI
OnPartitionInfoInstalled (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;
  Np05jFastbootStorageMarkDirty ();
}

STATIC
VOID
EFIAPI
OnEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&mTerminalDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&mTerminalDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&mTerminalDevicePath, NULL);

  Np05jFastbootStorageMarkDirty ();
  TryConnectExistingUsbfnHandles ();
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mDev.ExitedBootServices = TRUE;
  TeardownUsbfn (&mDev, FALSE);
  Np05jFastbootStorageStop ();
}

//
// Driver Entry Point
//
EFI_STATUS
EFIAPI
Np05jUsbSerialDriverEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS         Status;
  NP05J_BIOS_CONFIG  Cfg;

  ZeroMem (&mDev, sizeof (mDev));
  GetLiveBiosConfig (&Cfg);

  mDev.ImageHandle   = ImageHandle;
  mDev.CompositeMode = (Cfg.UsbMode != NP05J_USB_FASTBOOT);
  mDev.FbEpIndex     = mDev.CompositeMode ? NP05J_EP_FB_COMP_INDEX : NP05J_EP_FB_PURE_INDEX;
  mDev.BusSpeed      = UsbBusSpeedHigh;
  mDev.BulkMaxPacket = 512;

  // Default CDC-ACM line coding: 115200 8N1
  mDev.LineCoding.DteRate    = 115200;
  mDev.LineCoding.CharFormat = 0;
  mDev.LineCoding.ParityType = 0;
  mDev.LineCoding.DataBits   = 8;

  // Default EFI_SERIAL_IO_MODE
  mDev.SerialMode.ControlMask      = EFI_SERIAL_CLEAR_TO_SEND |
                                     EFI_SERIAL_DATA_SET_READY |
                                     EFI_SERIAL_CARRIER_DETECT |
                                     EFI_SERIAL_INPUT_BUFFER_EMPTY |
                                     EFI_SERIAL_OUTPUT_BUFFER_EMPTY |
                                     EFI_SERIAL_DATA_TERMINAL_READY |
                                     EFI_SERIAL_REQUEST_TO_SEND;
  mDev.SerialMode.Timeout          = 1000000;
  mDev.SerialMode.BaudRate         = 115200;
  mDev.SerialMode.ReceiveFifoDepth = NP05J_RX_RING_SIZE;
  mDev.SerialMode.DataBits         = 8;
  mDev.SerialMode.Parity           = NoParity;
  mDev.SerialMode.StopBits         = OneStopBit;

  mDev.SerialIo.Revision       = EFI_SERIAL_IO_PROTOCOL_REVISION;
  mDev.SerialIo.Reset          = Np05jSerialReset;
  mDev.SerialIo.SetAttributes  = Np05jSerialSetAttributes;
  mDev.SerialIo.SetControl     = Np05jSerialSetControl;
  mDev.SerialIo.GetControl     = Np05jSerialGetControl;
  mDev.SerialIo.Write          = Np05jSerialWrite;
  mDev.SerialIo.Read           = Np05jSerialRead;
  mDev.SerialIo.Mode           = &mDev.SerialMode;
  mDev.SerialIo.DeviceTypeGuid = NULL;

  mDev.FastbootProtocol.Revision           = 1U;
  mDev.FastbootProtocol.GetStatus          = Np05jFbGetStatus;
  mDev.FastbootProtocol.ServiceStep        = Np05jFbServiceStep;
  mDev.FastbootProtocol.SetDiagnosticsOnly = Np05jFbSetDiagnosticsOnly;

  LocateLogRing (&mDev);
  Np05jFastbootInit (&mDev.Fastboot, &mDev, FbSendCallback, NULL);
  mDev.Fastboot.Query      = FbQueryCallback;
  mDev.Fastboot.Diagnostic = FbDiagnosticCallback;
  if (!EFI_ERROR (Np05jFastbootStorageInit ())) {
    Np05jFastbootSetStorage (&mDev.Fastboot, Np05jFastbootStorageGet ());
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  Np05jUsbSerialTimerCb,
                  &mDev,
                  &mDev.PollTimer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnDeferredConnectTimer,
                  &mDev,
                  &mDev.DeferredConnectTimer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnEndOfDxe,
                  &mDev,
                  &gEfiEndOfDxeEventGroupGuid,
                  &mDev.EndOfDxeEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Np05jUsbSerial: EndOfDxe event registration failed: %r\n", Status));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  OnExitBootServices,
                  &mDev,
                  &gEfiEventExitBootServicesGuid,
                  &mDev.ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Np05jUsbSerial: ExitBootServices event registration failed: %r\n", Status));
  }

  // Install persistent SerialIo + DevicePath + FastbootProtocol handle
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mDev.SerialHandle,
                  &gEfiSerialIoProtocolGuid,
                  &mDev.SerialIo,
                  &gEfiDevicePathProtocolGuid,
                  &mSerialDevicePath,
                  &gNp05jFastbootProtocolGuid,
                  &mDev.FastbootProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Install UEFI Driver Binding protocol to bind onto UsbfnDwc3Dxe
  mDriverBinding.ImageHandle         = ImageHandle;
  mDriverBinding.DriverBindingHandle = ImageHandle;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiDriverBindingProtocolGuid,
                  &mDriverBinding,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mDev.UsbfnNotifyEvent = EfiCreateProtocolNotifyEvent (
                            &gEfiUsbFunctionIoProtocolGuid,
                            TPL_CALLBACK,
                            OnUsbfnProtocolInstalled,
                            &mDev,
                            &mDev.UsbfnNotifyReg
                            );

  mDev.PartNotifyEvent = EfiCreateProtocolNotifyEvent (
                           &gEfiPartitionInfoProtocolGuid,
                           TPL_CALLBACK,
                           OnPartitionInfoInstalled,
                           &mDev,
                           &mDev.PartNotifyReg
                           );

  TryConnectExistingUsbfnHandles ();

  DEBUG ((DEBUG_INFO, "Np05jUsbSerial: Driver initialized (SerialHandle=%p, Fastboot=SunUEFI-np05j)\n", mDev.SerialHandle));
  return EFI_SUCCESS;
}
