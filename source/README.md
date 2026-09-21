# Spectrum sources

The active build exposes exactly three source adapters:

- `BB60CSource`
- `HarogicSource`
- `FileSource`

The BB60C and Harogic adapters currently generate deterministic spectrum
profiles so the Qt 6 UI can be exercised without vendor SDKs. Their class
boundaries are the intended locations for the original device API calls.
`FileSource` follows the original ISA offline spectrum format. A `.dat` file
contains consecutive little-endian `float32` power-spectrum frames. Its name
may contain `Fc=..._Bw=..._Rbw=..._Reflevel=..._SpectrumLen=...`; these values
determine the frequency range, RBW, reference level, and frame length. The
reader stops at EOF or loops according to the UI setting. Generic `.bin`
float32 and `.txt/.csv/.asc` text files remain supported.
