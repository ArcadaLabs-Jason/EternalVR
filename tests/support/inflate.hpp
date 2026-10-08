#pragma once

// A plain reference decoder for the deflate and PNG tests: zlib streams (RFC 1950) of stored, fixed and
// dynamic deflate blocks (RFC 1951), and PNG scanline unfiltering. Written for clarity, not speed; any
// malformed input makes it report failure instead of reading out of range.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::test {

struct Inflated {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    int stored = 0; // blocks of each type
    int fixed = 0;
    int dynamic = 0;
    std::uint32_t adler = 0; // the stream's trailer
};

namespace inflate_detail {

class Bits {
public:
    Bits(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    // -1 past the end.
    int bit() {
        if (at_ >= size_ * 8) {
            return -1;
        }
        const int b = (data_[at_ >> 3] >> (at_ & 7)) & 1;
        ++at_;
        return b;
    }

    // LSB first; -1 past the end.
    long bits(int n) {
        long v = 0;
        for (int i = 0; i < n; ++i) {
            const int b = bit();
            if (b < 0) {
                return -1;
            }
            v |= static_cast<long>(b) << i;
        }
        return v;
    }

    void align() { at_ = (at_ + 7) & ~std::size_t{7}; }
    std::size_t byteAt() const { return at_ >> 3; }
    void skipBytes(std::size_t n) { at_ += n * 8; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
};

// A canonical Huffman code, decoded one bit at a time (as in zlib's puff.c).
struct Huffman {
    std::array<int, 16> count{};
    std::vector<int> symbols;
    bool complete = false; // every bit pattern is a code (some decoders refuse anything else)

    // False for an over-subscribed set of lengths.
    bool build(const std::uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) {
            ++count[lengths[i]];
        }
        count[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left = left * 2 - count[len];
            if (left < 0) {
                return false;
            }
        }
        complete = left == 0;
        std::array<int, 16> offsets{};
        for (int len = 1; len < 15; ++len) {
            offsets[len + 1] = offsets[len] + count[len];
        }
        symbols.assign(n, 0);
        for (int i = 0; i < n; ++i) {
            if (lengths[i] != 0) {
                symbols[offsets[lengths[i]]++] = i;
            }
        }
        return true;
    }

    // -1 for no code.
    int decode(Bits& in) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            const int b = in.bit();
            if (b < 0) {
                return -1;
            }
            code |= b;
            const int c = count[len];
            if (code - c < first) {
                return symbols[index + (code - first)];
            }
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr std::array<int, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                          31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistBase{1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                        33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

inline bool codes(Bits& in, const Huffman& litLen, const Huffman& dist, std::vector<std::uint8_t>& out) {
    for (;;) {
        const int symbol = litLen.decode(in);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 256) {
            out.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) {
            return true;
        }
        const int lc = symbol - 257;
        if (lc >= 29) {
            return false;
        }
        const long lengthExtra = in.bits(kLengthExtra[lc]);
        const int dc = dist.decode(in);
        if (lengthExtra < 0 || dc < 0 || dc >= 30) {
            return false;
        }
        const long distExtra = in.bits(kDistExtra[dc]);
        if (distExtra < 0) {
            return false;
        }
        const std::size_t length = kLengthBase[lc] + static_cast<std::size_t>(lengthExtra);
        const std::size_t distance = kDistBase[dc] + static_cast<std::size_t>(distExtra);
        if (distance > out.size() || distance > 32768) {
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            out.push_back(out[out.size() - distance]);
        }
    }
}

} // namespace inflate_detail

inline Inflated inflateZlib(const std::vector<std::uint8_t>& z) {
    using namespace inflate_detail;
    Inflated result;
    if (z.size() < 6 || (z[0] & 0x0F) != 8 || (z[0] >> 4) > 7 || ((z[0] << 8) | z[1]) % 31 != 0 ||
        (z[1] & 0x20)) {
        return result;
    }
    Bits in(z.data() + 2, z.size() - 2);
    std::vector<std::uint8_t>& out = result.bytes;
    for (;;) {
        const long last = in.bits(1);
        const long type = in.bits(2);
        if (last < 0 || type < 0) {
            return result;
        }
        if (type == 0) {
            in.align();
            const std::size_t at = 2 + in.byteAt();
            if (at + 4 > z.size()) {
                return result;
            }
            const std::size_t len = z[at] | (z[at + 1] << 8);
            const std::size_t nlen = z[at + 2] | (z[at + 3] << 8);
            if ((len ^ 0xFFFFu) != nlen || at + 4 + len > z.size()) {
                return result;
            }
            out.insert(out.end(), z.begin() + static_cast<std::ptrdiff_t>(at + 4),
                       z.begin() + static_cast<std::ptrdiff_t>(at + 4 + len));
            in.skipBytes(4 + len);
            ++result.stored;
        } else if (type == 1) {
            std::array<std::uint8_t, 288> lit{};
            for (int i = 0; i < 288; ++i) {
                lit[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            }
            std::array<std::uint8_t, 30> dist{};
            dist.fill(5);
            Huffman litCode;
            Huffman distCode;
            litCode.build(lit.data(), 288);
            distCode.build(dist.data(), 30);
            if (!codes(in, litCode, distCode, out)) {
                return result;
            }
            ++result.fixed;
        } else if (type == 2) {
            const long hlit = in.bits(5);
            const long hdist = in.bits(5);
            const long hclen = in.bits(4);
            if (hlit < 0 || hdist < 0 || hclen < 0 || hlit + 257 > 286 || hdist + 1 > 30) {
                return result;
            }
            constexpr std::array<int, 19> order{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                11, 4,  12, 3, 13, 2, 14, 1, 15};
            std::array<std::uint8_t, 19> clLengths{};
            for (long i = 0; i < hclen + 4; ++i) {
                const long v = in.bits(3);
                if (v < 0) {
                    return result;
                }
                clLengths[order[i]] = static_cast<std::uint8_t>(v);
            }
            Huffman clCode;
            if (!clCode.build(clLengths.data(), 19) || !clCode.complete) {
                return result;
            }
            const int total = static_cast<int>(hlit + 257 + hdist + 1);
            std::vector<std::uint8_t> lengths;
            while (static_cast<int>(lengths.size()) < total) {
                const int symbol = clCode.decode(in);
                if (symbol < 0) {
                    return result;
                }
                if (symbol < 16) {
                    lengths.push_back(static_cast<std::uint8_t>(symbol));
                    continue;
                }
                std::uint8_t value = 0;
                long repeat = 0;
                if (symbol == 16) {
                    if (lengths.empty()) {
                        return result;
                    }
                    value = lengths.back();
                    repeat = 3 + in.bits(2);
                } else if (symbol == 17) {
                    repeat = 3 + in.bits(3);
                } else {
                    repeat = 11 + in.bits(7);
                }
                if (repeat < 3 || static_cast<long>(lengths.size()) + repeat > total) {
                    return result;
                }
                lengths.insert(lengths.end(), static_cast<std::size_t>(repeat), value);
            }
            if (lengths[256] == 0) {
                return result; // no end-of-block code
            }
            Huffman litCode;
            Huffman distCode;
            if (!litCode.build(lengths.data(), static_cast<int>(hlit + 257)) ||
                !distCode.build(lengths.data() + hlit + 257, static_cast<int>(hdist + 1)) ||
                !litCode.complete || !distCode.complete) {
                return result;
            }
            if (!codes(in, litCode, distCode, out)) {
                return result;
            }
            ++result.dynamic;
        } else {
            return result;
        }
        if (last == 1) {
            break;
        }
    }
    in.align();
    const std::size_t at = 2 + in.byteAt();
    if (at + 4 != z.size()) {
        return result;
    }
    result.adler = (std::uint32_t{z[at]} << 24) | (std::uint32_t{z[at + 1]} << 16) |
                   (std::uint32_t{z[at + 2]} << 8) | z[at + 3];
    result.ok = true;
    return result;
}

// PNG scanlines (each a filter type byte and `rowBytes` filtered bytes) back to tightly packed rows; empty
// for an unknown filter type or a size that does not match.
inline std::vector<std::uint8_t>
unfilterPng(const std::vector<std::uint8_t>& raw, std::size_t rowBytes, std::size_t height, std::size_t bpp) {
    std::vector<std::uint8_t> out(rowBytes * height);
    if (raw.size() != (rowBytes + 1) * height) {
        return {};
    }
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t type = raw[y * (rowBytes + 1)];
        const std::uint8_t* in = raw.data() + y * (rowBytes + 1) + 1;
        std::uint8_t* row = out.data() + y * rowBytes;
        const std::uint8_t* up = y > 0 ? row - rowBytes : nullptr;
        for (std::size_t i = 0; i < rowBytes; ++i) {
            const int a = i >= bpp ? row[i - bpp] : 0;
            const int b = up ? up[i] : 0;
            const int c = up && i >= bpp ? up[i - bpp] : 0;
            int predicted = 0;
            switch (type) {
            case 0:
                break;
            case 1:
                predicted = a;
                break;
            case 2:
                predicted = b;
                break;
            case 3:
                predicted = (a + b) / 2;
                break;
            case 4: {
                const int p = a + b - c;
                const int pa = p > a ? p - a : a - p;
                const int pb = p > b ? p - b : b - p;
                const int pc = p > c ? p - c : c - p;
                predicted = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            }
            default:
                return {};
            }
            row[i] = static_cast<std::uint8_t>(in[i] + predicted);
        }
    }
    return out;
}

} // namespace evr::test
