# Piano Goodix — HyperOS 4

`gfpia.elf` is the complete signed executable reconstructed from `gfpia.mdt`
and `gfpia.b00..b08` on the `modem` partition of the official full OTA
`OS4.0.6.0.XPYCNXM` according to the ELF program header file offsets. Original
segment bytes are preserved unmodified, with no changes to code or signatures.

It is loaded by Qualcomm QTEE to communicate with the Goodix fingerprint
sensor. The file contains no enrolled user fingerprint templates, unlock
credentials, or runtime authentication data. Provenance, size, and SHA256 are
recorded in `manifest.json`.

The three `piano-goodix-*.bin` files are generic model parameters extracted
from `/odm/lib64/libgf_hal.so` in the same OTA version: the CAP S/T
anti-spoofing models and the Dubai U4 model used by chip type 49. They are
extracted using the actual file offsets of the exported ELF symbols with
original bytes preserved; the extracted symbol definitions live in
`tools/firmware/import_ota.py`. `manifest.json` records output sizes and SHA256
hashes, without recording the source library digest or per-symbol offsets.
These files contain no user data and no executable code from the stock library.

The native client selects the S or T model based on the flag returned by the
device, uploads it in chunks to the trusted application via command 1113, and
then issues the initialization-complete command. Communication and power
control are handled through standard QTEE and UIO interfaces, with no need to
run Android fingerprint services on Linux.

This signed binary and these model parameters are not original source code of
this project; neither this project's license nor the public QTEE client's
source license grants redistribution rights for them. The existing manifest and
OTA provenance establish file identity only—this directory contains no
redistribution license text or links covering `gfpia.elf` or the three models.
Rights-holder licensing terms must be verified separately before distribution;
nothing here asserts that redistribution is permitted or necessarily
prohibited.
