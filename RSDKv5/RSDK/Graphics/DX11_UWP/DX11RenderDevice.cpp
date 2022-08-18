#include "resource.h"

#if !RETRO_USE_ORIGINAL_CODE
#include <D3Dcompiler.h>
#endif

#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// Notes:
//  This backend is designed for Windows (Phone) 8.1+/Windows RT 8.1 or later.
//  Due to app model limitations, it has limited support for explicit fullscreen

// Tells NVIDIA/AMD GPU drivers to use the dedicated GPU
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

ID3D11DeviceContext *RenderDevice::dx11Context;
ID3D11Device *RenderDevice::dx11Device;
UINT RenderDevice::dxAdapter;
ID3D11Buffer *RenderDevice::dx11VertexBuffer;
ID3D11Texture2D *RenderDevice::screenTextures[SCREEN_COUNT];
ID3D11Texture2D *RenderDevice::imageTexture;
ID3D11ShaderResourceView *RenderDevice::screenTextureViews[SCREEN_COUNT];
ID3D11ShaderResourceView *RenderDevice::imageTextureView;
D3D11_VIEWPORT RenderDevice::dx11ViewPort;
IDXGISwapChain *RenderDevice::swapChain;
ID3D11RenderTargetView *RenderDevice::renderView;

ID3D11RasterizerState *RenderDevice::rasterState;
ID3D11SamplerState *RenderDevice::samplerPoint;
ID3D11SamplerState *RenderDevice::samplerLinear;
ID3D11Buffer *RenderDevice::psConstBuffer = NULL;

int32 RenderDevice::adapterCount = 0;
RECT RenderDevice::monitorDisplayRect;
LUID RenderDevice::deviceIdentifier;

D3D_DRIVER_TYPE RenderDevice::driverType     = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL RenderDevice::featureLevel = D3D_FEATURE_LEVEL_9_1;

bool RenderDevice::useFrequency = false;

std::map<uint32_t, RenderDevice::touch_t> RenderDevice::touches{};

LARGE_INTEGER RenderDevice::performanceCount;
LARGE_INTEGER RenderDevice::frequency;
LARGE_INTEGER RenderDevice::initialFrequency;
LARGE_INTEGER RenderDevice::curFrequency;

winrt::Windows::Foundation::Point RenderDevice::cusorPosition{ 0, 0 };

winrt::Windows::UI::Core::CoreWindow RenderDevice::coreWindow{ nullptr };
winrt::Windows::UI::Core::CoreDispatcher RenderDevice::coreDispatcher{ nullptr };

struct ShaderConstants {
    float2 pixelSize;
    float2 textureSize;
    float2 viewSize;
    float2 screenDim;
};

bool RenderDevice::Init()
{
    auto applicationView = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
    applicationView.Title(winrt::to_hstring(gameVerInfo.gameTitle));
    applicationView.PreferredLaunchViewSize(winrt::Windows::Foundation::Size(videoSettings.windowWidth * 2, videoSettings.windowHeight * 2));
    applicationView.PreferredLaunchWindowingMode(winrt::Windows::UI::ViewManagement::ApplicationViewWindowingMode::PreferredLaunchViewSize);

    coreWindow.PointerPressed(&RenderDevice::OnPointerPressed);
    coreWindow.PointerMoved(&RenderDevice::OnPointerMoved);
    coreWindow.PointerReleased(&RenderDevice::OnPointerReleased);

    coreWindow.KeyDown(&RenderDevice::OnKeyDown);
    coreWindow.KeyUp(&RenderDevice::OnKeyUp);

    coreWindow.SizeChanged(&RenderDevice::OnResized);

    if (!SetupRendering() || !AudioDevice::Init())
        return false;

    InitInputDevices();
    return true;
}

void RenderDevice::CopyFrameBuffer()
{
    for (int32 s = 0; s < videoSettings.screenCount; ++s) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (SUCCEEDED(dx11Context->Map(screenTextures[s], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
            WORD *pixels        = (WORD *)mappedResource.pData;
            uint16 *frameBuffer = screens[s].frameBuffer;

            int32 screenPitch = screens[s].pitch;
            int32 pitch       = (mappedResource.RowPitch >> 1) - screenPitch;

            for (int32 y = 0; y < SCREEN_YSIZE; ++y) {
                int32 pixelCount = screenPitch >> 4;
                for (int32 x = 0; x < pixelCount; ++x) {
                    pixels[0]  = frameBuffer[0];
                    pixels[1]  = frameBuffer[1];
                    pixels[2]  = frameBuffer[2];
                    pixels[3]  = frameBuffer[3];
                    pixels[4]  = frameBuffer[4];
                    pixels[5]  = frameBuffer[5];
                    pixels[6]  = frameBuffer[6];
                    pixels[7]  = frameBuffer[7];
                    pixels[8]  = frameBuffer[8];
                    pixels[9]  = frameBuffer[9];
                    pixels[10] = frameBuffer[10];
                    pixels[11] = frameBuffer[11];
                    pixels[12] = frameBuffer[12];
                    pixels[13] = frameBuffer[13];
                    pixels[14] = frameBuffer[14];
                    pixels[15] = frameBuffer[15];

                    frameBuffer += 16;
                    pixels += 16;
                }

                pixels += pitch;
            }

            dx11Context->Unmap(screenTextures[s], 0);
        }
    }
}

void RenderDevice::FlipScreen()
{
    if (windowRefreshDelay > 0) {
        if (!--windowRefreshDelay)
            UpdateGameWindow();

        return;
    }

    const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    dx11Context->RSSetViewports(1, &displayInfo.viewport);
    dx11Context->ClearRenderTargetView(renderView, clearColor);
    dx11Context->RSSetViewports(1, &dx11ViewPort);

    dx11Context->OMSetRenderTargets(1, &renderView, nullptr);
    dx11Context->RSSetState(rasterState);

    {
        // reload shader if needed
        if (lastShaderID != videoSettings.shaderID) {
            lastShaderID = videoSettings.shaderID;

            dx11Context->PSSetSamplers(0, 1, shaderList[videoSettings.shaderID].linear ? &samplerLinear : &samplerPoint);

            dx11Context->IASetInputLayout(shaderList[videoSettings.shaderID].vertexDeclare);
            dx11Context->VSSetShader(shaderList[videoSettings.shaderID].vertexShaderObject, nullptr, 0);
            dx11Context->PSSetShader(shaderList[videoSettings.shaderID].pixelShaderObject, nullptr, 0);
        }

        ShaderConstants constants[] = { pixelSize, textureSize, viewSize, { videoSettings.dimMax * videoSettings.dimPercent, 0 } };

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        ZeroMemory(&mappedResource, sizeof(mappedResource));
        dx11Context->Map(psConstBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, &constants, sizeof(ShaderConstants));
        dx11Context->Unmap(psConstBuffer, 0);

        dx11Context->PSSetConstantBuffers(0, 1, &psConstBuffer);

        int32 startVert            = 0;
        ID3D11Texture2D *const tex = screenTextures[0];

        switch (videoSettings.screenCount) {
            default:
            case 0:
#if RETRO_REV02
                startVert = 54;
#else
                startVert = 18;
#endif
                dx11Context->PSSetShaderResources(0, 1, &imageTextureView);
                dx11Context->Draw(6, startVert);
                break;

            case 1:
                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[0]);
                dx11Context->Draw(6, 0);
                break;

            case 2:
#if RETRO_REV02
                startVert = startVertex_2P[0];
#else
                startVert = 6;
#endif
                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[0]);
                dx11Context->Draw(6, startVert);

#if RETRO_REV02
                startVert = startVertex_2P[1];
#else
                startVert = 12;
#endif
                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[1]);
                dx11Context->Draw(6, startVert);
                break;

#if RETRO_REV02
            case 3:
                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[0]);
                dx11Context->Draw(6, startVertex_3P[0]);

                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[1]);
                dx11Context->Draw(6, startVertex_3P[1]);

                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[2]);
                dx11Context->Draw(6, startVertex_3P[2]);
                break;

            case 4:
                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[0]);
                dx11Context->Draw(startVert, 30);

                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[1]);
                dx11Context->Draw(startVert, 36);

                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[2]);
                dx11Context->Draw(startVert, 42);

                dx11Context->PSSetShaderResources(0, 1, &screenTextureViews[3]);
                dx11Context->Draw(startVert, 48);
                break;
#endif
        }
    }

    if (FAILED(swapChain->Present(videoSettings.vsync ? 1 : 0, 0)))
        windowRefreshDelay = 8;
}

void RenderDevice::Release(bool32 isRefresh)
{
    if (dx11Context)
        dx11Context->ClearState();

    for (int32 i = 0; i < shaderCount; ++i) {
        if (shaderList[i].vertexShaderObject)
            shaderList[i].vertexShaderObject->Release();
        shaderList[i].vertexShaderObject = NULL;

        if (shaderList[i].pixelShaderObject)
            shaderList[i].pixelShaderObject->Release();
        shaderList[i].pixelShaderObject = NULL;

        if (shaderList[i].vertexDeclare)
            shaderList[i].vertexDeclare->Release();
        shaderList[i].vertexDeclare = NULL;
    }

    shaderCount = 0;
#if RETRO_USE_MOD_LOADER
    userShaderCount = 0;
#endif

    if (imageTexture) {
        imageTexture->Release();
        imageTexture = NULL;
    }

    if (imageTextureView) {
        imageTextureView->Release();
        imageTextureView = NULL;
    }

    for (int32 i = 0; i < SCREEN_COUNT; ++i) {
        if (screenTextures[i])
            screenTextures[i]->Release();

        screenTextures[i] = NULL;

        if (screenTextureViews[i])
            screenTextureViews[i]->Release();

        screenTextureViews[i] = NULL;
    }

    if (renderView) {
        renderView->Release();
        renderView = NULL;
    }

    if (psConstBuffer) {
        psConstBuffer->Release();
        psConstBuffer = NULL;
    }

    if (samplerPoint) {
        samplerPoint->Release();
        samplerPoint = NULL;
    }

    if (samplerLinear) {
        samplerLinear->Release();
        samplerLinear = NULL;
    }

    if (rasterState) {
        rasterState->Release();
        rasterState = NULL;
    }

    if (!isRefresh && displayInfo.displays) {
        free(displayInfo.displays);
        displayInfo.displays = NULL;
    }

    if (dx11VertexBuffer) {
        dx11VertexBuffer->Release();
        dx11VertexBuffer = NULL;
    }

    if (swapChain) {
        DXGI_SWAP_CHAIN_DESC desc;
        swapChain->GetDesc(&desc);
        // it's not a good idea to release it while in fullscreen so, lets not!
        if (!desc.Windowed)
            swapChain->SetFullscreenState(FALSE, NULL);

        swapChain->Release();
        swapChain = NULL;
    }

    if (!isRefresh && dx11Device) {
        dx11Device->Release();
        dx11Device = NULL;
    }

    if (!isRefresh && dx11Context) {
        dx11Context->Release();
        dx11Context = NULL;
    }

    if (!isRefresh && scanlines) {
        free(scanlines);
        scanlines = NULL;
    }
}

void RenderDevice::RefreshWindow() { RefreshWindow(winrt::Windows::Foundation::Size(coreWindow.Bounds().Width, coreWindow.Bounds().Height)); }

void RenderDevice::RefreshWindow(winrt::Windows::Foundation::Size &size)
{
    videoSettings.windowState = WINDOWSTATE_UNINITIALIZED;
    Release(true);

    videoSettings.windowWidth  = (size.Width) / 480.0 * videoSettings.pixWidth;
    videoSettings.windowHeight = (size.Height) / 480.0 * videoSettings.pixHeight;

    if (!InitGraphicsAPI() || !InitShaders())
        return;

    videoSettings.windowState = WINDOWSTATE_ACTIVE;
}

void RenderDevice::InitFPSCap()
{
    assert(QueryPerformanceFrequency(&frequency));
    useFrequency              = true;
    initialFrequency.QuadPart = frequency.QuadPart / videoSettings.refreshRate;
    QueryPerformanceCounter(&performanceCount);
}

bool RenderDevice::CheckFPSCap()
{
    QueryPerformanceCounter(&curFrequency);
    if (curFrequency.QuadPart > performanceCount.QuadPart)
        return true;

    return false;
}

void RenderDevice::UpdateFPSCap() { performanceCount.QuadPart = curFrequency.QuadPart + initialFrequency.LowPart; }

void RenderDevice::InitVertexBuffer()
{
    RenderVertex vertBuffer[sizeof(rsdkVertexBuffer) / sizeof(RenderVertex)];
    memcpy(vertBuffer, rsdkVertexBuffer, sizeof(rsdkVertexBuffer));

    float x = 0.5 / (float)viewSize.x;
    float y = 0.5 / (float)viewSize.y;

    // ignore the last 6 verts, they're scaled to the 1024x512 textures already!
    int32 vertCount = (RETRO_REV02 ? 60 : 24) - 6;
    for (int32 v = 0; v < vertCount; ++v) {
        RenderVertex *vertex = &vertBuffer[v];
        vertex->pos.x        = vertex->pos.x - x;
        vertex->pos.y        = vertex->pos.y + y;

        if (vertex->tex.x)
            vertex->tex.x = screens[0].size.x * (1.0 / textureSize.x);

        if (vertex->tex.y)
            vertex->tex.y = screens[0].size.y * (1.0 / textureSize.y);
    }

    D3D11_MAPPED_SUBRESOURCE resource;
    if (SUCCEEDED(dx11Context->Map(dx11VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource))) {
        memcpy(resource.pData, vertBuffer, sizeof(vertBuffer));
        dx11Context->Unmap(dx11VertexBuffer, 0);
    }

    // Set/Update vertex buffer
    UINT stride = sizeof(RenderVertex);
    UINT offset = 0;
    dx11Context->IASetVertexBuffers(0, 1, &dx11VertexBuffer, &stride, &offset);

    // Init pixel shader constants
    D3D11_BUFFER_DESC cbDesc   = {};
    cbDesc.Usage               = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth           = sizeof(ShaderConstants);
    cbDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags           = 0;
    cbDesc.StructureByteStride = 0;

    dx11Device->CreateBuffer(&cbDesc, NULL, &psConstBuffer);
}

bool RenderDevice::InitGraphicsAPI()
{
    HRESULT hr = 0;

    winrt::com_ptr<IDXGIFactory1> dxgiFactory;

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    winrt::com_ptr<IDXGIDevice1> dxgiDevice1;
    winrt::com_ptr<IDXGIAdapter> dxgiAdapter;

    if (FAILED(hr = dx11Device->QueryInterface(dxgiDevice.put())))
        return false;

    if (FAILED(hr = dxgiDevice->GetAdapter(dxgiAdapter.put())))
        return false;

    if (FAILED(dxgiAdapter->GetParent(__uuidof(IDXGIFactory1), (void **)dxgiFactory.put())))
        return false;

    if (!(dxgiDevice1 = dxgiDevice.try_as<IDXGIDevice1>()))
        return false;

    dxgiDevice1->SetMaximumFrameLatency(2);

    // TODO: WinRT 8.0. Do we care?
    auto info   = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
    auto bounds = coreWindow.Bounds();

    viewSize.x = bounds.Width * (info.LogicalDpi() / 96.0);
    viewSize.y = bounds.Height * (info.LogicalDpi() / 96.0);

    winrt::com_ptr<IDXGIFactory2> dxgiFactory2 = dxgiFactory.as<IDXGIFactory2>();

    // DirectX 11.1 or later is guaranteed on Windows 8+
    if (FAILED(hr = dx11Device->QueryInterface(__uuidof(ID3D11Device1), (void **)&dx11Device)))
        return false;

    if (FAILED(hr = dx11Context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&dx11Context)))
        return false;

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width                 = viewSize.x;
    swapDesc.Height                = viewSize.y;
    swapDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.Stereo                = FALSE;
    swapDesc.SampleDesc.Count      = 1;
    swapDesc.SampleDesc.Quality    = 0;
    swapDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount           = 2;
    swapDesc.Scaling               = DXGI_SCALING_NONE;
    swapDesc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode             = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Flags                 = 0;

    winrt::com_ptr<IDXGISwapChain1> swapChain1;
    if (FAILED(hr = dxgiFactory2->CreateSwapChainForCoreWindow(dx11Device, winrt::get_unknown(coreWindow), &swapDesc, nullptr, swapChain1.put())))
        return false;

    if (FAILED(hr = swapChain1->QueryInterface(__uuidof(IDXGISwapChain), (void **)&swapChain)))
        return false;

    // Create a render target view
    winrt::com_ptr<ID3D11Texture2D> pBackBuffer;
    if (FAILED(hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), pBackBuffer.put_void())))
        return false;

    if (FAILED(hr = dx11Device->CreateRenderTargetView(pBackBuffer.get(), nullptr, &renderView)))
        return false;

    dx11Context->OMSetRenderTargets(1, &renderView, nullptr);
    dx11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage             = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth         = sizeof(RenderVertex) * ARRAYSIZE(rsdkVertexBuffer);
        desc.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(hr = dx11Device->CreateBuffer(&desc, NULL, &dx11VertexBuffer)))
            return false;
    }

    int32 maxPixHeight = 0;
    for (int32 s = 0; s < SCREEN_COUNT; ++s) {
        if (videoSettings.pixHeight > maxPixHeight)
            maxPixHeight = videoSettings.pixHeight;

        screens[s].size.y = videoSettings.pixHeight;

        float viewAspect  = viewSize.x / viewSize.y;
        int32 screenWidth = (int32)((viewAspect * videoSettings.pixHeight) + 3) & 0xFFFFFFFC;
        if (screenWidth < videoSettings.pixWidth)
            screenWidth = videoSettings.pixWidth;

#if !RETRO_USE_ORIGINAL_CODE
        if (customSettings.maxPixWidth && screenWidth > customSettings.maxPixWidth)
            screenWidth = customSettings.maxPixWidth;
#else
        if (screenWidth > DEFAULT_PIXWIDTH)
            screenWidth = DEFAULT_PIXWIDTH;
#endif

        memset(&screens[s].frameBuffer, 0, sizeof(screens[s].frameBuffer));
        SetScreenSize(s, screenWidth, screens[s].size.y);
    }

    pixelSize.x     = screens[0].size.x;
    pixelSize.y     = screens[0].size.y;
    float pixAspect = pixelSize.x / pixelSize.y;

    UINT viewportCount = 0;
    dx11Context->RSGetViewports(&viewportCount, NULL);
    if (viewportCount) {
        D3D11_VIEWPORT *viewports = new D3D11_VIEWPORT[viewportCount];
        dx11Context->RSGetViewports(&viewportCount, viewports);
        displayInfo.viewport = viewports[0];
        dx11ViewPort         = displayInfo.viewport;

        delete[] viewports;
    }
    else {
        displayInfo.viewport.TopLeftX = 0;
        displayInfo.viewport.TopLeftY = 0;
        displayInfo.viewport.Width    = viewSize.x;
        displayInfo.viewport.Height   = viewSize.y;
        displayInfo.viewport.MinDepth = 0;
        displayInfo.viewport.MaxDepth = 1;

        dx11ViewPort = displayInfo.viewport;
    }

    if ((viewSize.x / viewSize.y) <= ((pixelSize.x / pixelSize.y))) {
        viewSize.y            = (pixelSize.y / pixelSize.x) * viewSize.x;
        dx11ViewPort.TopLeftY = (displayInfo.viewport.Height * 0.5) - (viewSize.y * 0.5);
        dx11ViewPort.Height   = viewSize.y;
    }
    else {
        viewSize.x            = pixAspect * viewSize.y;
        dx11ViewPort.TopLeftX = (displayInfo.viewport.Width * 0.5) - (viewSize.x * 0.5);
        dx11ViewPort.Width    = viewSize.x;
    }

    dx11Context->RSSetViewports(1, &dx11ViewPort);

    if (maxPixHeight <= 256) {
        textureSize.x = 512.0;
        textureSize.y = 256.0;
    }
    else {
        textureSize.x = 1024.0;
        textureSize.y = 512.0;
    }

    for (int32 s = 0; s < SCREEN_COUNT; ++s) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width                = textureSize.x;
        desc.Height               = textureSize.y;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format                     = DXGI_FORMAT_B5G6R5_UNORM;
        desc.SampleDesc.Quality         = 0;
        desc.SampleDesc.Count           = 1;
        desc.Usage                      = D3D11_USAGE_DYNAMIC;
        desc.BindFlags                  = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags             = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags                  = 0;

        if (FAILED(hr = dx11Device->CreateTexture2D(&desc, NULL, &screenTextures[s])))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC resDesc;
        resDesc.Format                    = desc.Format;
        resDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        resDesc.Texture2D.MostDetailedMip = 0;
        resDesc.Texture2D.MipLevels       = 1;

        if (FAILED(hr = dx11Device->CreateShaderResourceView(screenTextures[s], &resDesc, &screenTextureViews[s])))
            return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width                = RETRO_VIDEO_TEXTURE_W;
    desc.Height               = RETRO_VIDEO_TEXTURE_H;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format                     = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Quality         = 0;
    desc.SampleDesc.Count           = 1;
    desc.Usage                      = D3D11_USAGE_DYNAMIC;
    desc.BindFlags                  = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags             = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags                  = 0;
    if (FAILED(hr = dx11Device->CreateTexture2D(&desc, NULL, &imageTexture)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC resDesc;
    resDesc.Format                    = desc.Format;
    resDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    resDesc.Texture2D.MostDetailedMip = 0;
    resDesc.Texture2D.MipLevels       = 1;

    if (FAILED(hr = dx11Device->CreateShaderResourceView(imageTexture, &resDesc, &imageTextureView)))
        return false;

    lastShaderID = -1;
    InitVertexBuffer();
    engine.inFocus          = 1;
    videoSettings.viewportX = dx11ViewPort.TopLeftX;
    videoSettings.viewportY = dx11ViewPort.TopLeftY;
    videoSettings.viewportW = 1.0 / viewSize.x;
    videoSettings.viewportH = 1.0 / viewSize.y;

    return true;
}

void RenderDevice::LoadShader(const char *name, bool32 linear)
{
    char fullFilePath[0x100];
    FileInfo info;

    for (int32 i = 0; i < shaderCount; ++i) {
        if (strcmp(shaderList[i].name, name) == 0)
            return;
    }

    if (shaderCount == SHADER_COUNT)
        return;

    ShaderEntry *shader = &shaderList[shaderCount];
    shader->linear      = linear;
    sprintf_s(shader->name, (int32)sizeof(shader->name), "%s", name);

    const D3D_SHADER_MACRO defines[] = {
#if RETRO_REV02
        "RETRO_REV02",
        "1",
#endif
        NULL,
        NULL
    };

    void *bytecode      = NULL;
    size_t bytecodeSize = 0;

    // BUGBUG:
    //  The "None" shader requires features unavailable in Shader Model 2.0, meaning devices with a feature level below 9_3
    //  can't run it. To keep the Surface RT happy we gracefully fallback to the "Clean" shader, which produces a slightly 
    //  softer image, but also means we dont have to resort to WARP.
    const char *fileName;
    if (featureLevel < D3D_FEATURE_LEVEL_9_3 && strcmp(name, "None") == 0) {
        fileName = "Clean";
    }
    else {
        fileName = name;
    }

#if !RETRO_USE_ORIGINAL_CODE && defined(DEBUG)
    // Try to compile the vertex shader source if it exists
    sprintf_s(fullFilePath, (int32)sizeof(fullFilePath), "Data/Shaders/DX11/%s.hlsl", fileName);
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint8 *fileData = NULL;
        AllocateStorage((void **)&fileData, info.fileSize, DATASET_TMP, false);
        ReadBytes(&info, fileData, info.fileSize);
        CloseFile(&info);

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        if (engine.devMenu) {
            flags |= D3DCOMPILE_DEBUG;
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
            flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
        }
        else {
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
        }

        ID3DBlob *shaderBlob = nullptr;
        ID3DBlob *errorBlob  = nullptr;
        HRESULT result =
            D3DCompile(fileData, info.fileSize, fullFilePath, defines, NULL, "VSMain", "vs_4_0_level_9_1", flags, 0, &shaderBlob, &errorBlob);

        if (FAILED(result)) {
            if (errorBlob) {
                PrintLog(PRINT_NORMAL, "ERROR COMPILING VERTEX SHADER: %s", (char *)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }

            if (shaderBlob)
                shaderBlob->Release();

            fileData = NULL;
            return;
        }
        else {
            PrintLog(PRINT_NORMAL, "Successfully compiled vertex shader!");
            if (errorBlob)
                PrintLog(PRINT_NORMAL, "Vertex shader warnings:\n%s", (char *)errorBlob->GetBufferPointer());

            if (FAILED(dx11Device->CreateVertexShader((DWORD *)shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL,
                                                      &shader->vertexShaderObject))) {
                if (shader->vertexShaderObject) {
                    shader->vertexShaderObject->Release();
                    shader->vertexShaderObject = NULL;
                }

                fileData = NULL;
                return;
            }
        }

        bytecode     = shaderBlob->GetBufferPointer();
        bytecodeSize = shaderBlob->GetBufferSize();
        fileData     = NULL;
    }
    else {
#endif
        // if the vertex shader source doesn't exist, fall back and try to load the vertex shader bytecode
        sprintf_s(fullFilePath, (int32)sizeof(fullFilePath), "Data/Shaders/CSO-DX11/%s.vso", fileName);
        InitFileInfo(&info);
        if (LoadFile(&info, fullFilePath, FMODE_RB)) {
            uint8 *fileData = NULL;
            AllocateStorage((void **)&fileData, info.fileSize, DATASET_TMP, false);
            ReadBytes(&info, fileData, info.fileSize);
            CloseFile(&info);

            if (FAILED(dx11Device->CreateVertexShader((DWORD *)fileData, info.fileSize, NULL, &shader->vertexShaderObject))) {
                if (shader->vertexShaderObject) {
                    shader->vertexShaderObject->Release();
                    shader->vertexShaderObject = NULL;
                }

                fileData = NULL;
                return;
            }

            bytecode     = fileData;
            bytecodeSize = info.fileSize;
            fileData     = NULL;
        }

#if !RETRO_USE_ORIGINAL_CODE && defined(DEBUG)
    }
#endif

    // create the vertex layout stuff using the vertex shader
    {
        D3D11_INPUT_ELEMENT_DESC elements[2];

        elements[0].SemanticName         = "SV_POSITION";
        elements[0].SemanticIndex        = 0;
        elements[0].Format               = DXGI_FORMAT_R32G32B32_FLOAT;
        elements[0].InputSlot            = 0;
        elements[0].AlignedByteOffset    = offsetof(RenderVertex, pos);
        elements[0].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
        elements[0].InstanceDataStepRate = 0;

        elements[1].SemanticName         = "TEXCOORD";
        elements[1].SemanticIndex        = 0;
        elements[1].Format               = DXGI_FORMAT_R32G32_FLOAT;
        elements[1].InputSlot            = 0;
        elements[1].AlignedByteOffset    = offsetof(RenderVertex, tex);
        elements[1].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
        elements[1].InstanceDataStepRate = 0;

        // elements[2].SemanticName         = "COLOR";
        // elements[2].SemanticIndex        = 0;
        // elements[2].Format               = DXGI_FORMAT_R32G32B32_UINT;
        // elements[2].InputSlot            = 0;
        // elements[2].AlignedByteOffset    = offsetof(RenderVertex, color);
        // elements[2].InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
        // elements[2].InstanceDataStepRate = 0;

        HRESULT res = dx11Device->CreateInputLayout(elements, ARRAYSIZE(elements), bytecode, bytecodeSize, &shader->vertexDeclare);
        if (FAILED(res))
            return;
    }

#if !RETRO_USE_ORIGINAL_CODE && defined(DEBUG)
    // Try to compile the pixel shader source if it exists
    sprintf_s(fullFilePath, (int32)sizeof(fullFilePath), "Data/Shaders/DX11/%s.hlsl", fileName);
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint8 *fileData = NULL;
        AllocateStorage((void **)&fileData, info.fileSize, DATASET_TMP, false);
        ReadBytes(&info, fileData, info.fileSize);
        CloseFile(&info);

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        if (engine.devMenu) {
            flags |= D3DCOMPILE_DEBUG;
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
            flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
        }
        else {
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
        }

        ID3DBlob *shaderBlob = nullptr;
        ID3DBlob *errorBlob  = nullptr;
        HRESULT result =
            D3DCompile(fileData, info.fileSize, fullFilePath, defines, NULL, "PSMain", "ps_4_0_level_9_1", flags, 0, &shaderBlob, &errorBlob);

        if (FAILED(result)) {
            if (errorBlob) {
                PrintLog(PRINT_NORMAL, "ERROR COMPILING PIXEL SHADER:\n%s", (char *)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }

            if (shaderBlob)
                shaderBlob->Release();
        }
        else {
            PrintLog(PRINT_NORMAL, "Successfully compiled pixel shader!");
            if (errorBlob)
                PrintLog(PRINT_NORMAL, "Pixel shader warnings:\n%s", (char *)errorBlob->GetBufferPointer());

            if (FAILED(dx11Device->CreatePixelShader((DWORD *)shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL,
                                                     &shader->pixelShaderObject))) {
                if (shader->vertexShaderObject) {
                    shader->vertexShaderObject->Release();
                    shader->vertexShaderObject = NULL;
                }

                fileData = NULL;
                return;
            }
        }

        fileData = NULL;
    }
    else {
#endif
        // if the pixel shader source doesn't exist, fall back and try to load the pixel shader bytecode
        sprintf_s(fullFilePath, (int32)sizeof(fullFilePath), "Data/Shaders/CSO-DX11/%s.fso", fileName);
        InitFileInfo(&info);
        if (LoadFile(&info, fullFilePath, FMODE_RB)) {
            uint8 *fileData = NULL;
            AllocateStorage((void **)&fileData, info.fileSize, DATASET_TMP, false);
            ReadBytes(&info, fileData, info.fileSize);
            CloseFile(&info);

            if (FAILED(dx11Device->CreatePixelShader((DWORD *)fileData, info.fileSize, NULL, &shader->pixelShaderObject))) {
                if (shader->pixelShaderObject) {
                    shader->pixelShaderObject->Release();
                    shader->pixelShaderObject = NULL;
                }

                fileData = NULL;
                return;
            }

            fileData = NULL;
        }

#if !RETRO_USE_ORIGINAL_CODE && defined(DEBUG)
    }
#endif

    shaderCount++;
}

bool RenderDevice::InitShaders()
{
    D3D11_RASTERIZER_DESC rDesc = {};

    D3D11_SAMPLER_DESC sPointDesc  = {};
    D3D11_SAMPLER_DESC sLinearDesc = {};

    // init
    rDesc.FillMode              = D3D11_FILL_SOLID;
    rDesc.CullMode              = D3D11_CULL_NONE;
    rDesc.FrontCounterClockwise = FALSE;
    rDesc.DepthBias             = D3D11_DEFAULT_DEPTH_BIAS;
    rDesc.DepthBiasClamp        = D3D11_DEFAULT_DEPTH_BIAS_CLAMP;
    rDesc.SlopeScaledDepthBias  = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rDesc.DepthClipEnable       = TRUE;
    rDesc.ScissorEnable         = FALSE;
    rDesc.MultisampleEnable     = FALSE;
    rDesc.AntialiasedLineEnable = FALSE;

    // init
    sPointDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sPointDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sPointDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sPointDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sPointDesc.MipLODBias     = 0;
    sPointDesc.MaxAnisotropy  = 1;
    sPointDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sPointDesc.BorderColor[0] = 1.0f;
    sPointDesc.BorderColor[1] = 1.0f;
    sPointDesc.BorderColor[2] = 1.0f;
    sPointDesc.BorderColor[3] = 1.0f;
    sPointDesc.MinLOD         = -FLT_MAX;
    sPointDesc.MaxLOD         = FLT_MAX;

    sLinearDesc        = sPointDesc;
    sLinearDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;

    if (FAILED(dx11Device->CreateRasterizerState(&rDesc, &rasterState))) {
        // uh oh...
    }

    if (FAILED(dx11Device->CreateSamplerState(&sPointDesc, &samplerPoint))) {
        // uh oh...
    }

    if (FAILED(dx11Device->CreateSamplerState(&sLinearDesc, &samplerLinear))) {
        // uh oh...
    }

    int32 maxShaders = 0;

    LoadShader("None", false);
    LoadShader("Clean", true);
    LoadShader("CRT-Yeetron", true);
    LoadShader("CRT-Yee64", true);

#if RETRO_USE_MOD_LOADER
    // a place for mods to load custom shaders
    RunModCallbacks(MODCB_ONSHADERLOAD, NULL);
    userShaderCount = shaderCount;
#endif

    LoadShader("YUV-420", true);
    LoadShader("YUV-422", true);
    LoadShader("YUV-444", true);
    LoadShader("RGB-Image", true);
    maxShaders = shaderCount;

    videoSettings.shaderID = videoSettings.shaderID >= maxShaders ? 0 : videoSettings.shaderID;

    return true;
}

bool RenderDevice::SetupRendering()
{
    // Init DX11 context & device
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE(driverTypes);

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                          D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1 };
    UINT numFeatureLevels             = ARRAYSIZE(featureLevels);

    HRESULT hr = 0;
    for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {
        driverType = driverTypes[driverTypeIndex];
        hr         = D3D11CreateDevice(nullptr, driverType, nullptr, createDeviceFlags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                       &dx11Device, &featureLevel, &dx11Context);

        if (SUCCEEDED(hr))
            break;
    }

    if (FAILED(hr))
        return false;

    ZeroMemory(&deviceIdentifier, sizeof(deviceIdentifier));

    GetDisplays();

    if (!InitGraphicsAPI() || !InitShaders())
        return false;

    int32 size = videoSettings.pixWidth >= SCREEN_YSIZE ? videoSettings.pixWidth : SCREEN_YSIZE;
    scanlines  = (ScanlineInfo *)malloc(size * sizeof(ScanlineInfo));
    memset(scanlines, 0, size * sizeof(ScanlineInfo));

    videoSettings.windowState = WINDOWSTATE_ACTIVE;
    videoSettings.dimMax      = 1.0;
    videoSettings.dimPercent  = 1.0;

    return true;
}

void RenderDevice::GetDisplays()
{
    std::vector<IDXGIAdapter *> adapterList = GetAdapterList();
    adapterCount                            = (int32)adapterList.size();

    uint32 prevAdapter = dxAdapter;
    for (int32 a = 0; a < adapterCount; ++a) {
        IDXGIOutput *pOutput;
        if (SUCCEEDED(adapterList[a]->EnumOutputs(0, &pOutput))) {
            DXGI_OUTPUT_DESC outputDesc;
            pOutput->GetDesc(&outputDesc);
            HMONITOR monitor = outputDesc.Monitor;

            UINT modeCount;
            pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &modeCount, nullptr);

            DXGI_MODE_DESC *descArr = new DXGI_MODE_DESC[modeCount];
            pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &modeCount, descArr);

            std::vector<DXGI_MODE_DESC> adapterModeList;
            for (UINT i = 0; i < modeCount; i++) adapterModeList.push_back(descArr[i]);
            pOutput->Release();

            displayWidth[a]  = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
            displayHeight[a] = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

            delete[] descArr;
        }
    }

    DXGI_ADAPTER_DESC adapterIdentifier;
    ZeroMemory(&adapterIdentifier, sizeof(adapterIdentifier));
    adapterList[dxAdapter]->GetDesc(&adapterIdentifier);

    // no change, don't reload anything
    if (memcmp(&deviceIdentifier, &adapterIdentifier.AdapterLuid, sizeof(deviceIdentifier)) == 0 && dxAdapter == prevAdapter)
        return;

    deviceIdentifier = adapterIdentifier.AdapterLuid;

    IDXGIOutput *pOutput;
    adapterList[dxAdapter]->EnumOutputs(0, &pOutput);

    pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, (UINT *)&displayCount, nullptr);
    if (displayInfo.displays)
        free(displayInfo.displays);

    DXGI_MODE_DESC *descArr = new DXGI_MODE_DESC[displayCount];
    pOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, (UINT *)&displayCount, descArr);

    displayInfo.displays          = (decltype(displayInfo.displays))malloc(sizeof(DXGI_MODE_DESC) * displayCount);
    int32 newDisplayCount         = 0;
    bool32 foundFullScreenDisplay = false;

    for (int32 d = 0; d < displayCount; ++d) {
        memcpy(&displayInfo.displays[newDisplayCount].internal, &descArr[d], sizeof(DXGI_MODE_DESC));

        int32 refreshRate = 0;

        DXGI_RATIONAL *rate = &displayInfo.displays[newDisplayCount].refresh_rate;
        if (rate->Numerator > 0 && rate->Denominator > 0)
            refreshRate = rate->Numerator / rate->Denominator;

        if (refreshRate >= 59 && (refreshRate <= 60 || refreshRate >= 120) && displayInfo.displays[newDisplayCount].height >= (SCREEN_YSIZE * 2)) {
            int32 prevRefreshRate = 0;
            if (d) {
                rate = &displayInfo.displays[newDisplayCount - 1].refresh_rate;

                if (rate->Numerator > 0 && rate->Denominator > 0)
                    prevRefreshRate = rate->Numerator / rate->Denominator;
            }

            // remove duplicates
            if (d && displayInfo.displays[newDisplayCount].width == displayInfo.displays[newDisplayCount - 1].width
                && displayInfo.displays[newDisplayCount].height == displayInfo.displays[newDisplayCount - 1].height
                && refreshRate == prevRefreshRate) {
                memcpy(&displayInfo.displays[newDisplayCount - 1], &displayInfo.displays[newDisplayCount], sizeof(displayInfo.displays[0]));
                --newDisplayCount;
            }
            // remove "duds"
            else if (d && refreshRate == 60 && prevRefreshRate == 59) {
                memcpy(&displayInfo.displays[newDisplayCount - 1], &displayInfo.displays[newDisplayCount], sizeof(displayInfo.displays[0]));
                --newDisplayCount;
            }

            if (videoSettings.fsWidth == displayInfo.displays[newDisplayCount].width
                && videoSettings.fsHeight == displayInfo.displays[newDisplayCount].height)
                foundFullScreenDisplay = true;

            ++newDisplayCount;
        }
    }

    delete[] descArr;

    displayCount = newDisplayCount;
    if (!foundFullScreenDisplay) {
        videoSettings.fsWidth     = 0;
        videoSettings.fsHeight    = 0;
        videoSettings.refreshRate = 60; // 0;
    }
}

void RenderDevice::GetWindowSize(int32 *width, int32 *height)
{
    auto bounds = coreWindow.Bounds();
    if (width) // why
        *width = (int)bounds.Width;
    if (height)
        *height = (int)bounds.Height;
}

bool RenderDevice::ProcessEvents()
{
    coreDispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);

    touchInfo.count = 0;
    for (auto &&touch : touches) {
        if (touch.second.state) {
            touchInfo.down[touchInfo.count] = touch.second.state;
            touchInfo.x[touchInfo.count]    = touch.second.x;
            touchInfo.y[touchInfo.count]    = touch.second.y;
            touchInfo.count++;
        }
    }

    return true;
}

winrt::Windows::Foundation::Point RenderDevice::TransformPointerPosition(winrt::Windows::Foundation::Point const &rawPosition)
{
    winrt::Windows::Foundation::Rect bounds = coreWindow.Bounds();
    winrt::Windows::Foundation::Point outputPosition{};

    /*switch (winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().CurrentOrientation()) {
        case winrt::Windows::Graphics::Display::DisplayOrientations::Portrait:
            outputPosition.X = rawPosition.X / bounds.Width;
            outputPosition.Y = rawPosition.Y / bounds.Height;
            break;
        case winrt::Windows::Graphics::Display::DisplayOrientations::PortraitFlipped:
            outputPosition.X = 1.0f - (rawPosition.X / bounds.Width);
            outputPosition.Y = 1.0f - (rawPosition.Y / bounds.Height);
            break;
        case winrt::Windows::Graphics::Display::DisplayOrientations::Landscape:
            outputPosition.X = rawPosition.Y / bounds.Height;
            outputPosition.Y = 1.0f - (rawPosition.X / bounds.Width);
            break;
        case winrt::Windows::Graphics::Display::DisplayOrientations::LandscapeFlipped:
            outputPosition.X = 1.0f - (rawPosition.Y / bounds.Height);
            outputPosition.Y = rawPosition.X / bounds.Width;
            break;
        default: break;
    }*/

    outputPosition.X = rawPosition.X / bounds.Width;
    outputPosition.Y = rawPosition.Y / bounds.Height;

    return outputPosition;
}

void RenderDevice::OnPointerPressed(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::UI::Core::PointerEventArgs const &args)
{
    auto pointerPoint = args.CurrentPoint();
    auto device       = pointerPoint.PointerDevice();
    auto position     = TransformPointerPosition(pointerPoint.Position());

    coreWindow.SetPointerCapture();

    if (device.PointerDeviceType() != winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
        if (touches.find(pointerPoint.PointerId()) == touches.end()) {
            touches.insert({ pointerPoint.PointerId(), { position.X, position.Y, 1 } });
        }
        else {
            auto &touch = touches.at(pointerPoint.PointerId());
            touch.x     = position.X;
            touch.y     = position.Y;
            touch.state = 1;
        }
    }
    else {
        touchInfo.down[0] = 1;
        touchInfo.count   = 1;
        cusorPosition     = pointerPoint.Position();
    }
}

void RenderDevice::OnPointerMoved(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::UI::Core::PointerEventArgs const &args)
{
    auto pointerPoint = args.CurrentPoint();
    auto device       = pointerPoint.PointerDevice();
    auto position     = TransformPointerPosition(pointerPoint.Position());

    if (device.PointerDeviceType() != winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
        if (touches.find(pointerPoint.PointerId()) != touches.end()) {
            auto &touch = touches.at(pointerPoint.PointerId());
            if (touch.state) {
                touch.x = position.X;
                touch.y = position.Y;
            }
        }
    }
    else {
        cusorPosition = pointerPoint.Position();
    }
}

void RenderDevice::OnPointerReleased(winrt::Windows::Foundation::IInspectable const &, winrt::Windows::UI::Core::PointerEventArgs const &args)
{
    auto pointerPoint = args.CurrentPoint();
    auto device       = pointerPoint.PointerDevice();
    auto position     = TransformPointerPosition(pointerPoint.Position());

    coreWindow.ReleasePointerCapture();

    if (device.PointerDeviceType() != winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
        if (touches.find(pointerPoint.PointerId()) != touches.end()) {
            auto &touch = touches.at(pointerPoint.PointerId());
            touch.x     = position.X;
            touch.y     = position.Y;
            touch.state = 0;
        }
    }
    else {
        touchInfo.down[0] = 0;
        touchInfo.count   = 0;
    }
}

void RenderDevice::OnKeyDown(winrt::Windows::Foundation::IInspectable const &sender, winrt::Windows::UI::Core::KeyEventArgs const &args)
{
    SKU::UpdateKeyState((int32)args.VirtualKey());
}

void RenderDevice::OnKeyUp(winrt::Windows::Foundation::IInspectable const &sender, winrt::Windows::UI::Core::KeyEventArgs const &args)
{
    SKU::ClearKeyState((int32)args.VirtualKey());
}

void RenderDevice::OnResized(winrt::Windows::UI::Core::CoreWindow const &sender, winrt::Windows::UI::Core::WindowSizeChangedEventArgs const &args)
{
    RefreshWindow();
}

void RenderDevice::ShowCursor(bool32 visible)
{
    coreWindow.PointerCursor(visible ? winrt::Windows::UI::Core::CoreCursor(winrt::Windows::UI::Core::CoreCursorType::Arrow, 0) : nullptr);
}

bool RenderDevice::GetCursorPos(Vector2 *pos)
{
    pos->x = cusorPosition.X;
    pos->y = cusorPosition.Y;
    return false;
}

void RenderDevice::SetWindowTitle()
{
    auto applicationView = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
    applicationView.Title(winrt::to_hstring(gameVerInfo.gameTitle));
}

void RenderDevice::SetupImageTexture(int32 width, int32 height, uint8 *imagePixels)
{
    if (!imagePixels)
        return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(dx11Context->Map(imageTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        DWORD *pixels = (DWORD *)mappedResource.pData;
        int32 pitch   = (mappedResource.RowPitch >> 2) - width;

        uint32 *imagePixels32 = (uint32 *)imagePixels;
        for (int32 y = 0; y < height; ++y) {
            for (int32 x = 0; x < width; ++x) {
                *pixels++ = *imagePixels32++;
            }

            pixels += pitch;
        }

        dx11Context->Unmap(imageTexture, 0);
    }
}

void RenderDevice::SetupVideoTexture_YUV420(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY, int32 strideU,
                                            int32 strideV)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(dx11Context->Map(imageTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        DWORD *pixels = (DWORD *)mappedResource.pData;
        int32 pitch   = (mappedResource.RowPitch >> 2) - width;

        // Shaders are supported! lets watch this video in full color!
        for (int32 y = 0; y < height; ++y) {
            for (int32 x = 0; x < width; ++x) {
                *pixels++ = (yPlane[x] << 16) | 0xFF000000;
            }

            pixels += pitch;
            yPlane += strideY;
        }

        pixels = (DWORD *)mappedResource.pData;
        pitch  = (mappedResource.RowPitch >> 2) - (width >> 1);
        for (int32 y = 0; y < (height >> 1); ++y) {
            for (int32 x = 0; x < (width >> 1); ++x) {
                *pixels++ |= (vPlane[x] << 0) | (uPlane[x] << 8) | 0xFF000000;
            }

            pixels += pitch;
            uPlane += strideU;
            vPlane += strideV;
        }

        dx11Context->Unmap(imageTexture, 0);
    }
}
void RenderDevice::SetupVideoTexture_YUV422(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY, int32 strideU,
                                            int32 strideV)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(dx11Context->Map(imageTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        DWORD *pixels = (DWORD *)mappedResource.pData;
        int32 pitch   = (mappedResource.RowPitch >> 2) - width;

        // Shaders are supported! lets watch this video in full color!
        for (int32 y = 0; y < height; ++y) {
            for (int32 x = 0; x < width; ++x) {
                *pixels++ = (yPlane[x] << 16) | 0xFF000000;
            }

            pixels += pitch;
            yPlane += strideY;
        }

        pixels = (DWORD *)mappedResource.pData;
        pitch  = 0; // (rect.Pitch >> 2) - (width >> 1);
        for (int32 y = 0; y < height; ++y) {
            for (int32 x = 0; x < (width >> 1); ++x) {
                *pixels++ |= (vPlane[x] << 0) | (uPlane[x] << 8) | 0xFF000000;
            }

            pixels += pitch;
            uPlane += strideU;
            vPlane += strideV;
        }

        dx11Context->Unmap(imageTexture, 0);
    }
}
void RenderDevice::SetupVideoTexture_YUV444(int32 width, int32 height, uint8 *yPlane, uint8 *uPlane, uint8 *vPlane, int32 strideY, int32 strideU,
                                            int32 strideV)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(dx11Context->Map(imageTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        DWORD *pixels = (DWORD *)mappedResource.pData;
        int32 pitch   = (mappedResource.RowPitch >> 2) - width;

        // Shaders are supported! lets watch this video in full color!
        for (int32 y = 0; y < height; ++y) {
            int32 pos1  = yPlane - vPlane;
            int32 pos2  = uPlane - vPlane;
            uint8 *pixV = vPlane;
            for (int32 x = 0; x < width; ++x) {
                *pixels++ = pixV[0] | (pixV[pos2] << 8) | (pixV[pos1] << 16) | 0xFF000000;
                pixV++;
            }

            pixels += pitch;
            yPlane += strideY;
            uPlane += strideU;
            vPlane += strideV;
        }

        dx11Context->Unmap(imageTexture, 0);
    }
}