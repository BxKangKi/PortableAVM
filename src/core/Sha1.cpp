#include "core/Sha1.h"
#include "core/StringUtil.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace pavm {
namespace {

std::uint32_t rotateLeft(std::uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

class Sha1 {
public:
    Sha1() { reset(); }

    void update(const void* input, std::size_t length) {
        const auto* bytes = static_cast<const std::uint8_t*>(input);
        totalBytes_ += length;
        while (length > 0) {
            const std::size_t available = buffer_.size() - bufferSize_;
            const std::size_t copied = length < available ? length : available;
            std::memcpy(buffer_.data() + bufferSize_, bytes, copied);
            bufferSize_ += copied;
            bytes += copied;
            length -= copied;
            if (bufferSize_ == buffer_.size()) {
                transform(buffer_.data());
                bufferSize_ = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t totalBits = static_cast<std::uint64_t>(totalBytes_) * 8ULL;
        const std::uint8_t marker = 0x80;
        update(&marker, 1);
        const std::uint8_t zero = 0;
        while (bufferSize_ != 56) {
            update(&zero, 1);
        }
        std::array<std::uint8_t, 8> lengthBytes{};
        for (int i = 0; i < 8; ++i) {
            lengthBytes[7 - i] = static_cast<std::uint8_t>((totalBits >> (i * 8)) & 0xffU);
        }
        update(lengthBytes.data(), lengthBytes.size());

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (std::uint32_t value : state_) {
            out << std::setw(8) << value;
        }
        return out.str();
    }

private:
    void reset() {
        state_ = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
        buffer_.fill(0);
        bufferSize_ = 0;
        totalBytes_ = 0;
    }

    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 80> words{};
        for (int i = 0; i < 16; ++i) {
            const int offset = i * 4;
            words[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(block[offset]) << 24U) |
                (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            words[static_cast<std::size_t>(i)] = rotateLeft(
                words[static_cast<std::size_t>(i - 3)] ^ words[static_cast<std::size_t>(i - 8)] ^
                words[static_cast<std::size_t>(i - 14)] ^ words[static_cast<std::size_t>(i - 16)], 1);
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const std::uint32_t temp = rotateLeft(a, 5) + f + e + k + words[static_cast<std::size_t>(i)];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<std::uint32_t, 5> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_ = 0;
    std::size_t totalBytes_ = 0;
};

} // namespace

std::string sha1File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open file for SHA-1: " + pathToUtf8(path));
    }
    Sha1 hash;
    std::array<char, 1024 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    return hash.finish();
}

std::string sha1Bytes(const void* data, std::size_t size) {
    Sha1 hash;
    hash.update(data, size);
    return hash.finish();
}

} // namespace pavm
