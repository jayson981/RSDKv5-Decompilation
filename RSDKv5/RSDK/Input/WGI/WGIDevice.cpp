
using namespace RSDK;

RSDK::SKU::InputDeviceWGI::InputDeviceWGI(winrt::Windows::Gaming::Input::Gamepad const &pad) { this->gamepad = pad; }

void RSDK::SKU::InputDeviceWGI::UpdateInput()
{
    using namespace winrt::Windows::Gaming::Input;

    auto reading = gamepad.GetCurrentReading();

    this->activeState ^= 1;
    GamepadButtons changedButtons = ~lastReading.Buttons & (lastReading.Buttons ^ reading.Buttons);

    if ((uint8)changedButtons)
        this->inactiveTimer[0] = 0;
    else
        ++this->inactiveTimer[0];

    if ((bool)(changedButtons & GamepadButtons::A) || (bool)(changedButtons & GamepadButtons::Menu))
        this->inactiveTimer[1] = 0;
    else
        ++this->inactiveTimer[1];

    this->anyPress = (uint8)changedButtons;
    this->stateUp       = (reading.Buttons & GamepadButtons::DPadUp) != (GamepadButtons)0;
    this->stateDown     = (reading.Buttons & GamepadButtons::DPadDown) != (GamepadButtons)0;
    this->stateLeft     = (reading.Buttons & GamepadButtons::DPadLeft) != (GamepadButtons)0;
    this->stateRight    = (reading.Buttons & GamepadButtons::DPadRight) != (GamepadButtons)0;
    this->stateA        = (reading.Buttons & GamepadButtons::A) != (GamepadButtons)0;
    this->stateB        = (reading.Buttons & GamepadButtons::B) != (GamepadButtons)0;
    this->stateX        = (reading.Buttons & GamepadButtons::X) != (GamepadButtons)0;
    this->stateY        = (reading.Buttons & GamepadButtons::Y) != (GamepadButtons)0;
    this->stateStart    = (reading.Buttons & GamepadButtons::Menu) != (GamepadButtons)0;;
    this->stateSelect   = (reading.Buttons & GamepadButtons::View) != (GamepadButtons)0;;
    this->stateBumper_L = (reading.Buttons & GamepadButtons::LeftShoulder) != (GamepadButtons)0;
    this->stateBumper_R = (reading.Buttons & GamepadButtons::RightShoulder) != (GamepadButtons)0;;
    this->stateStick_L  = (reading.Buttons & GamepadButtons::LeftThumbstick) != (GamepadButtons)0;
    this->stateStick_R  = (reading.Buttons & GamepadButtons::RightThumbstick) != (GamepadButtons)0;

    this->hDelta_L = reading.LeftThumbstickX * 32767.0;
    this->vDelta_L = reading.LeftThumbstickY * 32767.0;

    float div      = sqrtf((this->hDelta_L * this->hDelta_L) + (this->vDelta_L * this->vDelta_L));
    this->hDelta_L = this->hDelta_L / div;
    this->vDelta_L = this->vDelta_L / div;
    if (div <= 7864.0) {
        this->hDelta_L = 0.0;
        this->vDelta_L = 0.0;
    }
    else {
        this->hDelta_L = this->hDelta_L * ((fminf(32767.0, div) - 7864.0) / 24903.0);
        this->vDelta_L = this->vDelta_L * ((fminf(32767.0, div) - 7864.0) / 24903.0);
    }

    this->hDelta_R = reading.RightThumbstickX * 32767.0;
    this->vDelta_R = reading.RightThumbstickY * 32767.0;

    div            = sqrtf((this->hDelta_R * this->hDelta_R) + (this->vDelta_R * this->vDelta_R));
    this->hDelta_R = this->hDelta_R / div;
    this->vDelta_R = this->vDelta_R / div;
    if (div <= 7864.0) {
        this->hDelta_R = 0.0;
        this->vDelta_R = 0.0;
    }
    else {
        this->hDelta_R = this->hDelta_R * ((fminf(32767.0, div) - 7864.0) / 24903.0);
        this->vDelta_R = this->vDelta_R * ((fminf(32767.0, div) - 7864.0) / 24903.0);
    }

    this->deltaBumper_L = this->stateBumper_L ? 1.0 : 0.0;

    this->deltaTrigger_L = reading.LeftTrigger * 256;
    if (this->deltaTrigger_L <= 30.0)
        this->deltaTrigger_L = 0.0;
    else
        this->deltaTrigger_L = (this->deltaTrigger_L - 30.0) / 225.0;

    this->deltaBumper_R = this->stateBumper_R ? 1.0 : 0.0;

    this->deltaTrigger_R = reading.RightTrigger * 256;
    if (this->deltaTrigger_R <= 30.0)
        this->deltaTrigger_R = 0.0;
    else
        this->deltaTrigger_R = (this->deltaTrigger_R - 30.0) / 225.0;

    this->ProcessInput(CONT_ANY);
}

void RSDK::SKU::InputDeviceWGI::ProcessInput(int32 controllerID)
{
    controller[controllerID].keyUp.press |= this->stateUp;
    controller[controllerID].keyDown.press |= this->stateDown;
    controller[controllerID].keyLeft.press |= this->stateLeft;
    controller[controllerID].keyRight.press |= this->stateRight;
    controller[controllerID].keyA.press |= this->stateA;
    controller[controllerID].keyB.press |= this->stateB;
    controller[controllerID].keyX.press |= this->stateX;
    controller[controllerID].keyY.press |= this->stateY;
    controller[controllerID].keyStart.press |= this->stateStart;
    controller[controllerID].keySelect.press |= this->stateSelect;

#if RETRO_REV02
    stickL[controllerID].keyStick.press |= this->stateStick_L;
    stickL[controllerID].hDelta = this->hDelta_L;
    stickL[controllerID].vDelta = this->vDelta_L;
    stickL[controllerID].keyUp.press |= this->vDelta_L > INPUT_DEADZONE;
    stickL[controllerID].keyDown.press |= this->vDelta_L < -INPUT_DEADZONE;
    stickL[controllerID].keyLeft.press |= this->hDelta_L < -INPUT_DEADZONE;
    stickL[controllerID].keyRight.press |= this->hDelta_L > INPUT_DEADZONE;

    stickR[controllerID].keyStick.press |= this->stateStick_R;
    stickR[controllerID].hDelta = this->hDelta_R;
    stickR[controllerID].vDelta = this->vDelta_R;
    stickR[controllerID].keyUp.press |= this->vDelta_R > INPUT_DEADZONE;
    stickR[controllerID].keyDown.press |= this->vDelta_R < -INPUT_DEADZONE;
    stickR[controllerID].keyLeft.press |= this->hDelta_R < -INPUT_DEADZONE;
    stickR[controllerID].keyRight.press |= this->hDelta_R > INPUT_DEADZONE;

    triggerL[controllerID].keyBumper.press |= this->stateBumper_L;
    triggerL[controllerID].bumperDelta  = this->deltaBumper_L;
    triggerL[controllerID].triggerDelta = this->deltaTrigger_L;

    triggerR[controllerID].keyBumper.press |= this->stateBumper_R;
    triggerR[controllerID].bumperDelta  = this->deltaBumper_R;
    triggerR[controllerID].triggerDelta = this->deltaTrigger_R;
#else
    controller[controllerID].keyStickL.press |= this->stateStick_L;
    stickL[controllerID].hDeltaL = this->hDelta_L;
    stickL[controllerID].vDeltaL = this->vDelta_L;

    stickL[controllerID].keyUp.press |= this->vDelta_L > INPUT_DEADZONE;
    stickL[controllerID].keyDown.press |= this->vDelta_L < -INPUT_DEADZONE;
    stickL[controllerID].keyLeft.press |= this->hDelta_L < -INPUT_DEADZONE;
    stickL[controllerID].keyRight.press |= this->hDelta_L > INPUT_DEADZONE;

    controller[controllerID].keyStickR.press |= this->stateStick_R;
    stickL[controllerID].hDeltaL = this->hDelta_R;
    stickL[controllerID].vDeltaL = this->vDelta_R;

    controller[controllerID].keyBumperL.press |= this->stateBumper_L;
    stickL[controllerID].deadzone      = this->deltaBumper_L;
    stickL[controllerID].triggerDeltaL = this->deltaTrigger_L;

    controller[controllerID].keyBumperR.press |= this->stateBumper_R;
    stickL[controllerID].triggerDeltaR = this->deltaTrigger_R;
#endif
}

void RSDK::SKU::OnGamepadAdded(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::Gaming::Input::Gamepad const &gamepad)
{
    UpdateWGIDevices();
}

void RSDK::SKU::OnGamepadRemoved(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::Gaming::Input::Gamepad const &gamepad)
{
    UpdateWGIDevices();
}

void RSDK::SKU::InitWGIAPI()
{
    winrt::Windows::Gaming::Input::Gamepad::GamepadAdded(&OnGamepadAdded);
    winrt::Windows::Gaming::Input::Gamepad::GamepadRemoved(&OnGamepadRemoved);

    UpdateWGIDevices();
}

void RSDK::SKU::UpdateWGIDevices()
{
    auto gamepads = winrt::Windows::Gaming::Input::Gamepad::Gamepads();
    for (int32 d = 0; d < inputDeviceCount; ++d) {
        InputDeviceWGI *device = NULL;
        if (!inputDeviceList[d] || (inputDeviceList[d]->gamepadType >> 16) != DEVICE_API_WGI)
            continue;

        device     = (InputDeviceWGI *)inputDeviceList[d];
        bool found = false;
        for (auto &&gamepad : gamepads) {
            if (gamepad == device->gamepad) {
                found = true;
                break;
            }
        }

        if (!found) {
            RemoveInputDevice(device);
        }
    }

    bool found = false;
    for (uint32_t i = 0; i < gamepads.Size(); i++) {
        auto &&gamepad = gamepads.GetAt(i);
        for (int32 d = 0; d < inputDeviceCount; ++d) {
            InputDeviceWGI *device = NULL;
            if (!inputDeviceList[d] || (inputDeviceList[d]->gamepadType >> 16) != DEVICE_API_WGI)
                continue;
            if (gamepad == device->gamepad) {
                found = true;
                break;
            }
        }

        if (!found) {
            AddWGIDevice(gamepad, i);
        }
    }
}

void RSDK::SKU::AddWGIDevice(winrt::Windows::Gaming::Input::Gamepad const &gamepad, uint32_t id)
{
    if (inputDeviceCount == INPUTDEVICE_COUNT)
        return;

    if (inputDeviceList[inputDeviceCount] && inputDeviceList[inputDeviceCount]->active)
        return;

    if (inputDeviceList[inputDeviceCount])
        delete inputDeviceList[inputDeviceCount];

    char idString[16];

    sprintf_s(idString, (int32)sizeof(idString), "%s", "XInputDevice0");
    idString[12] = '0' + id;

    uint32 crc;
    GenerateHashCRC(&crc, idString);

    inputDeviceList[inputDeviceCount] = new InputDeviceWGI(gamepad);
    InputDeviceWGI *device            = (InputDeviceWGI *)inputDeviceList[inputDeviceCount];

    for (int32 i = 0; i < PLAYER_COUNT; ++i) disabledXInputDevices[i] = false;

    device->gamepadType = (DEVICE_API_WGI << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (DEVICE_XBOX << 0);
    device->disabled    = false;
    device->id          = crc;
    device->active      = true;

    for (int32 i = 0; i < PLAYER_COUNT; ++i) {
        if (inputSlots[i] == crc) {
            inputSlotDevices[i] = device;
            device->isAssigned  = true;
        }
    }

    inputDeviceCount++;
}