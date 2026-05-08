#include "sha256.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

class AlgorithmHandle {
  public:
    AlgorithmHandle() {
        const NTSTATUS status = BCryptOpenAlgorithmProvider(&handle_, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0) {
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed.");
        }
    }

    ~AlgorithmHandle() {
        if (handle_ != nullptr) {
            BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    BCRYPT_ALG_HANDLE get() const {
        return handle_;
    }

  private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class HashHandle {
  public:
    HashHandle(BCRYPT_ALG_HANDLE algorithm, const std::vector<unsigned char>& objectBuffer) : objectBuffer_(objectBuffer) {
        const NTSTATUS status = BCryptCreateHash(algorithm, &handle_, objectBuffer_.data(), static_cast<ULONG>(objectBuffer_.size()), nullptr, 0, 0);
        if (status < 0) {
            throw std::runtime_error("BCryptCreateHash failed.");
        }
    }

    ~HashHandle() {
        if (handle_ != nullptr) {
            BCryptDestroyHash(handle_);
        }
    }

    BCRYPT_HASH_HANDLE get() const {
        return handle_;
    }

  private:
    BCRYPT_HASH_HANDLE handle_ = nullptr;
    std::vector<unsigned char> objectBuffer_;
};

ULONG GetAlgorithmUlongProperty(BCRYPT_ALG_HANDLE algorithm, const wchar_t* property) {
    ULONG value = 0;
    ULONG bytesWritten = 0;
    const NTSTATUS status = BCryptGetProperty(algorithm, property, reinterpret_cast<PUCHAR>(&value), sizeof(value), &bytesWritten, 0);
    if (status < 0 || bytesWritten != sizeof(value)) {
        throw std::runtime_error("BCryptGetProperty ULONG read failed.");
    }

    return value;
}

std::string BytesToHex(const std::vector<unsigned char>& bytes) {
    std::ostringstream builder;
    builder.setf(std::ios::hex, std::ios::basefield);
    builder.fill('0');

    for (unsigned char value : bytes) {
        builder.width(2);
        builder << static_cast<int>(value);
    }

    return builder.str();
}

}  // namespace

namespace recovery {

std::string ComputeFileSha256(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file for hashing.");
    }

    AlgorithmHandle algorithm;
    const ULONG objectLength = GetAlgorithmUlongProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH);
    const ULONG hashLength = GetAlgorithmUlongProperty(algorithm.get(), BCRYPT_HASH_LENGTH);
    std::vector<unsigned char> objectBuffer(objectLength, 0);
    HashHandle hash(algorithm.get(), objectBuffer);

    std::array<char, 1 << 16> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }

        const NTSTATUS status = BCryptHashData(hash.get(), reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0);
        if (status < 0) {
            throw std::runtime_error("BCryptHashData failed.");
        }
    }

    std::vector<unsigned char> digest(hashLength, 0);
    const NTSTATUS finishStatus = BCryptFinishHash(hash.get(), digest.data(), hashLength, 0);
    if (finishStatus < 0) {
        throw std::runtime_error("BCryptFinishHash failed.");
    }

    return BytesToHex(digest);
}

}  // namespace recovery
