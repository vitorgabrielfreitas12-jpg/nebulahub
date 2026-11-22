#include "controller.h"
#include <orbis/libkernel.h>

Controller::Controller(OrbisUserServiceUserId userId) {
    padHandle_ = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
}

Controller::~Controller() {
    if (padHandle_ >= 0) scePadClose(padHandle_);
}

void Controller::poll() {
    prev_ = cur_;
    if (padHandle_ < 0 || scePadReadState(padHandle_, &cur_) < 0) cur_ = {};
}

bool Controller::pressed(uint32_t btn) const {
    return (cur_.buttons & btn) && !(prev_.buttons & btn);
}

bool Controller::held(uint32_t btn) const {
    return (cur_.buttons & btn) != 0;
}

bool Controller::released(uint32_t btn) const {
    return !(cur_.buttons & btn) && (prev_.buttons & btn);
}

float Controller::leftX() const {
    return (cur_.leftStick.x - 128) / 128.f;
}

float Controller::leftY() const {
    return (cur_.leftStick.y - 128) / 128.f;
}

float Controller::rightX() const {
    return (cur_.rightStick.x - 128) / 128.f;
}

float Controller::rightY() const {
    return (cur_.rightStick.y - 128) / 128.f;
}

bool Controller::repeat(uint32_t btn) const {
    if (pressed(btn)) return true;
    if (!held(btn)) return false;
    // Fire every ~150ms after 300ms initial delay
    // Using frame count approximation (60fps → 9 frames delay, 9 frame repeat)
    // Not using system time here to keep it simple
    return false; // callers handle fine-grained repeat with their own timers
}
