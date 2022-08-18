#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Input.h>

#include "RSDK/Core/RetroEngine.hpp" 
#include "main.hpp"

using namespace winrt;

using namespace Windows;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Composition;

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
    IFrameworkView CreateView()
    {
        return *this;
    }

    void Initialize(CoreApplicationView const &)
    {
    }

    void Load(hstring const&)
    {
    }

    void Uninitialize()
    {
    }

    void Run()
    {
        CoreWindow window = CoreWindow::GetForCurrentThread();
        window.Activate();

        RSDK::RenderDevice::coreWindow = window;
        RSDK::RenderDevice::coreDispatcher = window.Dispatcher();
        RSDK_main(0, nullptr, (void *)RSDK::LinkGameLogic);
    }

    void SetWindow(CoreWindow const & window)
    { 
    }
};

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(make<App>());
}
