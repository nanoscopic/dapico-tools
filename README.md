## Overview

`picotool` is a tool for working with RP2040/RP2350 binaries, and interacting with RP2040/RP2350 devices when they are in BOOTSEL mode. (As of version 1.1 of `picotool` it is also possible to interact with devices that are not in BOOTSEL mode, but are using USB stdio support from the Raspberry Pi Pico SDK by using the `-f` argument of `picotool`).

This stripped-down build only supports the `info`, `load`, and `reboot` commands (plus `help` for usage).

Note for additional documentation see https://rptl.io/pico-get-started

```text
$ picotool help
PICOTOOL:
    Tool for interacting with RP-series device(s) in BOOTSEL mode, or with an RP-series binary

SYNOPSIS:
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] [device-selection]
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] <filename> [-t <type>]
    picotool load [--ignore-partitions] [--family <family_id>] [-p <partition>] [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o
                <offset>] [device-selection]
    picotool reboot [-a] [-u] [-g <partition>] [-c <cpu>] [device-selection]
    picotool help [<cmd>]

COMMANDS:
    info        Display information from the target device(s) or file.
                Without any arguments, this will display basic information for all connected RP-series devices in BOOTSEL mode
    load        Load the program / memory range stored in a file onto the device.
    reboot      Reboot the device
    help        Show general help or help for a specific command

Use "picotool help <cmd>" for more info
```

Note commands that aren't acting on files require a device in BOOTSEL mode to be connected.

## Building & Installing

If you don't want to build picotool yourself, you can find pre-built executables for Windows, macOS, and Linux in the [pico-sdk-tools](https://github.com/raspberrypi/pico-sdk-tools/releases) repository. Assuming you've extracted that archive to `<extract_location>` (with the actual picotool executable at `<extract_location>/picotool/picotool`), you can point the Pico SDK at this binary by setting the `picotool_DIR` environment variable to `<extract_location>/picotool`, or by passing `-Dpicotool_DIR=<extract_location>/picotool` to your `cmake` command or setting it in your `CMakeLists.txt` file.

If you do wish to build picotool yourself, then see [Building](BUILDING.md#building) for build instructions. For the Pico SDK to find your picotool you will need to install it, the simplest way being to run `cmake --install .` - see [Installing](BUILDING.md#installing-so-the-pico-sdk-can-find-it) for more details and alternatives. **You cannot just copy the binary into your `PATH`, else the Pico SDK will not be able to locate it.**

## info

There is _Binary Information_ support in the SDK which allows for easily storing compact information that `picotool`
can find (See Binary Info section below). The info command is for reading this information.

The information can be either read from one or more connected devices in BOOTSEL mode, or from 
a file. This file can be an ELF, a UF2 or a BIN file.

```text
$ picotool help info
INFO:
    Display information from the target device(s) or file.
    Without any arguments, this will display basic information for all connected RP-series devices in BOOTSEL mode

SYNOPSIS:
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] [device-selection]
    picotool info [-b] [-m] [-p] [-d] [--debug] [-l] [-a] <filename> [-t <type>]

OPTIONS:
    Information to display
        -b, --basic
            Include basic information. This is the default
        -m, --metadata
            Include all metadata blocks
        -p, --pins
            Include pin information
        -d, --device
            Include device information
        --debug
            Include device debug information
        -l, --build
            Include build attributes
        -a, --all
            Include all information

TARGET SELECTION:
    To target one or more connected RP-series device(s) in BOOTSEL mode (the default)
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            USB drive mounted
    To target a file
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
```

Note the -f arguments vary slightly for Windows vs macOS / Unix platforms.

e.g.

```text
$ picotool info
Program Information
 name:      hello_world
 features:  stdout to UART
```

```text
$ picotool info -a
Program Information
 name:          hello_world
 features:      stdout to UART
 binary start:  0x10000000
 binary end:    0x1000606c

Fixed Pin Information
 20:  UART1 TX
 21:  UART1 RX

Build Information
 build date:        Dec 31 2020
 build attributes:  Debug build

Device Information
 flash size:   2048K
 ROM version:  2
```

```text
$ picotool info -bp
Program Information
 name:      hello_world
 features:  stdout to UART

Fixed Pin Information
 20:  UART1 TX
 21:  UART1 RX
```

```text
$ picotool info -a lcd_1602_i2c.uf2
File lcd_1602_i2c.uf2:

Program Information
 name:          lcd_1602_i2c
 web site:      https://github.com/raspberrypi/pico-examples/tree/HEAD/i2c/lcd_1602_i2c
 binary start:  0x10000000
 binary end:    0x10003c1c

Fixed Pin Information
 4:  I2C0 SDA
 5:  I2C0 SCL

Build Information
 build date:  Dec 31 2020
```

## load

`load` allows you to write data from a file onto the device (either writing to flash, or to RAM)

```text
$ picotool help load
LOAD:
    Load the program / memory range stored in a file onto the device.

SYNOPSIS:
    picotool load [--ignore-partitions] [--family <family_id>] [-p <partition>] [-n] [-N] [-u] [-v] [-x] <filename> [-t <type>] [-o
                <offset>] [device-selection]

OPTIONS:
    Post load actions
        --ignore-partitions
            When writing flash data, ignore the partition table and write to absolute space
        --family
            Specify the family ID of the file to load
        <family_id>
            family ID to use for load
        -p, --partition
            Specify the partition to load into
        <partition>
            partition to load into
        -n, --no-overwrite
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the
            program in flash, the command fails
        -N, --no-overwrite-unsafe
            When writing flash data, do not overwrite an existing program in flash. If picotool cannot determine the size/presence of the
            program in flash, the load continues anyway
        -u, --update
            Skip writing flash sectors that already contain identical data
        -v, --verify
            Verify the data was written correctly
        -x, --execute
            Attempt to execute the downloaded file as a program after the load
    File to load from
        <filename>
            The file name
        -t <type>
            Specify file type (uf2 | elf | bin) explicitly, ignoring file extension
    BIN file options
        -o, --offset
            Specify the load address for a BIN file
        <offset>
            Load offset (memory address; default 0x10000000)
    Target device selection
        --bus <bus>
            Filter devices by USB bus number
        --address <addr>
            Filter devices by USB device address
        --vid <vid>
            Filter by vendor id
        --pid <pid>
            Filter by product id
        --ser <ser>
            Filter by serial number
        -f, --force
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be rebooted back to application mode
        -F, --force-no-reboot
            Force a device not in BOOTSEL mode but running compatible code to reset so the command can be executed. After executing the
            command (unless the command itself is a 'reboot') the device will be left connected and accessible to picotool, but without the
            USB drive mounted
```

e.g.

```text
$ picotool load blink.uf2
Loading into Flash: [==============================]  100%
```

## Binary Information

Binary information is machine locatable and generally machine consumable. I say generally because anyone can
include any information, and we can tell it from ours, but it is up to them whether they make their data self describing.

Note that we will certainly add more binary info over time, but I'd like to get a minimum core set included
in most binaries from launch!!

### Basic Information

This information is really handy when you pick up a Pico and don't know what is on it!

Basic information includes

- program name
- program description
- program version string
- program build date
- program url
- program end address
- program features - this is a list built from individual strings in the binary, that can be displayed (e.g. we will have one for UART stdio and one for USB stdio) in the SDK
- build attributes - this is a similar list of strings, for things pertaining to the binary itself (e.g. Debug Build)

The binary information is self-describing/extensible, so programs can include information picotool is not aware of (e.g. MicroPython includes a list of in-built libraries)

### Pins

This is certainly handy when you have an executable called 'hello_world.elf' but you forgot what board it is built for...

Static (fixed) pin assignments can be recorded in the binary in very compact form:

```text
$ picotool info --pins sprite_demo.elf
File sprite_demo.elf:

Fixed Pin Information
0-4:    Red 0-4
6-10:   Green 0-4
11-15:  Blue 0-4
16:     HSync
17:     VSync
18:     Display Enable
19:     Pixel Clock
20:     UART1 TX
21:     UART1 RX
```

### Including Binary information

Binary information is declared in the program by macros (vile warped macros); for the pins example:

```text
$ picotool info --pins sprite_demo.elf
File sprite_demo.elf:

Fixed Pin Information
0-4:    Red 0-4
6-10:   Green 0-4
11-15:  Blue 0-4
16:     HSync
17:     VSync
18:     Display Enable
19:     Pixel Clock
20:     UART1 TX
21:     UART1 RX
```

... there is one line in the `setup_default_uart` function:

```c
bi_decl_if_func_used(bi_2pins_with_func(PICO_DEFAULT_UART_RX_PIN, PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART));
```


The two pin numbers, and the function UART are stored, then decoded to their actual function names (UART1 TX etc) by picotool.
The `bi_decl_if_func_used` makes sure the binary information is only included if the containing function is called.

Equally, the video code contains a few lines like this:

```c
bi_decl_if_func_used(bi_pin_mask_with_name(0x1f << (PICO_SCANVIDEO_COLOR_PIN_BASE + PICO_SCANVIDEO_DPI_PIXEL_RSHIFT), "Red 0-4"));
```

For the configuration example, you put the line

```c
bi_decl(bi_ptr_string(0x1111, 0x3333, name, "Billy", 128));
```

into your code, which will then create the name variable for you to subsequently print.
The parameters are the tag, the ID, variable name, default value, and maximum string length.

```c
printf("Name is %s\n", name);
```

### Details

Things are designed to waste as little space as possible, but you can turn everything off with preprocessor variable `PICO_NO_BINARY_INFO=1`. Additionally,
any SDK code that inserts binary info can be separately excluded by its own preprocessor variable.

You need
```c
#include "pico/binary_info.h"
```

Basically you either use `bi_decl(bi_blah(...))` for unconditional inclusion of the binary info blah, or
`bi_decl_if_func_used(bi_blah(...))` for binary information that may be stripped if the enclosing function
is not included in the binary by the linker (think `--gc-sections`)

There are a bunch of bi_ macros in the headers

```c
#define bi_binary_end(end) ...
#define bi_program_name(name) ...
#define bi_program_description(description) ...
#define bi_program_version_string(version_string) ...
#define bi_program_build_date_string(date_string) ...
#define bi_program_url(url) ...
#define bi_program_feature(feature) ...
#define bi_program_build_attribute(attr) ...
#define bi_1pin_with_func(p0, func) ...
#define bi_2pins_with_func(p0, p1, func) ...
#define bi_3pins_with_func(p0, p1, p2, func) ...
#define bi_4pins_with_func(p0, p1, p2, p3, func) ...
#define bi_5pins_with_func(p0, p1, p2, p3, p4, func) ...
#define bi_pin_range_with_func(plo, phi, func) ...
#define bi_pin_mask_with_name(pmask, label) ...
#define bi_pin_mask_with_names(pmask, label) ...
#define bi_1pin_with_name(p0, name) ...
#define bi_2pins_with_names(p0, name0, p1, name1) ...
#define bi_3pins_with_names(p0, name0, p1, name1, p2, name2) ...
#define bi_4pins_with_names(p0, name0, p1, name1, p2, name2, p3, name3) ... 
```

which make use of underlying macros, e.g.
```c
#define bi_program_url(url) bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_PROGRAM_URL, url)
```

NOTE: It is easy to forget to enclose these in `bi_decl` etc., so an effort has been made (at the expense of a lot of kittens)
to make the build fail with a _somewhat_ helpful error message if you do so.

For example, trying to compile

```c
bi_1pin_with_name(0, "Toaster activator");
```

gives

```
/home/graham/dev/mu/pico_sdk/src/common/pico_binary_info/include/pico/binary_info/code.h:17:55: error: '_error_bi_is_missing_enclosing_decl_261' undeclared here (not in a function)
17 | #define __bi_enclosure_check_lineno_var_name __CONCAT(_error_bi_is_missing_enclosing_decl_,__LINE__)
|                                                       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
... more macro call stack of doom
```

## Setting common fields from CMake

You can use 

```cmake
pico_set_program_name(foo "not foo") # as "foo" would be the default
pico_set_program_description(foo "this is a foo")
pico_set_program_version(foo "0.00001a")
pico_set_program_url(foo "www.plinth.com/foo")
```

Note all of these are passed as command line arguments to the compilation, so if you plan to use
quotes, newlines etc you may have better luck defining via bi_decl in the code.

## Additional binary information/picotool features

### Block devices

MicroPython and CircuitPython, eventually the SDK and others may support one or more storage devices in flash. We already
have macros to define these although picotool doesn't do anything with them yet... but backup/restore/file copy and even fuse mount
in the future might be interesting.

I suggest we tag these now... 

This is what I have right now off the top of my head (at the time)
```c
#define bi_block_device(_tag, _name, _offset, _size, _extra, _flags)
```
with the data going into
```c
typedef struct __packed _binary_info_block_device {
        struct _binary_info_core core;
        bi_ptr_of(const char) name; // optional static name (independent of what is formatted)
        uint32_t offset;
        uint32_t size;
        bi_ptr_of(binary_info_t) extra; // additional info
        uint16_t flags;
} binary_info_block_device_t;
```
and
```c
enum {
    BINARY_INFO_BLOCK_DEV_FLAG_READ = 1 << 0, // if not readable, then it is basically hidden, but tools may choose to avoid overwriting it
    BINARY_INFO_BLOCK_DEV_FLAG_WRITE = 1 << 1,
    BINARY_INFO_BLOCK_DEV_FLAG_REFORMAT = 1 << 2, // may be reformatted..

    BINARY_INFO_BLOCK_DEV_FLAG_PT_UNKNOWN = 0 << 4, // unknown free to look
    BINARY_INFO_BLOCK_DEV_FLAG_PT_MBR = 1 << 4, // expect MBR
    BINARY_INFO_BLOCK_DEV_FLAG_PT_GPT = 2 << 4, // expect GPT
    BINARY_INFO_BLOCK_DEV_FLAG_PT_NONE = 3 << 4, // no partition table
};
```

### Forced Reboots

Running commands with `-f/F` requires compatible code to be running on the device. The definition of compatible code for the
purposes of binaries compiled using the [pico-sdk](https://github.com/raspberrypi/pico-sdk) is code that
- Is still running -
If your code has returned then rebooting with `-f/F` will not work - instead you can set the compile definition `PICO_ENTER_USB_BOOT_ON_EXIT`
to reboot and be accessible to picotool once your code has finished execution, for example with
`target_compile_definitions(<yourTargetName> PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)`
- Uses stdio_usb -
If your binary calls `stdio_init_all()` and you have `pico_enable_stdio_usb(<yourTargetName> 1)` in your CMakeLists.txt file then you meet
this requirement (see the [hello_usb](https://github.com/raspberrypi/pico-examples/tree/master/hello_world/usb) example)

### Issues

If you ctrl+c out of the middle of a long operation, then libusb seems to get a bit confused, which means we aren't able
to unlock our lockout of USB MSD writes (we have turned them off so the user doesn't step on their own toes). Simply running
`picotool info` again will unlock it properly the next time (or you can reboot the device).
