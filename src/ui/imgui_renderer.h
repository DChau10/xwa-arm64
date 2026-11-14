#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <functional>

// On-demand DirectX 11 + Win32 Dear ImGui window host for xwa-arm64.
// Resources (D3D11 device, swap chain, fonts) exist only while the window
// is open, keeping background idle RAM/GPU usage at zero when closed.
class ImGuiRenderer
{
public:
    ImGuiRenderer() = default;
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer &) = delete;
    ImGuiRenderer &operator=(const ImGuiRenderer &) = delete;

    // Enable Per-Monitor V2 DPI awareness for the process (call once at startup).
    static void enableProcessDpiAwareness();

    // Create an ImGui-backed native dark-mode Win32 window.
    // The window remains hidden until the first frame is presented.
    bool createWindow(
        const wchar_t *title,
        int widthDip,
        int heightDip,
        int minWidthDip = 500,
        int minHeightDip = 440,
        std::function<void()> onPaint = nullptr
    );

    // Destroy the window and release all D3D11 / ImGui resources.
    void destroyWindow();

    bool isOpen() const { return m_hwnd != nullptr && IsWindow(m_hwnd); }
    bool isCloseRequested() const { return m_closeRequested; }
    void requestClose() { m_closeRequested = true; }
    void focusWindow();

    // Handle pending resize/DPI updates and begin an ImGui frame.
    // Returns false if the window is minimized, closed, or cannot render.
    bool beginFrame();

    // Finish the ImGui frame, render to backbuffer, and present with VSync.
    void endFrame(const float clearColor[4]);

    HWND getHwnd() const { return m_hwnd; }
    float getDpiScale() const { return m_dpiScale; }
    ImFont *getRegularFont() const { return m_regularFont; }
    ImFont *getBoldFont() const { return m_boldFont; }
    ImFont *getTitleFont() const { return m_titleFont; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool createDeviceAndSwapChain(HWND hwnd, UINT width, UINT height);
    bool createRenderTarget();
    void releaseRenderTarget();
    void loadFontsAndStyle(float dpiScale);
    void applyXboxDarkTheme(float dpiScale);

    HWND m_hwnd = nullptr;
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_deviceContext = nullptr;
    IDXGISwapChain *m_swapChain = nullptr;
    ID3D11RenderTargetView *m_rtv = nullptr;
    ImGuiContext *m_imguiCtx = nullptr;

    ImFont *m_regularFont = nullptr;
    ImFont *m_boldFont = nullptr;
    ImFont *m_titleFont = nullptr;

    float m_dpiScale = 1.0f;
    int m_minWidthDip = 500;
    int m_minHeightDip = 440;
    UINT m_pendingWidth = 0;
    UINT m_pendingHeight = 0;
    bool m_fontsDirty = false;
    bool m_closeRequested = false;
    bool m_firstFramePresented = false;
    int m_warmupFrames = 0;
    int m_forceRenderFrames = 0;
    bool m_inFrame = false;
    bool m_inSizeMove = false;
    bool m_occluded = false;
    bool m_win32Init = false;
    bool m_dx11Init = false;

    std::function<void()> m_onPaint;
};
