#pragma once

// Private, dependency-free SHA-256 for identifying the exact serialized engine.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace scn::algorithm::detail
{
inline std::string sha256(const std::uint8_t* data, std::size_t size)
{
    if (size > (std::numeric_limits<std::uint64_t>::max)() / 8)
        throw std::runtime_error("Engine is too large for SHA-256");

    constexpr std::array<std::uint32_t, 64> constants{{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    }};
    std::array<std::uint32_t, 8> hash{{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    }};
    const auto rotate = [](std::uint32_t value, unsigned shift) {
        return (value >> shift) | (value << (32 - shift));
    };
    const auto compress = [&](const std::uint8_t* block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            words[i] = (std::uint32_t{block[4 * i]} << 24)
                     | (std::uint32_t{block[4 * i + 1]} << 16)
                     | (std::uint32_t{block[4 * i + 2]} << 8)
                     | std::uint32_t{block[4 * i + 3]};
        }
        for (std::size_t i = 16; i < words.size(); ++i)
        {
            const auto x = words[i - 15];
            const auto y = words[i - 2];
            const auto s0 = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
            const auto s1 = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto a = hash[0]; auto b = hash[1]; auto c = hash[2]; auto d = hash[3];
        auto e = hash[4]; auto f = hash[5]; auto g = hash[6]; auto h = hash[7];
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto t1 = h + s1 + choose + constants[i] + words[i];
            const auto s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + majority;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    };

    std::size_t position = 0;
    while (size - position >= 64)
    {
        compress(data + position);
        position += 64;
    }
    std::array<std::uint8_t, 128> tail{};
    const auto remaining = size - position;
    for (std::size_t i = 0; i < remaining; ++i)
        tail[i] = data[position + i];
    tail[remaining] = 0x80;
    const std::size_t tailSize = remaining < 56 ? 64 : 128;
    const auto bits = static_cast<std::uint64_t>(size) * 8;
    for (std::size_t i = 0; i < 8; ++i)
        tail[tailSize - 1 - i] = static_cast<std::uint8_t>(bits >> (i * 8));
    compress(tail.data());
    if (tailSize == 128)
        compress(tail.data() + 64);

    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t i = 0; i < hash.size(); ++i)
        for (std::size_t nibble = 0; nibble < 8; ++nibble)
            result[i * 8 + nibble] = digits[(hash[i] >> ((7 - nibble) * 4)) & 0xf];
    return result;
}
} // namespace scn::algorithm::detail
