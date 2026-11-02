#include <windows.h>
#include <bcrypt.h>
#include "output/CatBoxProtocol.hpp"
#include <algorithm>
#include <string>
#pragma comment(lib, "bcrypt.lib")
namespace V2::Output::CatBoxProtocol {
bool DeriveKey(std::string_view uuid, Key& key) {
    if (uuid.empty() || uuid.size() > 8) return false;
    std::string normalized(8 - uuid.size(), '0');
    for (char c : uuid) {
        if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        normalized += c;
    }
    normalized += "00000000";
    std::copy(normalized.begin(), normalized.end(), key.begin());
    return true;
}
static bool Crypt(bool encrypt, const std::vector<uint8_t>& in, const Key& key,
                  Key iv, std::vector<uint8_t>& out) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_KEY_HANDLE handle = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
            sizeof(BCRYPT_CHAIN_MODE_CBC), 0) >= 0 &&
        BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0,
            const_cast<PUCHAR>(key.data()), 16, 0) >= 0) {
        out.resize(in.size() + 16); ULONG count = 0;
        const auto status = encrypt
            ? BCryptEncrypt(handle, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()), nullptr,
                iv.data(), 16, out.data(), static_cast<ULONG>(out.size()), &count, BCRYPT_BLOCK_PADDING)
            : BCryptDecrypt(handle, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()), nullptr,
                iv.data(), 16, out.data(), static_cast<ULONG>(out.size()), &count, BCRYPT_BLOCK_PADDING);
        ok = status >= 0;
        if (ok) out.resize(count); else out.clear();
    }
    if (handle) BCryptDestroyKey(handle);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
std::vector<uint8_t> Encode(Command c, const Key& key, const Key& iv) {
    const auto x = static_cast<uint16_t>(c.x), y = static_cast<uint16_t>(c.y);
    std::vector<uint8_t> plain = {c.code, uint8_t(c.options), uint8_t(c.options >> 8),
        uint8_t(x), uint8_t(x >> 8), uint8_t(y), uint8_t(y >> 8)}, encrypted;
    if (!Crypt(true, plain, key, iv, encrypted)) return {};
    std::vector<uint8_t> result(iv.begin(), iv.end());
    result.insert(result.end(), encrypted.begin(), encrypted.end());
    return result;
}
bool Decode(const std::vector<uint8_t>& packet, const Key& key, std::vector<uint8_t>& plain) {
    plain.clear();
    if (packet.size() < 32 || packet.size() > 1024 || packet.size() % 16) return false;
    Key iv{}; std::copy_n(packet.begin(), 16, iv.begin());
    return Crypt(false, {packet.begin() + 16, packet.end()}, key, iv, plain);
}
uint16_t HidToLinux(uint8_t u) {
    constexpr uint16_t letters[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    if (u >= 4 && u <= 29) return letters[u - 4];
    if (u >= 30 && u <= 38) return u - 28;
    if (u == 39) return 11;
    if (u >= 58 && u <= 67) return u + 1;
    if (u == 68 || u == 69) return u + 19;
    constexpr uint16_t modifiers[] = {29,42,56,125,97,54,100,126};
    if (u >= 224 && u <= 231) return modifiers[u - 224];
    switch (u) {
    case 40:return 28; case 41:return 1; case 42:return 14; case 43:return 15; case 44:return 57;
    case 45:return 12; case 46:return 13; case 47:return 26; case 48:return 27; case 49:return 43;
    case 50:return 86; case 51:return 39; case 52:return 40; case 53:return 41; case 54:return 51;
    case 55:return 52; case 56:return 53; case 57:return 58; case 70:return 99; case 71:return 70;
    case 72:return 119; case 73:return 110; case 74:return 102; case 75:return 104; case 76:return 111;
    case 77:return 107; case 78:return 109; case 79:return 106; case 80:return 105; case 81:return 108;
    case 82:return 103; case 83:return 69; case 84:return 98; case 85:return 55; case 86:return 74;
    case 87:return 78; case 88:return 96; case 89:return 79; case 90:return 80; case 91:return 81;
    case 92:return 75; case 93:return 76; case 94:return 77; case 95:return 71; case 96:return 72;
    case 97:return 73; case 98:return 82; case 99:return 83; default:return 0;
    }
}
}
