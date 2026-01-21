#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "boot/picoboot.h"
#include "pico/stdio_usb/reset_interface.h"

namespace {
constexpr uint16_t kVendorIdRaspberryPi = 0x2e8a;
constexpr uint16_t kProductIdRp2040UsbBoot = 0x0003;
constexpr uint16_t kProductIdRp2350UsbBoot = 0x000f;
constexpr uint16_t kProductIdRp2040StdioUsb = 0x000a;
constexpr uint16_t kProductIdRp2350StdioUsb = 0x0009;
constexpr uint32_t kUsbTimeoutMs = 3000;

struct PicobootInterface {
    UInt8 interface_number{};
    UInt8 pipe_in{};
    UInt8 pipe_out{};
    IOUSBInterfaceInterface **iface{};
};

struct ResetInterface {
    UInt8 interface_number{};
    IOUSBInterfaceInterface **iface{};
};

struct DeviceMatch {
    IOUSBDeviceInterface **device{};
    uint16_t product_id{};
    std::optional<PicobootInterface> picoboot;
    std::optional<ResetInterface> reset;
};

void print_usage(const char *argv0) {
    std::cout << "Usage: " << argv0 << " [--bootsel] [--verbose]\n"
              << "  --bootsel  Reboot into BOOTSEL mode (if reset interface is available)\n"
              << "  --verbose  Enable extra logging\n";
}

uint32_t cf_number_to_uint32(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return 0;
    }
    uint32_t out = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &out);
    return out;
}

IOUSBDeviceInterface **create_device_interface(io_service_t device_service) {
    IOCFPlugInInterface **plug_in = nullptr;
    SInt32 score = 0;
    IOReturn ret = IOCreatePlugInInterfaceForService(device_service, kIOUSBDeviceUserClientTypeID,
                                                     kIOCFPlugInInterfaceID, &plug_in, &score);
    if (ret != kIOReturnSuccess || !plug_in) {
        return nullptr;
    }

    IOUSBDeviceInterface **device = nullptr;
    HRESULT result = (*plug_in)->QueryInterface(plug_in, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                reinterpret_cast<void **>(&device));
    (*plug_in)->Release(plug_in);
    if (result || !device) {
        return nullptr;
    }
    return device;
}

IOUSBInterfaceInterface **create_interface_interface(io_service_t interface_service) {
    IOCFPlugInInterface **plug_in = nullptr;
    SInt32 score = 0;
    IOReturn ret = IOCreatePlugInInterfaceForService(interface_service, kIOUSBInterfaceUserClientTypeID,
                                                     kIOCFPlugInInterfaceID, &plug_in, &score);
    if (ret != kIOReturnSuccess || !plug_in) {
        return nullptr;
    }

    IOUSBInterfaceInterface **iface = nullptr;
    HRESULT result = (*plug_in)->QueryInterface(plug_in, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                                reinterpret_cast<void **>(&iface));
    (*plug_in)->Release(plug_in);
    if (result || !iface) {
        return nullptr;
    }
    return iface;
}

bool is_supported_product(uint32_t product_id) {
    return product_id == kProductIdRp2040UsbBoot || product_id == kProductIdRp2350UsbBoot ||
           product_id == kProductIdRp2040StdioUsb || product_id == kProductIdRp2350StdioUsb;
}

std::optional<DeviceMatch> find_device(bool verbose) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matching) {
        return std::nullopt;
    }

    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != kIOReturnSuccess) {
        return std::nullopt;
    }

    std::optional<DeviceMatch> match;
    io_service_t device_service = 0;
    while ((device_service = IOIteratorNext(iterator)) != 0) {
        CFTypeRef vendor_ref = IORegistryEntryCreateCFProperty(device_service, CFSTR(kUSBVendorID),
                                                               kCFAllocatorDefault, 0);
        CFTypeRef product_ref = IORegistryEntryCreateCFProperty(device_service, CFSTR(kUSBProductID),
                                                                kCFAllocatorDefault, 0);
        uint32_t vendor_id = cf_number_to_uint32(vendor_ref);
        uint32_t product_id = cf_number_to_uint32(product_ref);
        if (vendor_ref) {
            CFRelease(vendor_ref);
        }
        if (product_ref) {
            CFRelease(product_ref);
        }

        if (vendor_id != kVendorIdRaspberryPi || !is_supported_product(product_id)) {
            IOObjectRelease(device_service);
            continue;
        }

        IOUSBDeviceInterface **device = create_device_interface(device_service);
        if (!device) {
            IOObjectRelease(device_service);
            continue;
        }
        if ((*device)->USBDeviceOpen(device) != kIOReturnSuccess) {
            (*device)->Release(device);
            IOObjectRelease(device_service);
            continue;
        }

        IOUSBFindInterfaceRequest request;
        request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
        request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
        request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
        request.bAlternateSetting = kIOUSBFindInterfaceDontCare;

        io_iterator_t iface_iterator = 0;
        if ((*device)->CreateInterfaceIterator(device, &request, &iface_iterator) != kIOReturnSuccess) {
            (*device)->USBDeviceClose(device);
            (*device)->Release(device);
            IOObjectRelease(device_service);
            continue;
        }

        std::optional<PicobootInterface> picoboot;
        std::optional<ResetInterface> reset;

        io_service_t interface_service = 0;
        while ((interface_service = IOIteratorNext(iface_iterator)) != 0) {
            IOUSBInterfaceInterface **iface = create_interface_interface(interface_service);
            IOObjectRelease(interface_service);
            if (!iface) {
                continue;
            }
            if ((*iface)->USBInterfaceOpen(iface) != kIOReturnSuccess) {
                (*iface)->Release(iface);
                continue;
            }

            UInt8 interface_class = 0;
            UInt8 interface_subclass = 0;
            UInt8 interface_protocol = 0;
            UInt8 interface_number = 0;
            (*iface)->GetInterfaceClass(iface, &interface_class);
            (*iface)->GetInterfaceSubClass(iface, &interface_subclass);
            (*iface)->GetInterfaceProtocol(iface, &interface_protocol);
            (*iface)->GetInterfaceNumber(iface, &interface_number);

            if (verbose) {
                std::cout << "Found interface " << static_cast<int>(interface_number)
                          << " class=" << static_cast<int>(interface_class)
                          << " subclass=" << static_cast<int>(interface_subclass)
                          << " protocol=" << static_cast<int>(interface_protocol) << "\n";
            }

            bool matched = false;
            if (interface_class == 0xff && interface_subclass == RESET_INTERFACE_SUBCLASS &&
                interface_protocol == RESET_INTERFACE_PROTOCOL) {
                reset = ResetInterface{interface_number, iface};
                matched = true;
            }

            if (interface_class == 0xff && !picoboot) {
                UInt8 num_endpoints = 0;
                (*iface)->GetNumEndpoints(iface, &num_endpoints);

                UInt8 pipe_in = 0;
                UInt8 pipe_out = 0;
                for (UInt8 pipe_ref = 1; pipe_ref <= num_endpoints; ++pipe_ref) {
                    UInt8 direction = 0;
                    UInt8 number = 0;
                    UInt8 transfer_type = 0;
                    UInt16 max_packet = 0;
                    UInt8 interval = 0;
                    if ((*iface)->GetPipeProperties(iface, pipe_ref, &direction, &number, &transfer_type,
                                                    &max_packet, &interval) != kIOReturnSuccess) {
                        continue;
                    }
                    if (transfer_type != kUSBBulk) {
                        continue;
                    }
                    if (direction == kUSBIn) {
                        pipe_in = pipe_ref;
                    } else if (direction == kUSBOut) {
                        pipe_out = pipe_ref;
                    }
                }

                if (pipe_in != 0 && pipe_out != 0) {
                    picoboot = PicobootInterface{interface_number, pipe_in, pipe_out, iface};
                    matched = true;
                }
            }

            if (!matched) {
                (*iface)->USBInterfaceClose(iface);
                (*iface)->Release(iface);
            }

            if (picoboot && reset) {
                break;
            }
        }

        IOObjectRelease(iface_iterator);
        if (picoboot || reset) {
            match = DeviceMatch{device, static_cast<uint16_t>(product_id), picoboot, reset};
            IOObjectRelease(device_service);
            break;
        }

        if (picoboot) {
            (*picoboot->iface)->USBInterfaceClose(picoboot->iface);
            (*picoboot->iface)->Release(picoboot->iface);
        }
        if (reset) {
            (*reset->iface)->USBInterfaceClose(reset->iface);
            (*reset->iface)->Release(reset->iface);
        }
        (*device)->USBDeviceClose(device);
        (*device)->Release(device);
        IOObjectRelease(device_service);
    }

    IOObjectRelease(iterator);
    return match;
}

IOReturn send_picoboot_command(IOUSBInterfaceInterface **iface, const PicobootInterface &picoboot,
                               picoboot_cmd &cmd) {
    static uint32_t token = 1;
    cmd.dMagic = PICOBOOT_MAGIC;
    cmd.dToken = token++;

    IOReturn ret = (*iface)->WritePipeTO(iface, picoboot.pipe_out, &cmd, sizeof(cmd), kUsbTimeoutMs, kUsbTimeoutMs);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    uint8_t ack = 0;
    UInt32 ack_len = 1;
    return (*iface)->ReadPipeTO(iface, picoboot.pipe_in, &ack, &ack_len, kUsbTimeoutMs, kUsbTimeoutMs);
}

IOReturn reboot_via_picoboot(IOUSBInterfaceInterface **iface, uint16_t product_id,
                             const PicobootInterface &picoboot, bool bootsel) {
    if (bootsel) {
        std::cout << "Device is already in BOOTSEL mode.\n";
        return kIOReturnSuccess;
    }

    picoboot_cmd cmd{};
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

    return send_picoboot_command(iface, picoboot, cmd);
}

IOReturn reboot_via_reset_interface(IOUSBInterfaceInterface **iface, const ResetInterface &reset_iface,
                                    bool bootsel) {
    IOUSBDevRequest request{};
    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBInterface);
    request.bRequest = bootsel ? RESET_REQUEST_BOOTSEL : RESET_REQUEST_FLASH;
    request.wValue = 0;
    request.wIndex = reset_iface.interface_number;
    request.wLength = 0;
    request.pData = nullptr;
    return (*iface)->ControlRequest(iface, 0, &request);
}

void close_interface(IOUSBInterfaceInterface **iface) {
    if (!iface) {
        return;
    }
    (*iface)->USBInterfaceClose(iface);
    (*iface)->Release(iface);
}

void close_device(IOUSBDeviceInterface **device) {
    if (!device) {
        return;
    }
    (*device)->USBDeviceClose(device);
    (*device)->Release(device);
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

    auto match = find_device(verbose);
    if (!match) {
        std::cerr << "No Raspberry Pi USB device found.\n";
        return 1;
    }

    IOReturn ret = kIOReturnSuccess;
    if (bootsel) {
        if (match->reset) {
            ret = reboot_via_reset_interface(match->reset->iface, *match->reset, true);
        } else if (match->picoboot) {
            ret = reboot_via_picoboot(match->picoboot->iface, match->product_id, *match->picoboot, true);
        } else {
            std::cerr << "Device does not expose a reset or picoboot interface.\n";
            ret = kIOReturnError;
        }
    } else {
        if (match->picoboot) {
            ret = reboot_via_picoboot(match->picoboot->iface, match->product_id, *match->picoboot, false);
        } else if (match->reset) {
            ret = reboot_via_reset_interface(match->reset->iface, *match->reset, false);
        } else {
            std::cerr << "Device does not expose a reset or picoboot interface.\n";
            ret = kIOReturnError;
        }
    }

    if (ret != kIOReturnSuccess) {
        std::cerr << "Reboot request failed (IOReturn " << ret << ").\n";
    } else if (!bootsel) {
        std::cout << "Reboot request sent.\n";
    } else if (match->reset) {
        std::cout << "Requested reboot into BOOTSEL mode.\n";
    }

    if (match->picoboot) {
        close_interface(match->picoboot->iface);
    }
    if (match->reset) {
        close_interface(match->reset->iface);
    }
    close_device(match->device);

    return ret == kIOReturnSuccess ? 0 : 1;
}
