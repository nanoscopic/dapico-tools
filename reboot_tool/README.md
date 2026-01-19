# Pico Reboot (macOS)

Minimal, self-contained reboot utility extracted from picotool. This tool supports macOS and uses `libusb` to either:

- reboot a device currently in BOOTSEL mode back into the application, or
- reboot a running device into BOOTSEL mode via the USB reset interface.

## Requirements

- macOS
- CMake 3.16+
- `libusb-1.0`

Install libusb via Homebrew:

```bash
brew install libusb
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

The resulting binary is `build/pico-reboot`.

## Usage

```bash
./build/pico-reboot
```

Reboot into BOOTSEL mode (requires a reset interface from the running firmware):

```bash
./build/pico-reboot --bootsel
```

## Notes

- If the device is already in BOOTSEL mode and `--bootsel` is passed, the tool reports that no action is needed.
- The BOOTSEL reboot path relies on the reset interface exposed by firmware built with the Pico SDK USB stdio reset interface.
