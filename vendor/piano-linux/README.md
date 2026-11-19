# Piano Linux Board Inputs

`board.dtb` is the recorded release input of the adapted Device Tree for the BOE/16GB platform. Transient properties such as boot arguments, serial numbers, and random seeds have been stripped, as recorded in `manifest.json`. It retains kernel-managed fixes for DMA, clocks, DSP/PCIe, and keyboard providers; it is not an unadapted Android DTB.

It is derived board material and does not alter the license of its stock source. It pairs with the source tree in `patches/linux`; new packaging and installations still require actual verification. When modifying hardware bindings, rebuild the file and record its provenance rather than ignoring digest checks.
