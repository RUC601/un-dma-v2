#pragma once
#include "output/OutputTypes.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace V2::Output {
class CatBoxBackend {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    CatBoxBackend();
    ~CatBoxBackend();
    int Initialize(const std::string& ip, uint16_t port, const std::string& uuid, uint16_t monitorPort);
    OutputConnectionState GetConnectionState() const;
    int Move(int x, int y, int duration = 0);
    int Button(int button, bool down);
    int Keyboard(uint8_t modifiers, const std::vector<uint8_t>& usages, OutputIntent intent);
    int Mask(uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion = {});
    int ReleaseAllOutputAndWait(std::chrono::milliseconds timeout);
    int Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    PhysicalInputSnapshot PhysicalInput() const;
};
inline CatBoxBackend CatBoxMgr;
}
