# Dapico Load (macOS)

Minimal, self-contained loader extracted from picotool. This tool loads a **stripped ELF** onto a Raspberry Pi RP2040/RP2350 device in BOOTSEL mode using the PICOBOOT USB interface.

This macOS build uses the system IOKit USB stack directly (no `libusb`).

## Requirements

- macOS
- CMake 3.16+
- Xcode Command Line Tools (for IOKit/CoreFoundation headers)

## Build

```bash
cmake -S . -B build
cmake --build build
```

The resulting binary is `build/dapico-load`.

## Usage

```bash
./build/dapico-load path/to/firmware.elf
```

Enable flash writes (disabled by default; flash segments mirror into SRAM when possible):

```bash
./build/dapico-load --flash path/to/firmware.elf
```

Optional flags:

- `--flash` allow writing flash segments (default mirrors flash segments into SRAM).
- `--no-exec` skip executing the loaded image.
- `--dryrun` print planned operations without using a connected device.

## Notes

- Only stripped ELF inputs are supported (no UF2 or BIN).
- The device must already be in BOOTSEL mode.
