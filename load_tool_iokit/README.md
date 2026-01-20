# Pico Load (macOS IOKit)

Minimal, self-contained loader extracted from picotool. This tool loads a **stripped ELF** onto a Raspberry Pi RP2040/RP2350 device in BOOTSEL mode using the PICOBOOT USB interface.

This variant is macOS-specific and uses the system IOKit USB stack directly (no `libusb`).

## Requirements

- macOS
- CMake 3.16+
- Xcode Command Line Tools (for IOKit/CoreFoundation headers)

## Build

```bash
cmake -S . -B build
cmake --build build
```

The resulting binary is `build/pico-load-iokit`.

## Usage

```bash
./build/pico-load-iokit path/to/firmware.elf
```

Enable flash writes (disabled by default; flash segments mirror into SRAM when possible):

```bash
./build/pico-load-iokit --flash path/to/firmware.elf
```

Optional flags:

- `--flash` allow writing flash segments (default mirrors flash segments into SRAM).
- `--no-exec` skip executing the loaded image.

## Notes

- Only stripped ELF inputs are supported (no UF2 or BIN).
- The device must already be in BOOTSEL mode.
