# Piano Display Calibration

The two calibration blobs match the stock BOE and CSOT panels respectively;
source filenames, ROM version, and SHA256 hashes are recorded in
`manifest.json`. They contain no per-unit identifiers or user data.

The driver converts standard PQ or Gamma 2.2 signals to linear light via IGCv5,
applies BT.2020-to-P3-D65 conversion and stock panel calibration via Gamut
v4.3, and outputs panel-encoded values via GCv2. GC preserves the 1024 main
samples and 256 dark-region samples from stock `native_hdr`; the 3D LUT
combines the stock 17³ calibration table with the standard linear BT.2020 →
P3-D65 transform. Peak and black-level values in the files are stock
configuration values, not per-unit photometric measurements.

Regenerate from stock QDCM files of the same ROM version:

```sh
./build.sh display-calibration \
  --boe-source /path/to/qdcm_calib_data_xiaomi_p81_42_02_0a_vid_mode_dualdsi_dsc_panel.json \
  --csot-source /path/to/qdcm_calib_data_xiaomi_p81_35_02_0b_vid_mode_dualdsi_dsc_panel.json \
  --rom OS4.0.6.0.XPYCNXM --output build/display-calibration
```

The binary format consists of a 32-byte little-endian header, two 1025-entry
uniform Degamma LUTs, three-channel packed GC tables, and a 17³ RGB LUT with
the red index varying fastest. The header stores the format version, panel ID,
luminance in millinits, and total length. An empty configuration or validation
failure does not affect standard SDR output; the driver simply does not expose
HDR.

The stock calibration data originates from proprietary Qualcomm/Xiaomi
configurations, and its rights are unaffected by repacking. The source license
of this project's generator does not cover the stock calibration data.
