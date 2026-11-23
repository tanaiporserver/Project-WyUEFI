# SPDX-License-Identifier: BSD-3-Clause
dnl AudioReach topology สำหรับ nubia REDMAGIC Astra (NP05J, SM8750P)
dnl แอมป์สเตอริโอคู่ NXP TFA9874 (@0x34/@0x35) บน Primary MI2S (I2S0 SD0, 48 kHz, 2ch, S32_LE)
include(`audioreach/audioreach.m4')
include(`audioreach/stream-subgraph.m4')
include(`audioreach/device-subgraph.m4')
include(`util/route.m4')
include(`util/mixer.m4')
include(`audioreach/tokens.m4')

dnl MultiMedia1 Playback (สเตอริโอ 48 kHz)
STREAM_SG_PCM_ADD(audioreach/subgraph-stream-vol-playback.m4, FRONTEND_DAI_MULTIMEDIA1,
	`S16_LE', 48000, 48000, 1, 2,
	0x00004001, 0x00004001, 0x00006001, `110000')

dnl Primary MI2S Playback -> ลำโพงคู่ NXP TFA9874 (2 x 32-bit slots = 64 BCLK/fs, 3.072 MHz)
DEVICE_SG_ADD(subgraph-device-i2s-mfc-playback.m4, `Primary', PRIMARY_MI2S_RX,
	`S32_LE', 48000, 48000, 2, 2,
	LPAIF_INTF_TYPE_LPAIF, I2S_INTF_TYPE_PRIMARY, SD_LINE_IDX_I2S_SD0, DATA_FORMAT_FIXED_POINT,
	0x00004006, 0x00004006, 0x00006060, `PRIMARY_MI2S_RX')

STREAM_DEVICE_PLAYBACK_MIXER(PRIMARY_MI2S_RX, ``PRIMARY_MI2S_RX'', ``MultiMedia1'')
STREAM_DEVICE_PLAYBACK_ROUTE(PRIMARY_MI2S_RX, ``PRIMARY_MI2S_RX Audio Mixer'', ``MultiMedia1, stream0.logger1'')
