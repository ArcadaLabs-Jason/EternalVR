#include "stereo_seq/deflate.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace evr::stereo_seq {

namespace {

constexpr std::size_t kWindow = 32768;
// One below the window: a chain slot (prev, indexed by position modulo the window) still belongs to every
// candidate this close, so following a chain never jumps to a newer position.
constexpr std::size_t kMaxDistance = kWindow - 1;
constexpr std::size_t kMinMatch = 4; // after PNG filtering a 3-byte match rarely beats three literals
constexpr std::size_t kMaxMatch = 258;
constexpr int kHashBits = 16;
constexpr int kMaxChain = 16;          // candidates tried per position
constexpr std::size_t kNiceMatch = 96; // a match this long ends the search
constexpr std::size_t kBlockSymbols = std::size_t{1} << 16;
constexpr std::size_t kMaxStored = 65535;
constexpr std::size_t kMaxInput = 0x7FFF0000; // positions fit an int32; anything larger is stored

constexpr int kEndOfBlock = 256;
constexpr int kLitLenCodes = 286;
constexpr int kDistCodes = 30;
constexpr int kLengthCodes = 19;
constexpr int kMaxBits = 15;
constexpr int kMaxLengthBits = 7; // the code-length alphabet's limit

constexpr std::array<std::uint16_t, 29> kLengthBase{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                    15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<std::uint8_t, kLengthCodes> kLengthOrder{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                              11, 4,  12, 3, 13, 2, 14, 1, 15};

struct Symbol {
    std::uint16_t value;    // the literal byte, or the match length
    std::uint16_t distance; // 0 for a literal
};

// A Huffman code: lengths and bit-reversed codes (deflate sends a code's top bit first).
struct Code {
    std::array<std::uint8_t, 288> lengths{};
    std::array<std::uint16_t, 288> codes{};
};

void canonicalCodes(Code& code, int count) {
    std::array<std::uint16_t, kMaxBits + 1> perLength{};
    for (int i = 0; i < count; ++i) {
        ++perLength[code.lengths[i]];
    }
    perLength[0] = 0;
    std::array<std::uint16_t, kMaxBits + 1> next{};
    std::uint32_t c = 0;
    for (int bits = 1; bits <= kMaxBits; ++bits) {
        c = (c + perLength[bits - 1]) << 1;
        next[bits] = static_cast<std::uint16_t>(c);
    }
    for (int i = 0; i < count; ++i) {
        const int length = code.lengths[i];
        if (length == 0) {
            continue;
        }
        std::uint32_t v = next[length]++;
        std::uint32_t reversed = 0;
        for (int b = 0; b < length; ++b, v >>= 1) {
            reversed = (reversed << 1) | (v & 1u);
        }
        code.codes[i] = static_cast<std::uint16_t>(reversed);
    }
}

// Huffman code lengths of at most `maxBits` for `count` symbols of `freq` (at least two used); unused
// symbols get 0. Lengths past the limit are folded back as in miniz (the Kraft sum is restored by
// lengthening the deepest shorter codes), which costs little next to an optimal limited code.
void huffmanLengths(const std::uint32_t* freq, int count, int maxBits, Code& code) {
    std::array<std::pair<std::uint32_t, int>, 288> leaves{};
    int n = 0;
    for (int i = 0; i < count; ++i) {
        code.lengths[i] = 0;
        if (freq[i] != 0) {
            leaves[n++] = {freq[i], i};
        }
    }
    std::sort(leaves.begin(), leaves.begin() + n);
    // Two queues: the sorted leaves and the internal nodes, which are made in order of weight.
    std::array<std::uint64_t, 2 * 288> weight{};
    std::array<int, 2 * 288> parent{};
    for (int i = 0; i < n; ++i) {
        weight[i] = leaves[i].first;
    }
    int nextLeaf = 0;
    int nextNode = n;
    int made = n;
    const auto smallest = [&] {
        if (nextLeaf < n && (nextNode >= made || weight[nextLeaf] <= weight[nextNode])) {
            return nextLeaf++;
        }
        return nextNode++;
    };
    while (made < 2 * n - 1) {
        const int a = smallest();
        const int b = smallest();
        weight[made] = weight[a] + weight[b];
        parent[a] = made;
        parent[b] = made;
        ++made;
    }
    std::array<int, 2 * 288> depth{};
    std::array<std::uint32_t, kMaxBits + 1> perLength{};
    for (int i = 2 * n - 3; i >= 0; --i) {
        depth[i] = depth[parent[i]] + 1;
    }
    for (int i = 0; i < n; ++i) {
        ++perLength[std::min(depth[i], maxBits)];
    }
    std::uint32_t kraft = 0;
    for (int bits = 1; bits <= maxBits; ++bits) {
        kraft += perLength[bits] << (maxBits - bits);
    }
    while (kraft > (1u << maxBits)) {
        --perLength[maxBits];
        for (int bits = maxBits - 1; bits > 0; --bits) {
            if (perLength[bits] != 0) {
                --perLength[bits];
                perLength[bits + 1] += 2;
                break;
            }
        }
        --kraft;
    }
    // The least frequent symbols take the longest codes.
    int at = 0;
    for (int bits = maxBits; bits > 0; --bits) {
        for (std::uint32_t k = 0; k < perLength[bits]; ++k) {
            code.lengths[leaves[at++].second] = static_cast<std::uint8_t>(bits);
        }
    }
    canonicalCodes(code, count);
}

// A code needs two used symbols to be complete (some decoders refuse a one-symbol code).
void useTwo(std::uint32_t* freq, int count) {
    int used = 0;
    for (int i = 0; i < count; ++i) {
        used += freq[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < count && used < 2; ++i) {
        if (freq[i] == 0) {
            freq[i] = 1;
            ++used;
        }
    }
}

struct Tables {
    std::array<std::uint8_t, kMaxMatch + 1> lengthCode{}; // by match length: the code minus 257
    std::array<std::uint8_t, 512> distCode{};             // see distCodeOf
    Code fixedLitLen;
    Code fixedDist;
};

const Tables& tables() {
    static const Tables t = [] {
        Tables s;
        for (int c = 0; c < 29; ++c) {
            for (int i = 0; i < (1 << kLengthExtra[c]); ++i) {
                const std::size_t length = kLengthBase[c] + static_cast<std::size_t>(i);
                if (length <= kMaxMatch) {
                    s.lengthCode[length] = static_cast<std::uint8_t>(c);
                }
            }
        }
        s.lengthCode[kMaxMatch] = 28; // 258 has its own code, not code 284's last value
        for (int c = 0; c < kDistCodes; ++c) {
            for (int i = 0; i < (1 << kDistExtra[c]); ++i) {
                const int d = kDistBase[c] + i - 1;
                s.distCode[d < 256 ? d : 256 + (d >> 7)] = static_cast<std::uint8_t>(c);
            }
        }
        for (int i = 0; i < 288; ++i) {
            s.fixedLitLen.lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        }
        canonicalCodes(s.fixedLitLen, 288);
        for (int i = 0; i < 32; ++i) {
            s.fixedDist.lengths[i] = 5;
        }
        canonicalCodes(s.fixedDist, 32);
        return s;
    }();
    return t;
}

// Codes from 256 on cover multiples of 128 (base - 1), so distances past 256 are looked up by d >> 7.
int distCodeOf(const Tables& t, std::uint32_t distance) {
    const std::uint32_t d = distance - 1;
    return t.distCode[d < 256 ? d : 256 + (d >> 7)];
}

class BitWriter {
public:
    explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    // At most 32 bits.
    void put(std::uint32_t bits, int count) {
        acc_ |= std::uint64_t{bits} << used_;
        used_ += count;
        if (used_ >= 32) {
            for (int i = 0; i < 4; ++i, acc_ >>= 8) {
                out_.push_back(static_cast<std::uint8_t>(acc_));
            }
            used_ -= 32;
        }
    }

    // Pads to a whole byte and writes out every pending bit.
    void align() {
        while (used_ > 0) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
            acc_ >>= 8;
            used_ = std::max(used_ - 8, 0);
        }
        acc_ = 0;
    }

    int pendingBits() const { return used_ & 7; }
    std::vector<std::uint8_t>& bytes() { return out_; }

private:
    std::vector<std::uint8_t>& out_;
    std::uint64_t acc_ = 0;
    int used_ = 0;
};

// The code-length alphabet's tokens for a dynamic block's literal/length and distance lengths (one run,
// repeats may cross from one table into the other).
struct LengthToken {
    std::uint8_t symbol;
    std::uint8_t extra;
};

std::vector<LengthToken> lengthTokens(const std::uint8_t* lengths, int count) {
    std::vector<LengthToken> tokens;
    for (int i = 0; i < count;) {
        const std::uint8_t v = lengths[i];
        int run = 1;
        while (i + run < count && lengths[i + run] == v) {
            ++run;
        }
        i += run;
        if (v == 0) {
            for (; run >= 11; run -= std::min(run, 138)) {
                tokens.push_back({18, static_cast<std::uint8_t>(std::min(run, 138) - 11)});
            }
            if (run >= 3) {
                tokens.push_back({17, static_cast<std::uint8_t>(run - 3)});
                run = 0;
            }
        } else {
            tokens.push_back({v, 0});
            --run;
            for (; run >= 3; run -= std::min(run, 6)) {
                tokens.push_back({16, static_cast<std::uint8_t>(std::min(run, 6) - 3)});
            }
        }
        for (; run > 0; --run) {
            tokens.push_back({v, 0});
        }
    }
    return tokens;
}

constexpr std::array<std::uint8_t, kLengthCodes> kTokenExtraBits{0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                                 0, 0, 0, 0, 0, 0, 2, 3, 7};

class Deflater {
public:
    Deflater(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out)
        : data_(data), size_(size), bits_(out) {}

    void run(Compression compression) {
        if (compression == Compression::Stored || size_ > kMaxInput) {
            writeStored(0, size_, true);
            bits_.align();
            return;
        }
        std::vector<std::int32_t> head(std::size_t{1} << kHashBits, -1);
        std::vector<std::int32_t> prev(kWindow, -1);
        std::vector<Symbol> symbols;
        symbols.reserve(kBlockSymbols);
        std::size_t blockStart = 0;
        std::size_t pos = 0;
        const auto insert = [&](std::size_t p) {
            const std::uint32_t h = hashAt(p);
            prev[p & (kWindow - 1)] = head[h];
            head[h] = static_cast<std::int32_t>(p);
        };
        while (pos < size_) {
            std::size_t bestLength = 0;
            std::size_t bestDistance = 0;
            if (pos + kMinMatch <= size_) {
                std::int32_t candidate = head[hashAt(pos)];
                insert(pos);
                const std::size_t maxLength = std::min(kMaxMatch, size_ - pos);
                const std::uint8_t* here = data_ + pos;
                for (int chain = kMaxChain; candidate >= 0 && chain > 0; --chain) {
                    const std::size_t distance = pos - static_cast<std::size_t>(candidate);
                    if (distance > kMaxDistance) {
                        break;
                    }
                    const std::uint8_t* there = data_ + candidate;
                    if (there[bestLength] == here[bestLength] && load32(there) == load32(here)) {
                        const std::size_t length = matchLength(there, here, maxLength);
                        if (length > bestLength) {
                            bestLength = length;
                            bestDistance = distance;
                            if (length >= kNiceMatch || length == maxLength) {
                                break;
                            }
                        }
                    }
                    candidate = prev[static_cast<std::size_t>(candidate) & (kWindow - 1)];
                }
            }
            if (bestLength >= kMinMatch) {
                symbols.push_back(
                    {static_cast<std::uint16_t>(bestLength), static_cast<std::uint16_t>(bestDistance)});
                const std::size_t end = pos + bestLength;
                for (++pos; pos < end; ++pos) {
                    if (pos + kMinMatch <= size_) {
                        insert(pos);
                    }
                }
            } else {
                symbols.push_back({data_[pos], 0});
                ++pos;
            }
            if (symbols.size() == kBlockSymbols && pos < size_) {
                writeBlock(symbols, blockStart, pos, false);
                symbols.clear();
                blockStart = pos;
            }
        }
        writeBlock(symbols, blockStart, size_, true);
        bits_.align();
    }

private:
    static std::uint32_t load32(const std::uint8_t* p) {
        // Byte by byte, so the hash (and the output) is the same on every platform.
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
               (std::uint32_t{p[3]} << 24);
    }

    std::uint32_t hashAt(std::size_t p) const {
        return (load32(data_ + p) * 2654435761u) >> (32 - kHashBits);
    }

    static std::size_t matchLength(const std::uint8_t* a, const std::uint8_t* b, std::size_t maxLength) {
        std::size_t n = 0;
        for (; n + 8 <= maxLength; n += 8) {
            if (std::memcmp(a + n, b + n, 8) != 0) {
                break;
            }
        }
        while (n < maxLength && a[n] == b[n]) {
            ++n;
        }
        return n;
    }

    void writeStored(std::size_t begin, std::size_t end, bool last) {
        do {
            const std::size_t n = std::min(kMaxStored, end - begin);
            const bool final = last && begin + n == end;
            bits_.put(final ? 1u : 0u, 3); // BFINAL, BTYPE 00
            bits_.align();
            auto& out = bits_.bytes();
            const auto len = static_cast<std::uint16_t>(n);
            const auto nlen = static_cast<std::uint16_t>(~len);
            const std::uint8_t header[4] = {
                static_cast<std::uint8_t>(len & 0xFFu), static_cast<std::uint8_t>(len >> 8),
                static_cast<std::uint8_t>(nlen & 0xFFu), static_cast<std::uint8_t>(nlen >> 8)};
            out.insert(out.end(), header, header + 4);
            out.insert(out.end(), data_ + begin, data_ + begin + n);
            begin += n;
        } while (begin < end);
    }

    void writeSymbols(const std::vector<Symbol>& symbols, const Code& litLen, const Code& dist) {
        const Tables& t = tables();
        for (const Symbol& s : symbols) {
            if (s.distance == 0) {
                bits_.put(litLen.codes[s.value], litLen.lengths[s.value]);
                continue;
            }
            const int lc = t.lengthCode[s.value];
            bits_.put(litLen.codes[257 + lc] |
                          (static_cast<std::uint32_t>(s.value - kLengthBase[lc]) << litLen.lengths[257 + lc]),
                      litLen.lengths[257 + lc] + kLengthExtra[lc]);
            const int dc = distCodeOf(t, s.distance);
            bits_.put(dist.codes[dc] |
                          (static_cast<std::uint32_t>(s.distance - kDistBase[dc]) << dist.lengths[dc]),
                      dist.lengths[dc] + kDistExtra[dc]);
        }
        bits_.put(litLen.codes[kEndOfBlock], litLen.lengths[kEndOfBlock]);
    }

    // One block of `symbols`, which stand for the bytes [begin, end): dynamic, fixed or stored, whichever
    // is smallest.
    void writeBlock(const std::vector<Symbol>& symbols, std::size_t begin, std::size_t end, bool last) {
        const Tables& t = tables();
        std::array<std::uint32_t, kLitLenCodes> litFreq{};
        std::array<std::uint32_t, kDistCodes> distFreq{};
        std::uint64_t extraBits = 0;
        for (const Symbol& s : symbols) {
            if (s.distance == 0) {
                ++litFreq[s.value];
                continue;
            }
            const int lc = t.lengthCode[s.value];
            const int dc = distCodeOf(t, s.distance);
            ++litFreq[257 + lc];
            ++distFreq[dc];
            extraBits += kLengthExtra[lc] + kDistExtra[dc];
        }
        litFreq[kEndOfBlock] = 1;

        const auto dataBits = [&](const Code& litLen, const Code& dist) {
            std::uint64_t bits = extraBits;
            for (int i = 0; i < kLitLenCodes; ++i) {
                bits += std::uint64_t{litFreq[i]} * litLen.lengths[i];
            }
            for (int i = 0; i < kDistCodes; ++i) {
                bits += std::uint64_t{distFreq[i]} * dist.lengths[i];
            }
            return bits;
        };
        const std::uint64_t fixedBits = 3 + dataBits(t.fixedLitLen, t.fixedDist);

        Code litLen;
        Code dist;
        useTwo(litFreq.data(), kLitLenCodes);
        useTwo(distFreq.data(), kDistCodes);
        huffmanLengths(litFreq.data(), kLitLenCodes, kMaxBits, litLen);
        huffmanLengths(distFreq.data(), kDistCodes, kMaxBits, dist);
        int hlit = kLitLenCodes;
        while (hlit > 257 && litLen.lengths[hlit - 1] == 0) {
            --hlit;
        }
        int hdist = kDistCodes;
        while (hdist > 1 && dist.lengths[hdist - 1] == 0) {
            --hdist;
        }
        std::array<std::uint8_t, kLitLenCodes + kDistCodes> all{};
        std::copy_n(litLen.lengths.begin(), hlit, all.begin());
        std::copy_n(dist.lengths.begin(), hdist, all.begin() + hlit);
        const auto tokens = lengthTokens(all.data(), hlit + hdist);
        std::array<std::uint32_t, kLengthCodes> tokenFreq{};
        for (const LengthToken& token : tokens) {
            ++tokenFreq[token.symbol];
        }
        useTwo(tokenFreq.data(), kLengthCodes);
        Code lengthCode;
        huffmanLengths(tokenFreq.data(), kLengthCodes, kMaxLengthBits, lengthCode);
        int hclen = kLengthCodes;
        while (hclen > 4 && lengthCode.lengths[kLengthOrder[hclen - 1]] == 0) {
            --hclen;
        }
        std::uint64_t dynamicBits =
            3 + 5 + 5 + 4 + 3 * static_cast<std::uint64_t>(hclen) + dataBits(litLen, dist);
        for (const LengthToken& token : tokens) {
            dynamicBits += lengthCode.lengths[token.symbol] + kTokenExtraBits[token.symbol];
        }

        const std::size_t stored = end - begin;
        const std::uint64_t storedBits =
            8 * std::uint64_t{stored} + 40 * std::uint64_t{(stored + kMaxStored - 1) / kMaxStored + 1};
        if (storedBits <= std::min(fixedBits, dynamicBits)) {
            writeStored(begin, end, last);
        } else if (fixedBits <= dynamicBits) {
            bits_.put((last ? 1u : 0u) | (1u << 1), 3);
            writeSymbols(symbols, t.fixedLitLen, t.fixedDist);
        } else {
            bits_.put((last ? 1u : 0u) | (2u << 1), 3);
            bits_.put(static_cast<std::uint32_t>(hlit - 257), 5);
            bits_.put(static_cast<std::uint32_t>(hdist - 1), 5);
            bits_.put(static_cast<std::uint32_t>(hclen - 4), 4);
            for (int i = 0; i < hclen; ++i) {
                bits_.put(lengthCode.lengths[kLengthOrder[i]], 3);
            }
            for (const LengthToken& token : tokens) {
                bits_.put(lengthCode.codes[token.symbol], lengthCode.lengths[token.symbol]);
                if (kTokenExtraBits[token.symbol] != 0) {
                    bits_.put(token.extra, kTokenExtraBits[token.symbol]);
                }
            }
            writeSymbols(symbols, litLen, dist);
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    BitWriter bits_;
};

} // namespace

std::uint32_t adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler) {
    std::uint32_t a = adler & 0xFFFFu;
    std::uint32_t b = adler >> 16;
    constexpr std::uint32_t kMod = 65521;
    while (size > 0) {
        const std::size_t n = std::min<std::size_t>(size, 5552); // no overflow before the modulo
        for (std::size_t i = 0; i < n; ++i) {
            a += data[i];
            b += a;
        }
        a %= kMod;
        b %= kMod;
        data += n;
        size -= n;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> zlibCompress(const std::uint8_t* data, std::size_t size, Compression compression) {
    const bool stored = compression == Compression::Stored;
    std::vector<std::uint8_t> out;
    out.reserve((stored ? size + size / kMaxStored * 5 : size / 2) + 64);
    out.push_back(0x78);                 // deflate, 32 KB window
    out.push_back(stored ? 0x01 : 0x5E); // level: fastest or fast; the header's check bits
    Deflater(data, size, out).run(compression);
    const std::uint32_t check = adler32(data, size);
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(check >> shift));
    }
    return out;
}

} // namespace evr::stereo_seq
