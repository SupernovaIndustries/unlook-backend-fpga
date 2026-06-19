#pragma once

/**
 * @file XdmaTransport.hpp
 * @brief Thin RAII wrapper over the Xilinx XDMA character devices.
 *
 * Opens the `_user` BAR (AXI-Lite registers, via mmap), and the `_h2c_0` /
 * `_c2h_0` bulk DMA channels (via pwrite/pread). All methods return bool and
 * never throw, mirroring the Unlook SDK hardware-controller conventions.
 *
 * One transport instance == one open device, owned by one matcher; not
 * re-entrant (single set of control registers + DMA channels).
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace unlook_fpga {

class XdmaTransport {
public:
    explicit XdmaTransport(std::string devicePrefix, int dmaTimeoutMs);
    ~XdmaTransport();

    XdmaTransport(const XdmaTransport&) = delete;
    XdmaTransport& operator=(const XdmaTransport&) = delete;

    /// Open _user (mmap the AXI-Lite BAR) + _h2c_0 + _c2h_0. false on any failure.
    bool open();
    bool isOpen() const { return userMap_ != nullptr; }

    /// AXI-Lite 32-bit register access through the mmap'd _user BAR.
    bool writeReg(uint32_t offset, uint32_t value);
    bool readReg(uint32_t offset, uint32_t& value);

    /// Bulk DMA. cardAddr is the offset into card memory (see RegisterMap).
    bool writeH2C(const void* src, size_t bytes, uint64_t cardAddr);
    bool readC2H(void* dst, size_t bytes, uint64_t cardAddr);

    int dmaTimeoutMs() const { return dmaTimeoutMs_; }
    const std::string& devicePrefix() const { return prefix_; }
    const std::string& lastError() const { return lastError_; }

private:
    void close();
    bool fail(const std::string& what);

    std::string prefix_;          ///< e.g. "/dev/xdma0"
    int         dmaTimeoutMs_ = 33;
    int         userFd_ = -1;
    int         h2cFd_  = -1;
    int         c2hFd_  = -1;
    void*       userMap_ = nullptr;
    size_t      userMapLen_ = 0;
    std::string lastError_;
};

} // namespace unlook_fpga
