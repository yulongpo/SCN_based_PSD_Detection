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
`HarogicSource` remains a placeholder adapter until its HTRA device contract
is migrated; the UI reports it as not connected rather than treating the
deterministic profile as a hardware connection.
`FileSource` follows the original ISA offline spectrum format. A `.dat` file
contains consecutive little-endian `float32` power-spectrum frames. Its name
may contain `Fc=..._Bw=..._Rbw=..._Reflevel=..._SpectrumLen=...`; these values
determine the frequency range, RBW, reference level, and frame length. The
reader stops at EOF or loops according to the UI setting. Generic `.bin`
float32 and `.txt/.csv/.asc` text files remain supported.
