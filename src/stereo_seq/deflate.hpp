#pragma once

// Deflate (RFC 1951) in a zlib stream (RFC 1950) for the PNG writer (png_writer.hpp), so the capture images
// need no compression library. LZ77 over the 32 KB window with a hash-chain matcher (a bounded chain, so the
// time stays near linear), and for each block of symbols whichever is smallest: dynamic Huffman codes, the
// fixed codes, or stored bytes (data that does not compress grows by a few bytes per 64 KB at most).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::stereo_seq {

enum class Compression : std::uint8_t {
    Best,   // LZ77 + Huffman as above (about 0.35 s for one 2056x2216 eye image)
    Stored, // stored blocks only: no compression, as fast as a copy
};

// A complete zlib stream of `size` bytes: the 2-byte header, the deflate blocks and the Adler-32.
std::vector<std::uint8_t>
zlibCompress(const std::uint8_t* data, std::size_t size, Compression compression = Compression::Best);

std::uint32_t adler32(const std::uint8_t* data, std::size_t size, std::uint32_t adler = 1);

} // namespace evr::stereo_seq
