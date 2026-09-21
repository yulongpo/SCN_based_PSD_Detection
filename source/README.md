# Spectrum sources

The active build exposes exactly three source adapters:

- `BB60CSource`
- `HarogicSource`
- `FileSource`

`BB60CSource` uses the Signal Hound BB API SDK when
`SCN_BB60C_SDK_ROOT` points to a valid SDK installation. It opens the first
available BB60C, configures center/span, reference level, RBW/VBW, RBW window
shape (`Nuttall`, `Flattop`, or `CISPR`) and sweep detector, then returns the
hardware dBm trace from `bbFetchTrace_32f`. The reference level controls the
device's automatic gain/attenuation target and is also used by the UI as the
top of the 100 dB display range.
`HarogicSource` uses the local Harogic HTRA SDK when `SCN_HAROGIC_SDK_ROOT`
points to a valid SDK. It opens the first USB device, configures the HTRA SWP
mode with the center/span, reference level, RBW and window settings, assembles
the partial sweeps into a complete trace, and publishes the returned hardware
dBm spectrum. HTRA data timestamps are used when available. If the SDK or its
runtime DLLs are missing, the adapter fails configuration explicitly and the
UI reports the source as disconnected; it does not fall back to synthetic data.
`FileSource` follows the original ISA offline spectrum format. A `.dat` file
contains consecutive little-endian `float32` power-spectrum frames. Its name
may contain `Fc=..._Bw=..._Rbw=..._Reflevel=..._SpectrumLen=...`; these values
determine the frequency range, RBW, reference level, and frame length. The
reader stops at EOF or loops according to the UI setting. Generic `.bin`
float32 and `.txt/.csv/.asc` text files remain supported.
