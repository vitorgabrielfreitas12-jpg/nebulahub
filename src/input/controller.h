#pragma once
#include "../nebula.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>

class Controller {
public:
    explicit Controller(OrbisUserServiceUserId userId);
    ~Controller();

    void poll();

    bool pressed(uint32_t btn)  const; // true only on frame btn went down
    bool held(uint32_t btn)     const; // true while held
    bool released(uint32_t btn) const;

private:
    int            padHandle_ = -1;
    OrbisPadData   cur_{};
    OrbisPadData   prev_{};
    uint32_t analogDirection_ = 0;
    uint32_t analogPress_ = 0;
    uint64_t nextAnalogPress_ = 0;
};
