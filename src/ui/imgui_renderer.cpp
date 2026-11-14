#include "imgui_renderer.h"
#include "../utils/log.h"
#include "../../res/resource.h"

#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include <dxgi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr wchar_t kWindowClassName[] = L"XwaArm64SettingsImGuiWnd";

template <typename T>
void safeRelease(T *&ptr)
{
    if (ptr)
    {
        ptr->Release();
        ptr = nullptr;
    }
}

float queryDpiScaleForWindow(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn && hwnd)
        {
            const UINT dpi = fn(hwnd);
            if (dpi > 0)
            {
                return static_cast<float>(dpi) / 96.0f;
            }
        }
    }

    HMONITOR monitor = hwnd
        ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (monitor)
    {
        const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
        if (scale > 0.0f)
        {
            return scale;
        }
    }
    return 1.0f;
}

float queryDpiScaleForMonitor(HMONITOR monitor)
{
    if (monitor)
    {
        const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
        if (scale > 0.0f)
        {
            return scale;
        }
    }
    return 1.0f;
}

RECT adjustWindowRectForDpiCompat(RECT clientRect, DWORD style, DWORD exStyle, UINT dpi)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using AdjustForDpiFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto fn = reinterpret_cast<AdjustForDpiFn>(
            GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        if (fn && fn(&clientRect, style, FALSE, exStyle, dpi))
        {
            return clientRect;
        }
    }
    AdjustWindowRectEx(&clientRect, style, FALSE, exStyle);
    return clientRect;
}

void applyDarkTitleBarAttributes(HWND hwnd)
{
    BOOL darkMode = TRUE;
    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacy = 19;
    if (FAILED(DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &darkMode, sizeof(darkMode))))
    {
        DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkModeLegacy, &darkMode, sizeof(darkMode));
    }

    // Blend Windows 11 title bar caption with the ImGui dark slate background (#101418).
    constexpr DWORD kDwmCaptionColor = 35;
    COLORREF captionColor = RGB(16, 20, 24);
    DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &captionColor, sizeof(captionColor));
}

} // namespace

ImGuiRenderer::~ImGuiRenderer()
{
    destroyWindow();
}

void ImGuiRenderer::enableProcessDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(HANDLE);
        auto fn = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (DPI_AWARENESS_CONTEXT)-4
        if (fn && fn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))))
        {
            return;
        }
    }
    ImGui_ImplWin32_EnableDpiAwareness();
}

bool ImGuiRenderer::createWindow(
    const wchar_t *title,
    int widthDip,
    int heightDip,
    int minWidthDip,
    int minHeightDip,
    std::function<void()> onPaint)
{
    destroyWindow();

    m_minWidthDip = minWidthDip;
    m_minHeightDip = minHeightDip;
    m_onPaint = std::move(onPaint);
    m_closeRequested = false;
    m_firstFramePresented = false;
    m_warmupFrames = 0;
    m_forceRenderFrames = 0;
    m_inFrame = false;
    m_inSizeMove = false;
    m_occluded = false;
    m_pendingWidth = 0;
    m_pendingHeight = 0;
    m_fontsDirty = false;

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc))
    {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            Log::error("RegisterClassExW failed for ImGui window (err=%lu)", err);
            return false;
        }
    }

    // Center on the monitor containing the cursor (where the tray menu was clicked).
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT workArea{0, 0, 1280, 720};
    if (monitor && GetMonitorInfoW(monitor, &mi))
    {
        workArea = mi.rcWork;
    }

    m_dpiScale = queryDpiScaleForMonitor(monitor);
    const UINT initialDpi = static_cast<UINT>(std::round(96.0f * m_dpiScale));

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    constexpr DWORD exStyle = WS_EX_APPWINDOW;

    const int clientW = static_cast<int>(std::round(widthDip * m_dpiScale));
    const int clientH = static_cast<int>(std::round(heightDip * m_dpiScale));

    RECT winRect = adjustWindowRectForDpiCompat(RECT{0, 0, clientW, clientH}, style, exStyle, initialDpi);
    int winW = winRect.right - winRect.left;
    int winH = winRect.bottom - winRect.top;

    const int availW = workArea.right - workArea.left;
    const int availH = workArea.bottom - workArea.top;
    if (winW > availW) winW = availW;
    if (winH > availH) winH = availH;

    const int x = workArea.left + (std::max)(0, (availW - winW) / 2);
    const int y = workArea.top + (std::max)(0, (availH - winH) / 2);

    m_hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        title ? title : L"Settings",
        style,
        x, y, winW, winH,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd)
    {
        Log::error("CreateWindowExW failed for ImGui settings window (err=%lu)", GetLastError());
        return false;
    }

    applyDarkTitleBarAttributes(m_hwnd);
    m_dpiScale = queryDpiScaleForWindow(m_hwnd);

    RECT client{};
    GetClientRect(m_hwnd, &client);
    const UINT backW = static_cast<UINT>((std::max)(1L, client.right - client.left));
    const UINT backH = static_cast<UINT>((std::max)(1L, client.bottom - client.top));
    m_pendingWidth = 0;
    m_pendingHeight = 0;

    if (!createDeviceAndSwapChain(m_hwnd, backW, backH))
    {
        destroyWindow();
        return false;
    }

    if (!createRenderTarget())
    {
        destroyWindow();
        return false;
    }

    IMGUI_CHECKVERSION();
    m_imguiCtx = ImGui::CreateContext();
    if (!m_imguiCtx)
    {
        Log::error("ImGui::CreateContext failed");
        destroyWindow();
        return false;
    }

    ImGui::SetCurrentContext(m_imguiCtx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        Log::error("ImGui_ImplWin32_Init failed");
        destroyWindow();
        return false;
    }
    m_win32Init = true;

    if (!ImGui_ImplDX11_Init(m_device, m_deviceContext))
    {
        Log::error("ImGui_ImplDX11_Init failed");
        destroyWindow();
        return false;
    }
    m_dx11Init = true;

    loadFontsAndStyle(m_dpiScale);
    return true;
}

void ImGuiRenderer::destroyWindow()
{
    m_inFrame = false;
    m_onPaint = nullptr;

    if (m_imguiCtx)
    {
        ImGui::SetCurrentContext(m_imguiCtx);
        if (m_dx11Init)
        {
            ImGui_ImplDX11_Shutdown();
            m_dx11Init = false;
        }
        if (m_win32Init)
        {
            ImGui_ImplWin32_Shutdown();
            m_win32Init = false;
        }
        ImGui::DestroyContext(m_imguiCtx);
        m_imguiCtx = nullptr;
    }

    m_regularFont = nullptr;
    m_boldFont = nullptr;
    m_titleFont = nullptr;

    if (m_deviceContext)
    {
        m_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_deviceContext->ClearState();
    }

    releaseRenderTarget();
    safeRelease(m_swapChain);
    safeRelease(m_deviceContext);
    safeRelease(m_device);

    if (m_hwnd)
    {
        HWND hwnd = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(hwnd);
    }
}

void ImGuiRenderer::focusWindow()
{
    if (!isOpen())
    {
        return;
    }
    if (IsIconic(m_hwnd))
    {
        ShowWindow(m_hwnd, SW_RESTORE);
    }
    SetForegroundWindow(m_hwnd);
}

bool ImGuiRenderer::createDeviceAndSwapChain(HWND hwnd, UINT width, UINT height)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL createdLevel{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &m_swapChain,
        &m_device,
        &createdLevel,
        &m_deviceContext
    );

    if (hr == DXGI_ERROR_UNSUPPORTED || FAILED(hr))
    {
        // Retry with legacy DXGI_SWAP_EFFECT_DISCARD if FLIP_DISCARD isn't supported.
        sd.BufferCount = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &sd,
            &m_swapChain,
            &m_device,
            &createdLevel,
            &m_deviceContext
        );
    }

    if (FAILED(hr))
    {
        // Final fallback to WARP software rasterizer.
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &sd,
            &m_swapChain,
            &m_device,
            &createdLevel,
            &m_deviceContext
        );
        if (FAILED(hr))
        {
            sd.BufferCount = 1;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                createDeviceFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &sd,
                &m_swapChain,
                &m_device,
                &createdLevel,
                &m_deviceContext
            );
        }
    }

    if (FAILED(hr))
    {
        Log::error("D3D11CreateDeviceAndSwapChain failed (hr=0x%08lX)", hr);
        return false;
    }

    IDXGIFactory *factory = nullptr;
    if (SUCCEEDED(m_swapChain->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory))) && factory)
    {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }

    return true;
}

bool ImGuiRenderer::createRenderTarget()
{
    ID3D11Texture2D *backBuffer = nullptr;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer));
    if (FAILED(hr) || !backBuffer)
    {
        Log::error("IDXGISwapChain::GetBuffer failed (hr=0x%08lX)", hr);
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
    backBuffer->Release();
    if (FAILED(hr) || !m_rtv)
    {
        Log::error("ID3D11Device::CreateRenderTargetView failed (hr=0x%08lX)", hr);
        return false;
    }

    return true;
}

void ImGuiRenderer::releaseRenderTarget()
{
    safeRelease(m_rtv);
}

void ImGuiRenderer::loadFontsAndStyle(float dpiScale)
{
    ImGui::SetCurrentContext(m_imguiCtx);
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    m_regularFont = nullptr;
    m_boldFont = nullptr;
    m_titleFont = nullptr;

    const float baseSize = (std::max)(12.0f, std::round(16.0f * dpiScale));
    const float titleSize = (std::max)(16.0f, std::round(21.0f * dpiScale));

    static ImVector<ImWchar> glyphRanges;
    if (glyphRanges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddChar(0x2013); // en-dash
        builder.AddChar(0x2014); // em-dash
        builder.AddChar(0x2022); // bullet
        builder.AddChar(0x2026); // ellipsis
        builder.AddChar(0x2192); // right arrow
        builder.AddChar(0x00D7); // multiplication sign
        builder.BuildRanges(&glyphRanges);
    }

    wchar_t winDir[MAX_PATH]{};
    const UINT len = GetWindowsDirectoryW(winDir, MAX_PATH);
    std::string fontDir = "C:\\Windows\\Fonts\\";
    if (len > 0 && len < MAX_PATH)
    {
        const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, winDir, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 1)
        {
            std::string dir(static_cast<size_t>(utf8Len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, winDir, -1, dir.data(), utf8Len, nullptr, nullptr);
            fontDir = dir + "\\Fonts\\";
        }
    }

    const std::string regularPath = fontDir + "segoeui.ttf";
    const std::string boldPath = fontDir + "segoeuib.ttf";

    auto fileExists = [](const std::string &path) {
        const DWORD attr = GetFileAttributesA(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };

    if (fileExists(regularPath))
    {
        m_regularFont = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), baseSize, nullptr, glyphRanges.Data);
    }
    if (!m_regularFont)
    {
        ImFontConfig cfg;
        cfg.SizePixels = baseSize;
        m_regularFont = io.Fonts->AddFontDefault(&cfg);
    }

    if (fileExists(boldPath))
    {
        m_boldFont = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), baseSize, nullptr, glyphRanges.Data);
        m_titleFont = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), titleSize, nullptr, glyphRanges.Data);
    }
    if (!m_boldFont)
    {
        m_boldFont = m_regularFont;
    }
    if (!m_titleFont)
    {
        m_titleFont = m_boldFont;
    }

    io.FontDefault = m_regularFont;
    io.Fonts->Build();

    applyXboxDarkTheme(dpiScale);
}

void ImGuiRenderer::applyXboxDarkTheme(float dpiScale)
{
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle(); // Reset before scaling so DPI shifts never compound.

    style.WindowPadding = ImVec2(20.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.CellPadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 14.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.60f, 0.66f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.063f, 0.078f, 0.094f, 1.00f); // #101418
    c[ImGuiCol_ChildBg] = ImVec4(0.094f, 0.114f, 0.137f, 1.00f);  // #181D23
    c[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.133f, 0.161f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.200f, 0.235f, 0.282f, 1.00f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    c[ImGuiCol_FrameBg] = ImVec4(0.125f, 0.153f, 0.184f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.165f, 0.200f, 0.243f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.192f, 0.235f, 0.286f, 1.00f);

    c[ImGuiCol_TitleBg] = ImVec4(0.063f, 0.078f, 0.094f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.063f, 0.078f, 0.094f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.075f, 0.090f, 0.110f, 1.00f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.29f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.38f, 0.44f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.46f, 0.52f, 1.00f);

    // Xbox Green accent (#107C10 / brighter #2CA82C)
    c[ImGuiCol_CheckMark] = ImVec4(0.24f, 0.80f, 0.24f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.16f, 0.65f, 0.16f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.24f, 0.80f, 0.24f, 1.00f);

    c[ImGuiCol_Button] = ImVec4(0.145f, 0.176f, 0.212f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.196f, 0.239f, 0.290f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.118f, 0.145f, 0.176f, 1.00f);

    c[ImGuiCol_Header] = ImVec4(0.063f, 0.486f, 0.063f, 0.40f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.086f, 0.580f, 0.086f, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.110f, 0.667f, 0.110f, 0.70f);

    c[ImGuiCol_Separator] = ImVec4(0.200f, 0.235f, 0.282f, 1.00f);
    c[ImGuiCol_Tab] = ImVec4(0.102f, 0.125f, 0.153f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.165f, 0.200f, 0.243f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.063f, 0.486f, 0.063f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.24f, 0.80f, 0.24f, 1.00f);

    c[ImGuiCol_TableHeaderBg] = ImVec4(0.118f, 0.145f, 0.176f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.200f, 0.235f, 0.282f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.160f, 0.190f, 0.225f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

    c[ImGuiCol_NavHighlight] = ImVec4(0.24f, 0.80f, 0.24f, 1.00f);

    style.ScaleAllSizes(dpiScale);
}

bool ImGuiRenderer::beginFrame()
{
    if (!isOpen() || m_closeRequested || m_inFrame)
    {
        return false;
    }

    if (IsIconic(m_hwnd))
    {
        Sleep(50);
        return false;
    }

    if (m_occluded && m_forceRenderFrames <= 0)
    {
        const HRESULT testHr = m_swapChain->Present(0, DXGI_PRESENT_TEST);
        if (testHr == DXGI_STATUS_OCCLUDED)
        {
            Sleep(50);
            return false;
        }
        m_occluded = false;
    }
    if (m_forceRenderFrames > 0)
    {
        --m_forceRenderFrames;
    }

    ImGui::SetCurrentContext(m_imguiCtx);

    if (m_fontsDirty)
    {
        m_fontsDirty = false;
        ImGui_ImplDX11_InvalidateDeviceObjects();
        loadFontsAndStyle(m_dpiScale);
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    if (m_pendingWidth > 0 && m_pendingHeight > 0)
    {
        const UINT w = m_pendingWidth;
        const UINT h = m_pendingHeight;
        m_pendingWidth = 0;
        m_pendingHeight = 0;

        m_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_deviceContext->Flush();
        releaseRenderTarget();

        const HRESULT hr = m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            Log::error("IDXGISwapChain::ResizeBuffers(%ux%u) failed (hr=0x%08lX)", w, h, hr);
            requestClose();
            return false;
        }
        if (!createRenderTarget())
        {
            requestClose();
            return false;
        }
    }

    if (!m_rtv)
    {
        return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    m_inFrame = true;
    return true;
}

void ImGuiRenderer::endFrame(const float clearColor[4])
{
    if (!m_inFrame)
    {
        return;
    }

    ImGui::Render();

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const float width = static_cast<float>((std::max)(1L, rc.right - rc.left));
    const float height = static_cast<float>((std::max)(1L, rc.bottom - rc.top));

    D3D11_VIEWPORT vp{};
    vp.Width = width;
    vp.Height = height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_deviceContext->RSSetViewports(1, &vp);
    m_deviceContext->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_deviceContext->ClearRenderTargetView(m_rtv, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_inFrame = false;

    const bool warmingUp = !m_firstFramePresented;
    const HRESULT hr = m_swapChain->Present(warmingUp ? 0 : 1, 0);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        Log::error("D3D11 device lost during Present (hr=0x%08lX)", hr);
        requestClose();
        return;
    }

    if (warmingUp)
    {
        ++m_warmupFrames;
        // Let Dear ImGui run 3 frames off-screen so AutoResizeY children and TabBar tabs
        // finish their initial layout pass before the window becomes visible.
        if (m_warmupFrames >= 3 && !m_closeRequested)
        {
            m_firstFramePresented = true;
            ShowWindow(m_hwnd, SW_SHOW);
            UpdateWindow(m_hwnd);
            focusWindow();
        }
        return;
    }

    if (hr == DXGI_STATUS_OCCLUDED)
    {
        m_occluded = true;
    }
    else
    {
        m_occluded = false;
    }
}

LRESULT CALLBACK ImGuiRenderer::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGuiRenderer *self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self = reinterpret_cast<ImGuiRenderer *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<ImGuiRenderer *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
    {
        return self->handleWndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ImGuiRenderer::handleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
    case WM_DPICHANGED:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        m_forceRenderFrames = 3;
        break;
    }

    if (m_win32Init)
    {
        ImGui::SetCurrentContext(m_imguiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        {
            return TRUE;
        }
    }

    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            m_pendingWidth = LOWORD(lParam);
            m_pendingHeight = HIWORD(lParam);
        }
        return 0;

    case WM_GETMINMAXINFO:
    {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
        const UINT dpi = static_cast<UINT>(std::round(96.0f * m_dpiScale));
        const int minClientW = static_cast<int>(std::round(m_minWidthDip * m_dpiScale));
        const int minClientH = static_cast<int>(std::round(m_minHeightDip * m_dpiScale));
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        const RECT minWin = adjustWindowRectForDpiCompat(
            RECT{0, 0, minClientW, minClientH}, style, exStyle, dpi);
        mmi->ptMinTrackSize.x = minWin.right - minWin.left;
        mmi->ptMinTrackSize.y = minWin.bottom - minWin.top;
        return 0;
    }

    case WM_DPICHANGED:
    {
        const UINT newDpi = HIWORD(wParam);
        if (newDpi > 0)
        {
            m_dpiScale = static_cast<float>(newDpi) / 96.0f;
            m_fontsDirty = true;
        }
        const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
        if (suggested)
        {
            SetWindowPos(
                hwnd,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        m_inSizeMove = true;
        return 0;

    case WM_EXITSIZEMOVE:
        m_inSizeMove = false;
        m_forceRenderFrames = 2;
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        if (m_inSizeMove && !m_inFrame && m_firstFramePresented && !m_closeRequested && m_onPaint)
        {
            m_onPaint();
        }
        else if (m_forceRenderFrames < 1)
        {
            m_forceRenderFrames = 1;
        }
        return 0;
    }

    case WM_CLOSE:
        m_closeRequested = true;
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
