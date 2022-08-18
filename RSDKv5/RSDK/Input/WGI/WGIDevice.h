
namespace SKU
{

struct InputDeviceWGI : InputDevice {

    InputDeviceWGI(winrt::Windows::Gaming::Input::Gamepad const &gamepad);

    void UpdateInput();
    void ProcessInput(int32 controllerID);

    winrt::Windows::Gaming::Input::Gamepad gamepad{ nullptr };
    winrt::Windows::Gaming::Input::GamepadReading lastReading{};

    uint8 activeState;
    uint8 controllerID;
    uint8 stateUp;
    uint8 stateDown;
    uint8 stateLeft;
    uint8 stateRight;
    uint8 stateA;
    uint8 stateB;
    uint8 stateX;
    uint8 stateY;
    uint8 stateStart;
    uint8 stateSelect;
    uint8 stateBumper_L;
    uint8 stateBumper_R;
    uint8 stateStick_L;
    uint8 stateStick_R;
    int32 unused;
    float hDelta_L;
    float vDelta_L;
    float hDelta_R;
    float vDelta_R;
    float deltaBumper_L;
    float deltaTrigger_L;
    float deltaBumper_R;
    float deltaTrigger_R;
};

void OnGamepadAdded(winrt::Windows::Foundation::IInspectable const &sender, winrt::Windows::Gaming::Input::Gamepad const &gamepad);
void OnGamepadRemoved(winrt::Windows::Foundation::IInspectable const &sender, winrt::Windows::Gaming::Input::Gamepad const &gamepad);

void InitWGIAPI();
void UpdateWGIDevices();
void AddWGIDevice(winrt::Windows::Gaming::Input::Gamepad const &gamepad, uint32_t idx);

} // namespace SKU