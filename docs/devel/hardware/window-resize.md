# GNOME Window Resize Performance

The display currently scans out at 144 Hz, yet continuous resizing of some GTK windows commits frames at only ~30 times/s. GNOME Overview animations do not require applications to repeatedly relayout and redraw, so their smoothness cannot be used to infer regular window resize performance.

## Scoped Findings

On 2026-10-09, automated resizing of Sound Settings was run for ~6 seconds on the current Debian GNOME build. The application is a native Wayland client on an output advertising 3200×2136 at 143999 mHz. Wayland protocol logging was enabled only for this process, with no other processes attached and no repeated GPU floor-frequency testing.

| Metric | Result | What It Shows |
| --- | --- | --- |
| Resize `configure` and `ack` | 406 pairs (~68.4/s); avg ack ~0.006 ms | The test is not throttled on size acknowledgments |
| Frame commits with new buffer | 193 (~32.5/s) | Client frame commits are significantly slower than size notifications |
| Adjacent frame commits | Avg ~30.8 ms | Consistent with the observed ~30 fps |
| Previous frame callback to next frame request | Avg ~14.5 ms | Includes client frame scheduling, layout, drawing, and buffer prep; needs further breakdown |
| Frame commit to frame callback received | Avg ~16.2 ms | Includes compositor, IPC transport, and client handling; cannot all be attributed to compositor time |

Counts include only commits following a non-null buffer attach, excluding state-only commits and `wl_display.sync` callbacks. Server-side frame callback timestamp intervals match client-side records, showing no continuously growing event backlog, though fixed latency remains unknown; this does not establish a hard 60 Hz or 30 Hz cap.

The 193 frame commits correspond to 193 newly allocated buffers, with every consecutive commit changing size. This points follow-up investigation toward resize layout, drawing, and the EGL buffer path; buffer allocation time has not yet been measured, so correlation must not be treated as root cause.

The interval from callback to the next frame request also includes GTK frame-clock wait time and does not mean the CPU spent 14.5 ms continuously busy. Current data is still insufficient to separate compute, wait, and allocation shares.

## Source References and Next Steps

Current Mutter 48.7 coalesces the latest resize position before redrawing; the Wayland path has no rule waiting for the previous `configure` to be acknowledged before sending the next. [GTK 4.18.6](https://github.com/GNOME/gtk/tree/4.18.6/gdk/wayland) acknowledges resize notifications and waits for the `wl_surface.frame` callback after drawing before continuing updates. Normal window resizing also requires the client to commit a new buffer before taking effect.

The next step is to use GTK's standard frame-clock/renderer profiling to break down the ~14.5 ms client-side phase before deciding whether kernel, Mesa, GTK, or application changes are needed. Do not start by skipping frame waits, forcing vsync off, or guessing about touch polling rates. See the [official GTK debugging and profiling reference](https://docs.gtk.org/gtk4/running.html#profiling).

Local protocol logs, screenshots, automated control recordings, and frame-by-frame correlations are stored in the internal records directory.
