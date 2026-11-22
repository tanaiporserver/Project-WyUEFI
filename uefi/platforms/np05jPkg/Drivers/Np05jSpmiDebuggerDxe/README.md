# NP05J SPMI boot debugger

Build the 24 GiB NP05J diagnostic image from the repository root with
`NP05J_SPMI_DEBUGGER=1 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The Android boot image is `Mu-nubia_np05j.img`. Copy it to a distinct name
when retaining an earlier build.

Record the early display from power-on. The debugger runs in Mu after the
slot B SPMI provider initializes its cache and before PMIC initialization.
The banner is `NP05J SPMI DEBUGGER v1`. The capture window lasts two seconds.
If the firmware already supplies a working text input console, press **P**
for a ten-second pause, **R** to read the cache again, or **C** to continue.
Automatic capture works before keyboard initialization.

For bus 0, PPID 0x74F, capture every candidate, the requesting EE, the
predicted effective APID, write owner and computed RW status address. Values
are hexadecimal. The debugger bounds its reads to the loaded provider
image, checks the NP05J layout and walks the PID chain in stock lookup order.
The final line reports any lookup or layout error. Normal DXE dispatch resumes
after the capture window, including on a diagnostic error.

Compare the prediction against the FAR and ELR of a subsequent exception in
the same recording. An `allow` result describes the cached owner check.
The existing exception handler records what happened on the actual load.
This diagnostic observes Mu's boot after handoff. A comparison with the
earlier stock DXE still needs a stock trace.

## Diagnostic write filter (v2)

Build with `NP05J_SPMI_DEBUGGER=2 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The banner `NP05J SPMI DEBUGGER v2 - 7:4F44 write filter armed` identifies the
loaded variant. The snapshot's `owner-guard=allow` describes the channel cache.
After lookup, the v2 transfer filter checks for the observed single-byte write
on bus 0 from EE 0 to SID 7, register 0x4F44, effective APID 0x2C7.
On a match it requests the log line `NP05J SPMI v2: deny 7:4F44 APID=2C7 result=6`
and returns the native denial before touching the RW window.

Record from the banner through the first subsequent DXE driver or exception.
If another transfer faults, capture the register page with ELR, ESR and FAR
as well as the module bases from that boot.

## Second observed channel (v3)

Use `NP05J_SPMI_DEBUGGER=3 python3 build_uefi.py -d nubia_np05j -r DEBUG`
to capture both PPIDs, 0x74F and 0x73E, before PmicDxe starts. In this
build the filter also returns status 6 for a one-byte opcode-0 write on
bus 0, EE 0 to SID 7, register 0x3E46, effective APID 0x2C6. The v2
hardware trace reached that channel after the first denial. Record the
two v3 denial messages and the first driver or exception that follows.

## Fifth observed write (v6)

Build with `NP05J_SPMI_DEBUGGER=6 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
During PMIC initialization v6 retains the four v5 denials and checks the
one-byte write to register 0x2650 on bus 0, EE 0, SID 7, APID 0x2BD.
Record the line `v6 deny 7:2650/2BD=6` and the next DXE driver or exception.

## Sixth observed write (v7)

Build with `NP05J_SPMI_DEBUGGER=7 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The v6 hardware capture shows a masked write to 7:2654 after the denial for
7:2650. The v7 filter returns status 6 for that additional one-byte write
on bus 0, EE 0, APID 0x2BD. Record `v7 7:2654/2BD=6` and the next driver
or exception.

## Seventh observed write (v8)

Build with `NP05J_SPMI_DEBUGGER=8 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The snapshot now includes PPID 0x729. Capture its candidates and selected
write owner, the denial `v8 7:2950/2C0=6`, and the next driver or exception.

## Eighth observed write (v9)

Build with `NP05J_SPMI_DEBUGGER=9 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The v8 recording confirmed the denial at 7:2950 and showed the next masked
write at 7:2951 on APID 0x2C0. Capture `v9 7:2951/2C0=6` and the next
driver or exception.

## Ninth observed write (v10)

Build with `NP05J_SPMI_DEBUGGER=10 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The v9 trace reaches a direct one-byte write to 7:2952 on APID 0x2C0.
Capture `v10 7:2952/2C0=6` and the next driver or exception.

## Tenth observed write (v11)

Build with `NP05J_SPMI_DEBUGGER=11 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The v10 capture shows a masked write to 7:2960 on APID 0x2C0 after the
denial at 7:2952. Capture `v11 7:2960/2C0=6` and the next driver or exception.

## Live write-owner check (v12)

Build with `NP05J_SPMI_DEBUGGER=12 python3 build_uefi.py -d nubia_np05j -r DEBUG`.
The v12 debugger adds a cache snapshot for PPID `0x10B` before PmicDxe dispatch.
The SPMI driver checks the current owner using the configuration word used by
the original cache builder. The check runs after cached ownership passes and
before the write path accesses the RW channel. A mismatch returns status 6.
Reads through the observer use the stock path. `cache-guard` reports the
cache verdict at snapshot time. Hardware can still reject a matching owner.

Record the PPID `10B` snapshot and the next driver or exception. On an
exception, include X3 and X4, ELR, ESR, FAR, and the stack rows starting at
SP+0x38 and SP+0xC0. The faulting load uses X3 + X4 for its address.
