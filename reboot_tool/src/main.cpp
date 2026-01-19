#include <libusb.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "boot/picoboot.h"
#include "pico/stdio_usb/reset_interface.h"

namespace {
constexpr uint16_t kVendorIdRaspberryPi = 0x2e8a;
constexpr uint16_t kProductIdRp2040UsbBoot = 0x0003;
constexpr uint16_t kProductIdRp2350UsbBoot = 0x000f;
constexpr uint16_t kProductIdRp2040StdioUsb = 0x000a;
constexpr uint16_t kProductIdRp2350StdioUsb = 0x0009;

struct PicobootInterface {
    uint8_t interface_number{};
    uint8_t ep_in{};
    uint8_t ep_out{};
};

struct ResetInterface {
    uint8_t interface_number{};
};

struct DeviceMatch {
    libusb_device *device{};
    libusb_device_handle *handle{};
    uint16_t product_id{};
    std::optional<PicobootInterface> picoboot;
    std::optional<ResetInterface> reset;
};

void print_usage(const char *argv0) {
    std::cout << "Usage: " << argv0 << " [--bootsel] [--verbose]\n"
              << "  --bootsel  Reboot into BOOTSEL mode (if reset interface is available)\n"
              << "  --verbose  Enable libusb debug output\n";
}

bool is_picoboot_interface(const libusb_interface_descriptor &desc, uint8_t &ep_in, uint8_t &ep_out) {
    if (desc.bInterfaceClass != 0xff || desc.bNumEndpoints != 2) {
        return false;
    }
    ep_in = 0;
    ep_out = 0;
    for (int i = 0; i < desc.bNumEndpoints; ++i) {
        const auto &endpoint = desc.endpoint[i];
        if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
            ep_in = endpoint.bEndpointAddress;
        } else {
            ep_out = endpoint.bEndpointAddress;
        }
    }
    return ep_in != 0 && ep_out != 0;
}

std::optional<DeviceMatch> find_device(libusb_context *ctx) {
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return std::nullopt;
    }

    std::optional<DeviceMatch> match;
    for (ssize_t idx = 0; idx < count; ++idx) {
        libusb_device *device = list[idx];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(device, &desc) != 0) {
            continue;
        }
        if (desc.idVendor != kVendorIdRaspberryPi) {
            continue;
        }

        libusb_config_descriptor *config = nullptr;
        if (libusb_get_active_config_descriptor(device, &config) != 0) {
            continue;
        }

        DeviceMatch candidate{};
        candidate.device = device;
        candidate.product_id = desc.idProduct;

        for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
            const auto &interface = config->interface[i];
            if (interface.num_altsetting == 0) {
                continue;
            }
            const auto &alt = interface.altsetting[0];
            if (alt.bInterfaceClass == 0xff &&
                alt.bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
                alt.bInterfaceProtocol == RESET_INTERFACE_PROTOCOL) {
                candidate.reset = ResetInterface{alt.bInterfaceNumber};
                continue;
            }
            uint8_t ep_in = 0;
            uint8_t ep_out = 0;
            if (is_picoboot_interface(alt, ep_in, ep_out)) {
                candidate.picoboot = PicobootInterface{alt.bInterfaceNumber, ep_in, ep_out};
            }
        }

        libusb_free_config_descriptor(config);

        if (candidate.picoboot || candidate.reset) {
            match = candidate;
            break;
        }
    }

    libusb_free_device_list(list, 1);
    return match;
}

int claim_interface(libusb_device_handle *handle, uint8_t interface_number) {
#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x0100010A
    libusb_set_auto_detach_kernel_driver(handle, 1);
#endif
    return libusb_claim_interface(handle, interface_number);
}

int send_picoboot_command(libusb_device_handle *handle, const PicobootInterface &iface, struct picoboot_cmd &cmd) {
    int sent = 0;
    int ret = libusb_bulk_transfer(handle, iface.ep_out, reinterpret_cast<uint8_t *>(&cmd), sizeof(cmd), &sent, 3000);
    if (ret != 0 || sent != static_cast<int>(sizeof(cmd))) {
        return ret != 0 ? ret : LIBUSB_ERROR_IO;
    }

    uint8_t ack = 0;
    int received = 0;
    ret = libusb_bulk_transfer(handle, iface.ep_in, &ack, 1, &received, 3000);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

int reboot_via_picoboot(libusb_device_handle *handle, uint16_t product_id, const PicobootInterface &iface, bool bootsel) {
    if (bootsel) {
        std::cout << "Device is already in BOOTSEL mode.\n";
        return 0;
    }

    struct picoboot_cmd cmd{};
    cmd.dMagic = PICOBOOT_MAGIC;
    cmd.dToken = 1;
    cmd.dTransferLength = 0;

    if (product_id == kProductIdRp2350UsbBoot) {
        cmd.bCmdId = PC_REBOOT2;
        cmd.bCmdSize = sizeof(cmd.reboot2_cmd);
        cmd.reboot2_cmd.dFlags = REBOOT2_FLAG_REBOOT_TYPE_NORMAL;
        cmd.reboot2_cmd.dDelayMS = 500;
        cmd.reboot2_cmd.dParam0 = 0;
        cmd.reboot2_cmd.dParam1 = 0;
    } else {
        cmd.bCmdId = PC_REBOOT;
        cmd.bCmdSize = sizeof(cmd.reboot_cmd);
        cmd.reboot_cmd.dPC = 0;
        cmd.reboot_cmd.dSP = 0;
        cmd.reboot_cmd.dDelayMS = 500;
    }

    int claim = claim_interface(handle, iface.interface_number);
    if (claim != 0) {
        return claim;
    }

    int ret = send_picoboot_command(handle, iface, cmd);
    libusb_release_interface(handle, iface.interface_number);
    return ret;
}

int reboot_via_reset_interface(libusb_device_handle *handle, const ResetInterface &iface, bool bootsel) {
    int claim = claim_interface(handle, iface.interface_number);
    if (claim != 0) {
        return claim;
    }

    uint8_t request = bootsel ? RESET_REQUEST_BOOTSEL : RESET_REQUEST_FLASH;
    int ret = libusb_control_transfer(handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                      request, 0, iface.interface_number, nullptr, 0, 2000);
    libusb_release_interface(handle, iface.interface_number);
    return ret;
}
} // namespace

int main(int argc, char **argv) {
    bool bootsel = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bootsel" || arg == "-u") {
            bootsel = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }

    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        std::cerr << "Failed to initialize libusb.\n";
        return 1;
    }

    if (verbose) {
        libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
    }

    auto match = find_device(ctx);
    if (!match) {
        std::cerr << "No Raspberry Pi USB device found.\n";
        libusb_exit(ctx);
        return 1;
    }

    libusb_device_handle *handle = nullptr;
    if (libusb_open(match->device, &handle) != 0 || !handle) {
        std::cerr << "Failed to open USB device.\n";
        libusb_exit(ctx);
        return 1;
    }

    int ret = 0;
    if (bootsel) {
        if (match->reset) {
            ret = reboot_via_reset_interface(handle, *match->reset, true);
        } else if (match->picoboot) {
            ret = reboot_via_picoboot(handle, match->product_id, *match->picoboot, true);
        } else {
            std::cerr << "Device does not expose a reset or picoboot interface.\n";
            ret = 1;
        }
    } else {
        if (match->picoboot) {
            ret = reboot_via_picoboot(handle, match->product_id, *match->picoboot, false);
        } else if (match->reset) {
            ret = reboot_via_reset_interface(handle, *match->reset, false);
        } else {
            std::cerr << "Device does not expose a reset or picoboot interface.\n";
            ret = 1;
        }
    }

    if (ret != 0) {
        std::cerr << "Reboot request failed (libusb error " << ret << ").\n";
    } else if (!bootsel) {
        std::cout << "Reboot request sent.\n";
    } else if (match->reset) {
        std::cout << "Requested reboot into BOOTSEL mode.\n";
    }

    libusb_close(handle);
    libusb_exit(ctx);
    return ret == 0 ? 0 : 1;
}
