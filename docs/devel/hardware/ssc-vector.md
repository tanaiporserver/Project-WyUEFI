# Raw SSC Reports and Vector Decoding

The libssc extension reuses existing SUID discovery, QMI transport, and open/close flows to expose raw reports and vector decoding for verified layouts. `piano-ssc-vector` is a time-bounded diagnostic client; it does not autostart as a service and does not create IIO devices.

The shared patch is wired into `./build.sh sensors` via `sensors.patches` in `config/release.json` and built through the standard Debian quilt/Meson workflow. `0003-libssc-raw-vector-reports.patch` provides the reporting interface and client; see [Build and Package Sources](sensors.md#build-and-package-sources) for sid package versions.

## Timed Raw Report Capture

Subscribe using the exact SSC data type; capture defaults to 3 seconds, and `--seconds` accepts 1–30 seconds:

```sh
piano-ssc-vector --sensor cct_front --seconds 3
piano-ssc-vector --sensor ambient_light_back --seconds 3
piano-ssc-vector --sensor flicker --seconds 3
```

The client uses the existing `SSCSensor` open/close flow and supports continuous and on-change subscriptions. Other stream types return an unsupported error.

`SSCSensor::raw-report(sensor, guint message_id, GBytes *payload)` forwards only raw reports matching the object's SUID. `GBytes` is borrowed for the duration of the callback; call `g_bytes_ref()` to retain it longer. The signal runs on the existing report dispatch context, so GUI applications must marshal handling to their own main loop. When nothing is connected to this signal, the payload is not copied, preserving compatibility with existing strongly typed measurement signals and sampling policies.

Output includes the raw message ID, full payload hex, and decoded fields for supported layouts. Unknown layouts are emitted in raw form without guessing physical units for their components.

On Ctrl-C or timeout expiration, the client invokes the existing close API and prints `closed=true` only after receiving the close response. Discovery or open timeouts cancel the operation and wait up to 5 seconds for shutdown handling; late discovery results arriving after stop do not trigger an open. The tool never writes to the registry or runs calibration.

## Offline Vector Decoding

`ssc_sensor_decode_vector_report()` returns a caller-owned `GVariant a{sv}`, currently decoding verified layouts from front color temperature, rear ambient light, and flicker reports:

| Raw Event ID | Returned Fields | Semantic Boundary |
| --- | --- | --- |
| 1025 | `values: ad`, `accuracy: i` | All float32 values from field1, raw int32 from field2 |
| 769 | `field1: i`, `field2: i`, `field3: i`, `values: ad` | Three raw int32 values from flicker report, all float32 values from field4 |

float32 values are widened to double without altering their numeric values; the original bit patterns remain in `GBytes`. The three integers in event 769 are named strictly by field number without inferring frequency, accuracy, or physical units. Unknown IDs return `G_IO_ERROR_NOT_SUPPORTED`, malformed or truncated supported layouts return `G_IO_ERROR_INVALID_DATA`, and the raw payload can always be saved independently.

```sh
piano-ssc-vector --decode 1025 --payload-hex 0a04000000001003
```

To compile the same client manually from the repository, link against the development library built with the patch above:

```sh
cc -O2 -Wall -Wextra linux/userspace/piano-ssc-vector.c \
  -o piano-ssc-vector $(pkg-config --cflags --libs libssc gio-unix-2.0)
```
