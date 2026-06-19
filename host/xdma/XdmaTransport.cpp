#include "XdmaTransport.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace unlook_fpga {

namespace {
// XDMA AXI-Lite BAR window. 64 KiB comfortably covers the register file.
constexpr size_t kUserMapLen = 64 * 1024;
} // namespace

XdmaTransport::XdmaTransport(std::string devicePrefix, int dmaTimeoutMs)
    : prefix_(std::move(devicePrefix)), dmaTimeoutMs_(dmaTimeoutMs) {}

XdmaTransport::~XdmaTransport() { close(); }

bool XdmaTransport::fail(const std::string& what) {
    lastError_ = what + " (errno=" + std::to_string(errno) + ": " +
                 std::strerror(errno) + ")";
    return false;
}

bool XdmaTransport::open() {
    const std::string userPath = prefix_ + "_user";
    const std::string h2cPath  = prefix_ + "_h2c_0";
    const std::string c2hPath  = prefix_ + "_c2h_0";

    userFd_ = ::open(userPath.c_str(), O_RDWR | O_SYNC);
    if (userFd_ < 0) return fail("open " + userPath);

    h2cFd_ = ::open(h2cPath.c_str(), O_WRONLY);
    if (h2cFd_ < 0) return fail("open " + h2cPath);

    c2hFd_ = ::open(c2hPath.c_str(), O_RDONLY);
    if (c2hFd_ < 0) return fail("open " + c2hPath);

    userMap_ = ::mmap(nullptr, kUserMapLen, PROT_READ | PROT_WRITE,
                      MAP_SHARED, userFd_, 0);
    if (userMap_ == MAP_FAILED) {
        userMap_ = nullptr;
        return fail("mmap " + userPath);
    }
    userMapLen_ = kUserMapLen;
    return true;
}

void XdmaTransport::close() {
    if (userMap_) { ::munmap(userMap_, userMapLen_); userMap_ = nullptr; }
    if (c2hFd_  >= 0) { ::close(c2hFd_);  c2hFd_  = -1; }
    if (h2cFd_  >= 0) { ::close(h2cFd_);  h2cFd_  = -1; }
    if (userFd_ >= 0) { ::close(userFd_); userFd_ = -1; }
}

bool XdmaTransport::writeReg(uint32_t offset, uint32_t value) {
    if (!userMap_ || offset + sizeof(uint32_t) > userMapLen_)
        return fail("writeReg offset out of range");
    volatile uint32_t* p =
        reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(userMap_) + offset);
    *p = value;
    return true;
}

bool XdmaTransport::readReg(uint32_t offset, uint32_t& value) {
    if (!userMap_ || offset + sizeof(uint32_t) > userMapLen_)
        return fail("readReg offset out of range");
    volatile uint32_t* p =
        reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(userMap_) + offset);
    value = *p;
    return true;
}

bool XdmaTransport::writeH2C(const void* src, size_t bytes, uint64_t cardAddr) {
    if (h2cFd_ < 0) return fail("writeH2C: channel not open");
    const uint8_t* p = static_cast<const uint8_t*>(src);
    size_t done = 0;
    while (done < bytes) {
        const ssize_t n = ::pwrite(h2cFd_, p + done, bytes - done,
                                   static_cast<off_t>(cardAddr + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail("writeH2C pwrite");
        }
        if (n == 0) return fail("writeH2C short write");
        done += static_cast<size_t>(n);
    }
    return true;
}

bool XdmaTransport::readC2H(void* dst, size_t bytes, uint64_t cardAddr) {
    if (c2hFd_ < 0) return fail("readC2H: channel not open");
    uint8_t* p = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < bytes) {
        const ssize_t n = ::pread(c2hFd_, p + done, bytes - done,
                                  static_cast<off_t>(cardAddr + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail("readC2H pread");
        }
        if (n == 0) return fail("readC2H short read");
        done += static_cast<size_t>(n);
    }
    return true;
}

} // namespace unlook_fpga
