#pragma once
// Read-only mmap of the whole ITCH file. mmap keeps file load out of the
// measured loop (the pages fault in, but we can also fault them ahead of time)
// and lets the parser walk raw bytes with zero copies. Works the same on Linux
// and macOS.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace itch {

class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0)
            throw std::runtime_error("cannot open file: " + path);
        struct stat st{};
        if (::fstat(fd_, &st) != 0) {
            ::close(fd_);
            throw std::runtime_error("fstat failed: " + path);
        }
        size_ = static_cast<size_t>(st.st_size);
        if (size_ == 0) {
            ::close(fd_);
            throw std::runtime_error("file is empty: " + path);
        }
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) {
            ::close(fd_);
            throw std::runtime_error("mmap failed: " + path);
        }
        data_ = static_cast<const uint8_t*>(p);
        // Hint sequential access so the kernel reads ahead aggressively.
        ::madvise(const_cast<uint8_t*>(data_), size_, MADV_SEQUENTIAL);
    }

    ~MappedFile() {
        if (data_)
            ::munmap(const_cast<uint8_t*>(data_), size_);
        if (fd_ >= 0)
            ::close(fd_);
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Touch every page so faults don't land inside the timed loop.
    void prefault() const {
        volatile uint8_t sink = 0;
        for (size_t i = 0; i < size_; i += 4096)
            sink ^= data_[i];
        (void)sink;
    }

    const uint8_t* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }

private:
    int fd_ = -1;
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace itch
