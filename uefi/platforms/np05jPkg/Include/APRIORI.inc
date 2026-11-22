APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF QcomPkg/Drivers/IortDtDxe/IortDtDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/EnvDxe/EnvDxeEnhanced.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/MailboxDxe/MailboxDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/QcomScmiDxe/QcomScmiDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
!if $(TARGET) == DEBUG AND $(NP05J_NX729J_DXE) == 1
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ClockDxe/ClockDxeNx729j.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
!endif
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/QupDxe/QupDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
!if $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 12
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV12.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 11
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV11.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 10
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV10.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 9
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV9.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 8
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV8.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 7
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV7.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 6
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV6.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 5
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV5.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 4
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV4.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 3
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnosticV3.inf
!elseif $(TARGET) == DEBUG AND $(NP05J_SPMI_DEBUGGER) == 2
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxeDiagnostic.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
!endif
!if $(TARGET) == DEBUG AND ($(NP05J_SPMI_DEBUGGER) >= 1 AND $(NP05J_SPMI_DEBUGGER) <= 12)
  INF nubia_np05jPkg/Drivers/Np05jSpmiDebuggerDxe/Np05jSpmiDebuggerDxe.inf
!endif

  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf

!if $(TARGET) == DEBUG AND $(NP05J_NX729J_DXE) == 1
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PmicDxe/PmicDxeLaNx729j.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
!endif

  INF Binaries/nubia_np05j/MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF Binaries/nubia_np05j/MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

!if $(TARGET) == DEBUG AND $(NP05J_NX729J_DXE) == 1
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UFSDxe/UFSDxeNx729j.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
!endif

  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/FeatureEnablerDxe/FeatureEnablerDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PILDxe/PILDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
  #INF Binaries/nubia_np05j/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF Binaries/nubia_np05j/MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/SPIDxe/SPIDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
!if $(TARGET) == DEBUG AND $(NP05J_NX729J_DXE) == 1
  INF Binaries/nubia_np05j/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLANx729j.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
!endif
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/XhciDxe/XhciDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF nubia_np05jPkg/Drivers/Np05jUsbSerialDxe/Np05jUsbSerialDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
!if $(TARGET) == DEBUG AND ($(NP05J_BUTTONS_ENTER) == 1 OR $(NP05J_NX729J_DXE) == 1)
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ButtonsDxe/ButtonsDxeEnter.inf
!else
  INF Binaries/nubia_np05j/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
!endif
  INF Binaries/nubia_np05j/QcomPkg/Drivers/TsensDxe/TsensDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/nubia_np05j/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/CPRDxe/CPRDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif
  #INF Binaries/nubia_np05j/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/UCDxe/UCDxe.inf
  INF Binaries/nubia_np05j/QcomPkg/Drivers/RNGDxe/RngDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF nubia_np05jPkg/Drivers/Np05jGraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf

  INF Binaries/nubia_np05j/QcomPkg/Drivers/RscDxe/RscDxe.inf
}
