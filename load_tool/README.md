# Pico Load (macOS/Linux)

Minimal, self-contained loader extracted from picotool. This tool loads a **stripped ELF** onto a Raspberry Pi RP2040/RP2350 device in BOOTSEL mode using the PICOBOOT USB interface.

## Requirements

- macOS or Linux
- CMake 3.16+
- `libusb-1.0`

Install libusb:

```bash
# macOS
brew install libusb

# Debian/Ubuntu
sudo apt-get install libusb-1.0-0-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

The resulting binary is `build/pico-load`.

## Usage

```bash
./build/pico-load path/to/firmware.elf
```

Enable flash writes (disabled by default; flash segments mirror into SRAM when possible):

```bash
./build/pico-load --flash path/to/firmware.elf
```

Optional flags:

- `--flash` allow writing flash segments (default mirrors flash segments into SRAM).
- `--no-exec` skip executing the loaded image.
- `--verbose` enable libusb debug output.

## Notes

- Only stripped ELF inputs are supported (no UF2 or BIN).
- The device must already be in BOOTSEL mode.
