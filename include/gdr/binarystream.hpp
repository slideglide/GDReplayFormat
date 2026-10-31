#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace detail {
    /// @brief Reverse the bytes of a value.
    template <size_t N>
    inline std::array<uint8_t, N> reverseBytes(std::array<uint8_t, N> raw) noexcept {
        if constexpr (N == 4) {
            return std::bit_cast<std::array<uint8_t, N>>(std::byteswap(std::bit_cast<uint32_t>(raw)));
        } else if constexpr (N == 8) {
            return std::bit_cast<std::array<uint8_t, N>>(std::byteswap(std::bit_cast<uint64_t>(raw)));
        } else {
            std::ranges::reverse(raw);
            return raw;
        }
    }

    /// @brief Append the bytes of any type.
    template <typename T>
    inline void appendBytes(std::vector<uint8_t>& out, const T& data) noexcept {
        auto raw = std::bit_cast<std::array<uint8_t, sizeof(T)>>(data);

        if constexpr (std::endian::native == std::endian::little) {
            raw = reverseBytes(raw);
        }

        out.insert(out.end(), raw.begin(), raw.end());
    }

    /// @brief Append an integral type in a variable-length format.
    template <typename T> requires std::is_integral_v<T>
    inline void appendBytes(std::vector<uint8_t>& out, const T& data) noexcept {
        uint64_t value = data;

        while (value > 0x7F) {
            out.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
            value >>= 7;
        }
        out.push_back(static_cast<uint8_t>(value));
    }

    /// @brief Append a string with a prefixed length.
    inline void appendBytes(std::vector<uint8_t>& out, const std::string& data) noexcept {
        appendBytes(out, data.size());
        out.insert(out.end(), data.begin(), data.end());
    }

    template <size_t N>
    inline void appendBytes(std::vector<uint8_t>& out, const char (&data)[N]) noexcept {
        out.insert(out.end(), data, data + N - 1);
    }

    /// @brief Read a value from a span of bytes and return the value and the size of the value.
    template <typename T>
    inline size_t consume(T& result, std::span<uint8_t> data) noexcept {
        if (data.size() < sizeof(T)) return 0;

        std::array<uint8_t, sizeof(T)> raw;
        std::ranges::copy_n(data.begin(), sizeof(T), raw.begin());

        if constexpr (std::endian::native == std::endian::little) {
            raw = reverseBytes(raw);
        }

        result = std::bit_cast<T>(raw);

        return sizeof(T);
    }

    /// @brief Read a variable-length value from a span of bytes and return the value and the size of the value.
    template <typename T> requires std::is_integral_v<T>
    inline size_t consume(T& result, std::span<uint8_t> data) noexcept {
        uint64_t value = 0;
        size_t size = 0;
        for (size_t i = 0; i < 10; i++) {
            if (i >= data.size()) return 0;
            value |= static_cast<uint64_t>(data[i] & 0x7F) << (i * 7);
            size++;
            if ((data[i] & 0x80) == 0) {
                break;
            }
        }
        result = static_cast<T>(value);
        return size;
    }

    /// @brief Read a string with a prefixed length from a span of bytes and return the value and the size of the value.
    inline size_t consume(std::string& result, std::span<uint8_t> data) noexcept {
        size_t stringLen = 0;
        size_t sizeLen = consume(stringLen, data);

        if (data.size() < sizeLen + stringLen) return 0;
        if (stringLen > 0xFFFF) return 0;

        result.assign(reinterpret_cast<const char*>(data.data() + sizeLen), stringLen);
        return sizeLen + stringLen;
    }

    template <size_t S>
    inline size_t consume(std::array<char, S>& result, std::span<uint8_t> data) noexcept {
        if (data.size() < S) return 0;
        std::memcpy(result.data(), data.data(), S);
        return S;
    }
}

class binary_reader {
public:
    explicit binary_reader(std::span<uint8_t> data) noexcept : m_data(data) {}

    template <typename T>
    binary_reader& operator>>(T& data) noexcept {
        auto count = detail::consume(data, {m_data.data(), m_data.size()});
        m_data = m_data.subspan(count);
        return *this;
    }

    template <typename T>
    void read(T& data) noexcept {
        auto count = detail::consume(data, {m_data.data(), m_data.size()});
        m_data = m_data.subspan(count);
    }

    void read(void* data, size_t size) noexcept {
        if (size > m_data.size()) {
            m_data = {};
            return;
        }

        std::memcpy(data, m_data.data(), size);
        m_data = m_data.subspan(size);
    }

    void read(std::vector<uint8_t>& data, size_t size) noexcept {
        if (size > m_data.size()) {
            m_data = {};
            return;
        }

        data = std::vector(m_data.begin(), m_data.begin() + static_cast<int32_t>(size));
        m_data = m_data.subspan(size);
    }

    void skip(size_t size) noexcept {
        if (size > m_data.size()) {
            m_data = {};
            return;
        }

        m_data = m_data.subspan(size);
    }

    [[nodiscard]] std::span<uint8_t> peek(size_t size) const noexcept {
        return m_data.first(size);
    }

    [[nodiscard]] uint8_t peek() const noexcept {
        return m_data.front();
    }

    [[nodiscard]] size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }

    [[nodiscard]] std::span<uint8_t> data() const noexcept { return m_data; }

protected:
    std::span<uint8_t> m_data;
};

class binary_writer {
public:
    binary_writer() noexcept = default;

    template <typename T>
    binary_writer& operator<<(const T& data) noexcept {
        detail::appendBytes(m_data, data);
        return *this;
    }

    void write(const uint8_t* data, size_t size) noexcept {
        m_data.insert(m_data.end(), data, data + size);
    }

    template <typename T>
    void write(const T& data) noexcept {
        detail::appendBytes(m_data, data);
    }

    void write(const std::vector<uint8_t>& data) noexcept {
        m_data.insert(m_data.end(), data.begin(), data.end());
    }

    void clear() noexcept { m_data.clear(); }
    [[nodiscard]] size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }

    [[nodiscard]] const std::vector<uint8_t>& data() const noexcept { return m_data; }
    std::vector<uint8_t>& data() noexcept { return m_data; }
    std::vector<uint8_t> release() noexcept { return std::move(m_data); }

    bool save(const std::filesystem::path& path) const noexcept {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(m_data.data()), static_cast<int32_t>(m_data.size()));
        return true;
    }

private:
    std::vector<uint8_t> m_data;
};
