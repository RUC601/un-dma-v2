#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace V2::Output::CatBoxProtocol {
using Key = std::array<uint8_t, 16>;
struct Command { uint8_t code = 0; uint16_t options = 0; int16_t x = 0, y = 0; };
bool DeriveKey(std::string_view uuid, Key& key);
std::vector<uint8_t> Encode(Command command, const Key& key, const Key& iv);
bool Decode(const std::vector<uint8_t>& packet, const Key& key, std::vector<uint8_t>& plain);
// USB HID usages and CAT's Linux input-event codes are distinct number spaces.
uint16_t HidToLinux(uint8_t usage);
}
