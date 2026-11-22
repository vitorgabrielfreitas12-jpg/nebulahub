#include "controller.h"
#include <orbis/libkernel.h>
#include <cstdlib>

Controller::Controller(OrbisUserServiceUserId userId) {
    padHandle_ = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
}

Controller::~Controller() {
    if (padHandle_ >= 0) scePadClose(padHandle_);
}

void Controller::poll() {
    prev_ = cur_;
    analogPress_ = 0;
    if (padHandle_ < 0 || scePadReadState(padHandle_, &cur_) < 0 || !cur_.connected) cur_ = {};
    constexpr uint32_t dpad = ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_RIGHT |
                              ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_DOWN;
    if (!cur_.connected || (cur_.buttons & dpad)) {
        analogDirection_ = 0;
        return;
    }
    const int x = cur_.leftStick.x - 128;
    const int y = cur_.leftStick.y - 128;
    const int threshold = analogDirection_ ? 38 : 57;
    uint32_t direction = 0;
    if (std::abs(x) >= threshold && std::abs(x) >= std::abs(y))
        direction = x < 0 ? ORBIS_PAD_BUTTON_LEFT : ORBIS_PAD_BUTTON_RIGHT;
    else if (std::abs(y) >= threshold)
        direction = y < 0 ? ORBIS_PAD_BUTTON_UP : ORBIS_PAD_BUTTON_DOWN;

    const auto now = sceKernelGetProcessTime();
    if (direction && (direction != analogDirection_ || now >= nextAnalogPress_)) {
        analogPress_ = direction;
        nextAnalogPress_ = now + (direction == analogDirection_ ? 140000 : 350000);
    }
    analogDirection_ = direction;
}

bool Controller::pressed(uint32_t btn) const {
    return (analogPress_ & btn) || ((cur_.buttons & btn) && !(prev_.buttons & btn));
}

bool Controller::held(uint32_t btn) const {
    return (cur_.buttons & btn) != 0;
}

bool Controller::released(uint32_t btn) const {
    return !(cur_.buttons & btn) && (prev_.buttons & btn);
}
