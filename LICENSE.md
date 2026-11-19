# License Scope Notice

This repository follows the existing file-level licensing declared in each source file.

## Existing Declarations

| License | Verified Representative Files | Scope |
| --- | --- | --- |
| BSD-2-Clause-Patent | [PianoProductCore.c](uefi/core/PianoProductCore.c), [PianoEarlyMemory.c](uefi/handoff/early-memory/PianoEarlyMemory.c) | Applies to files carrying this notice; does not cover all UEFI code. |
| GPL-2.0-only | [PianoKeys.c](uefi/core/PianoKeys.c), [PianoPmicMetadata.c](uefi/core/PianoPmicMetadata.c) | Preserves existing identifiers and reference attributions. |
| GPL-2.0-or-later | [PianoTouchProbe.c](uefi/core/PianoTouchProbe.c) | Preserves existing identifiers; tracked separately from GPL-2.0-only. |
| LGPL-3.0-or-later | [simple-init runtime](uefi/components/product-pump/simple-init/src/gui/piano_product_runtime.c) and its header files | Applies to these files; does not cover the entire firmware or all tools. |
| BSD-3-Clause | [Linux DT overlays](linux/dts/piano-linux-owned-dma.dtso) and other files with the same identifier | Governed by each file's own declaration. |
| Original file copyright and distribution terms | [AOSP Font.h](uefi/platforms/pianoProbePkg/Library/RamLogSerialPortLib/Font.h) | Original header notice is retained and not superseded by this document. |

The table above lists representative files; all other files are governed by their respective SPDX identifiers, copyright notices, and origin statements. Full license texts can be looked up by ID in the [SPDX License List](https://spdx.org/licenses/). For third-party and binary materials, see [THIRD_PARTY.md](THIRD_PARTY.md).
