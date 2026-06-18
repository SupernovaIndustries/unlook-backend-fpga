#pragma once

/**
 * @file FpgaDetector.hpp
 * @brief Stateless probe for a present Unlook XC7A200T core on PCIe.
 *
 * Mirrors the AS1170 detection idiom in the SDK: cheap, no held handles, bool
 * outcome. Checks the XDMA char device exists, confirms the Xilinx PCI vendor
 * id (0x10ee) via sysfs, then opens briefly to read the ID/VERSION registers.
 */

#include <cstdint>
#include <string>

namespace unlook_fpga {

struct ProbeResult {
    bool        present = false;
    uint16_t    pciVendorId = 0;
    uint16_t    pciDeviceId = 0;
    uint32_t    bitstreamVersion = 0;
    std::string devicePath;
    std::string detail;
};

/// Probe @p devicePrefix (e.g. "/dev/xdma0"). Never throws.
ProbeResult probeFpga(const std::string& devicePrefix);

} // namespace unlook_fpga
