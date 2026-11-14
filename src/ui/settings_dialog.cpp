#include "settings_dialog.h"
#include "../utils/log.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>

namespace {

constexpr float kClearColor[4] = {0.063f, 0.078f, 0.094f, 1.0f}; // #101418

struct StatusVisual
{
    ImVec4 color;
    std::string label;
    const char *actionLabel;
    bool isWinUsbAction;
};

StatusVisual describeStatus(const TrayStatusSnapshot &snap)
{
    if (snap.driverState == DriverUiState::DriverMissing)
    {
        return {
            ImVec4(0.96f, 0.65f, 0.14f, 1.0f),
            "Adapter Detected  \xE2\x80\xA2  WinUSB Driver Required",
            "WinUSB Setup Guide",
            true};
    }

    if (snap.controllerConnected || snap.driverState == DriverUiState::ControllerConnected)
    {
        std::string text = "Xbox Wireless Adapter Active  \xE2\x80\xA2  Controller Connected";
        if (!snap.batteryLevel.empty() && snap.batteryLevel != "none" && snap.batteryLevel != "unknown")
        {
            if (snap.batteryLevel == "charging")
            {
                text += " (charging)";
            }
            else
            {
                text += " (" + snap.batteryLevel + " battery)";
            }
        }
        return {ImVec4(0.24f, 0.84f, 0.28f, 1.0f), std::move(text), "Pair Controller", false};
    }

    if (snap.dongleActive || snap.driverState == DriverUiState::Active)
    {
        return {
            ImVec4(0.26f, 0.78f, 0.60f, 1.0f),
            "Adapter Ready  \xE2\x80\xA2  Waiting for Controller",
            "Pair Controller",
            false};
    }

    return {
        ImVec4(0.56f, 0.62f, 0.70f, 1.0f),
        "Waiting for Xbox Wireless Adapter...",
        "WinUSB Setup Guide",
        true};
}

void pushSectionCardStyle(float dpi)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * dpi, 12.0f * dpi));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.094f, 0.114f, 0.137f, 1.00f));
}

void popSectionCardStyle()
{
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void renderSectionHeader(ImFont *boldFont, const char *title)
{
    if (boldFont)
    {
        ImGui::PushFont(boldFont);
    }
    ImGui::TextUnformatted(title);
    if (boldFont)
    {
        ImGui::PopFont();
    }
}

bool renderComboSetting(const char *label, const char *id, int &value, const char *const *items, int count)
{
    if (value < 0 || value >= count)
    {
        value = 0;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    return ImGui::Combo(id, &value, items, count);
}

} // namespace

SettingsDialog::~SettingsDialog()
{
    close();
}

bool SettingsDialog::open(const Settings &current, bool autoStart, Callbacks callbacks)
{
    if (isOpen())
    {
        focus();
        return true;
    }

    m_working = current;
    m_autoStart = autoStart;
    m_callbacks = std::move(callbacks);
    m_configPath = Settings::getDefaultConfigPath();
    m_copiedUntil = std::chrono::steady_clock::time_point{};

    if (!m_renderer.createWindow(
            L"xwa-arm64 Settings",
            700,
            640,
            560,
            480,
            [this]() { renderFrame(); }))
    {
        m_open = false;
        return false;
    }

    m_open = true;
    return true;
}

void SettingsDialog::close()
{
    m_open = false;
    m_renderer.destroyWindow();
    m_callbacks = Callbacks{};
}

bool SettingsDialog::isOpen() const
{
    return m_open && m_renderer.isOpen();
}

void SettingsDialog::focus()
{
    if (isOpen())
    {
        m_renderer.focusWindow();
    }
}

void SettingsDialog::tick()
{
    if (!m_open)
    {
        return;
    }

    if (m_renderer.isCloseRequested() || !m_renderer.isOpen())
    {
        close();
        return;
    }

    renderFrame();

    if (m_renderer.isCloseRequested())
    {
        close();
    }
}

void SettingsDialog::renderFrame()
{
    if (!m_renderer.beginFrame())
    {
        return;
    }

    const float dpi = m_renderer.getDpiScale();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##XwaSettingsRoot", nullptr, rootFlags))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            m_renderer.requestClose();
        }

        // Header
        if (m_renderer.getTitleFont())
        {
            ImGui::PushFont(m_renderer.getTitleFont());
        }
        ImGui::TextUnformatted("xwa-arm64");
        if (m_renderer.getTitleFont())
        {
            ImGui::PopFont();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Xbox Wireless Adapter for Windows on ARM64");

        ImGui::Spacing();
        renderStatusBanner(dpi);
        ImGui::Spacing();

        const float footerHeight =
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f + 4.0f * dpi;

        if (ImGui::BeginTabBar("##SettingsTabs"))
        {
            if (ImGui::BeginTabItem("General"))
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::BeginChild("##GeneralScroll", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None))
                {
                    renderGeneralTab(dpi);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Share Button"))
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::BeginChild("##ShareScroll", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None))
                {
                    renderShareButtonTab(dpi);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Custom Keystrokes"))
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::BeginChild("##KeysScroll", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None))
                {
                    renderKeystrokesTab(dpi);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // Pinned footer
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset Defaults"))
        {
            m_working = Settings{};
            m_autoStart = false;
        }

        const char *saveLabel = "Save & Apply";
        const char *cancelLabel = "Cancel";
        const ImGuiStyle &style = ImGui::GetStyle();
        const float btnExtraPad = 14.0f * dpi;
        const float saveWidth = ImGui::CalcTextSize(saveLabel).x + style.FramePadding.x * 2.0f + btnExtraPad;
        const float cancelWidth = ImGui::CalcTextSize(cancelLabel).x + style.FramePadding.x * 2.0f + btnExtraPad;
        const float rightX = ImGui::GetWindowContentRegionMax().x - (saveWidth + style.ItemSpacing.x + cancelWidth);

        if (rightX > ImGui::GetCursorPosX())
        {
            ImGui::SameLine(rightX);
        }
        else
        {
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.063f, 0.486f, 0.063f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.086f, 0.580f, 0.086f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.050f, 0.390f, 0.050f, 1.00f));
        const bool clickedSave = ImGui::Button(saveLabel, ImVec2(saveWidth, 0.0f));
        ImGui::PopStyleColor(3);

        if (clickedSave)
        {
            if (m_callbacks.onSave)
            {
                m_callbacks.onSave(m_working, m_autoStart);
            }
            m_renderer.requestClose();
        }

        ImGui::SameLine();
        if (ImGui::Button(cancelLabel, ImVec2(cancelWidth, 0.0f)))
        {
            m_renderer.requestClose();
        }
    }
    ImGui::End();

    m_renderer.endFrame(kClearColor);
}

void SettingsDialog::renderStatusBanner(float dpi)
{
    const TrayStatusSnapshot snap = m_callbacks.getStatus ? m_callbacks.getStatus() : TrayStatusSnapshot{};
    const StatusVisual visual = describeStatus(snap);

    pushSectionCardStyle(dpi);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.102f, 0.125f, 1.00f));
    if (ImGui::BeginChild("##StatusBanner", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        const bool showActionButton =
            (visual.isWinUsbAction && snap.driverState == DriverUiState::DriverMissing && m_callbacks.onWinUsbGuide) ||
            (!visual.isWinUsbAction && snap.dongleActive && m_callbacks.onPair);

        const float dotRadius = 5.0f * dpi;
        const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
        const float frameHeight = ImGui::GetFrameHeight();
        const ImVec2 dotCenter(
            cursorScreen.x + dotRadius + 2.0f * dpi,
            cursorScreen.y + frameHeight * 0.5f);

        ImGui::GetWindowDrawList()->AddCircleFilled(
            dotCenter,
            dotRadius,
            ImGui::ColorConvertFloat4ToU32(visual.color),
            16);

        ImGui::Dummy(ImVec2(dotRadius * 2.0f + 6.0f * dpi, frameHeight));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (m_renderer.getBoldFont())
        {
            ImGui::PushFont(m_renderer.getBoldFont());
        }
        ImGui::TextUnformatted(visual.label.c_str());
        if (m_renderer.getBoldFont())
        {
            ImGui::PopFont();
        }

        if (showActionButton)
        {
            const float btnW =
                ImGui::CalcTextSize(visual.actionLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f * dpi;
            const float targetX = ImGui::GetWindowContentRegionMax().x - btnW;
            if (targetX > ImGui::GetCursorPosX())
            {
                ImGui::SameLine(targetX);
                if (ImGui::Button(visual.actionLabel, ImVec2(btnW, 0.0f)))
                {
                    if (visual.isWinUsbAction)
                    {
                        m_callbacks.onWinUsbGuide();
                    }
                    else
                    {
                        m_callbacks.onPair();
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    popSectionCardStyle();
}

void SettingsDialog::renderGeneralTab(float dpi)
{
    ImGui::Spacing();

    // Startup & System Integration
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##GeneralStartup", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Startup & Integration");
        ImGui::Spacing();
        ImGui::Checkbox("Start xwa-arm64 automatically with Windows", &m_autoStart);
        ImGui::TextDisabled("Runs quietly in the Windows notification area at sign-in via HKCU\\...\\Run.");
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();

    // Driver & Hardware Setup
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##GeneralDriver", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Driver & Hardware Setup");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(
            "The adapter communicates via the inbox Microsoft WinUSB driver and presents connected controllers "
            "through ViGEmBus as standard XUSB gamepads.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("Open WinUSB Setup Assistant...") && m_callbacks.onWinUsbGuide)
        {
            m_callbacks.onWinUsbGuide();
        }
        ImGui::SameLine();
        const TrayStatusSnapshot snap = m_callbacks.getStatus ? m_callbacks.getStatus() : TrayStatusSnapshot{};
        ImGui::BeginDisabled(!snap.dongleActive);
        if (ImGui::Button("Start Controller Pairing") && m_callbacks.onPair)
        {
            m_callbacks.onPair();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Hint: Plug in the adapter first if Pairing is currently idle.");
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();

    // Supported Adapters Table
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##GeneralHardware", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Supported Xbox Wireless Adapters");
        ImGui::Spacing();

        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("##HardwareTable", 3, tableFlags))
        {
            ImGui::TableSetupColumn("Device Model", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("USB VID:PID", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch, 2.2f);
            ImGui::TableHeadersRow();

            struct AdapterInfo
            {
                const char *name;
                const char *vidPid;
                const char *notes;
            };
            static constexpr AdapterInfo kAdapters[] = {
                {"Xbox Wireless Adapter (Model 1790)", "045E:02FE", "Slim 2nd-gen USB adapter (verified)"},
                {"Xbox Wireless Adapter (Model 1713)", "045E:02E6", "Original 1st-gen USB adapter"},
                {"Microsoft Surface Xbox Radio", "045E:091E", "Built-in transceiver on Surface devices"},
            };

            for (const auto &row : kAdapters)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(row.name);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.vidPid);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", row.notes);
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();

    // Configuration File Path
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##GeneralConfigPath", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Configuration File");
        ImGui::Spacing();

        const bool justCopied = std::chrono::steady_clock::now() < m_copiedUntil;
        const char *copyLabel = justCopied ? "Copied!" : "Copy Path";
        const float copyBtnW =
            ImGui::CalcTextSize("Copy Path").x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f * dpi;

        std::array<char, 512> pathBuf{};
        std::snprintf(pathBuf.data(), pathBuf.size(), "%s", m_configPath.c_str());

        ImGui::SetNextItemWidth(-(copyBtnW + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##ConfigPath", pathBuf.data(), pathBuf.size(), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button(copyLabel, ImVec2(copyBtnW, 0.0f)))
        {
            ImGui::SetClipboardText(m_configPath.c_str());
            m_copiedUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        }
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();
}

void SettingsDialog::renderShareButtonTab(float dpi)
{
    ImGui::Spacing();

    // Opt-in toggle card
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##ShareEnableCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Series X|S Share Button Interception");
        ImGui::Spacing();
        ImGui::Checkbox("Enable Share button capture & hotkey mapping", &m_working.enableShareButton);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(
            "Maps single taps and timed holds on Xbox Series X|S controllers to Windows Game Bar, Steam, "
            "or custom keyboard shortcuts.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();
    ImGui::BeginDisabled(!m_working.enableShareButton);

    // Actions & Provider card
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##ShareActionsCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Capture Actions");
        ImGui::Spacing();

        static const char *const kProviders[] = {
            "Auto (Steam in Steam games, Game Bar otherwise)",
            "Steam Only",
            "Windows Game Bar Only",
            "Custom Keystrokes Only",
        };
        int providerIdx = static_cast<int>(m_working.provider);
        if (renderComboSetting("Capture Provider", "##ProviderCombo", providerIdx, kProviders, IM_ARRAYSIZE(kProviders)))
        {
            m_working.provider = static_cast<CaptureProvider>(providerIdx);
        }

        ImGui::Spacing();

        static const char *const kTapActions[] = {
            "Take Screenshot",
            "Steam Timeline Marker (Ctrl+F11)",
            "Custom Keystroke",
            "Disabled",
        };
        int tapIdx = static_cast<int>(m_working.tapAction);
        if (renderComboSetting("Single Tap Action", "##TapActionCombo", tapIdx, kTapActions, IM_ARRAYSIZE(kTapActions)))
        {
            m_working.tapAction = static_cast<TapAction>(tapIdx);
        }

        ImGui::Spacing();

        static const char *const kHoldActions[] = {
            "Record Last 30 Seconds / Save Clip",
            "Toggle Video Recording",
            "Custom Keystroke",
            "Disabled",
        };
        int hold1Idx = static_cast<int>(m_working.holdAction);
        if (renderComboSetting(
                "Short Hold Action (Tier 1)", "##Hold1ActionCombo", hold1Idx, kHoldActions, IM_ARRAYSIZE(kHoldActions)))
        {
            m_working.holdAction = static_cast<HoldAction>(hold1Idx);
        }

        ImGui::Spacing();

        int hold2Idx = static_cast<int>(m_working.hold2Action);
        if (renderComboSetting(
                "Long Hold Action (Tier 2)", "##Hold2ActionCombo", hold2Idx, kHoldActions, IM_ARRAYSIZE(kHoldActions)))
        {
            m_working.hold2Action = static_cast<HoldAction>(hold2Idx);
        }
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::Spacing();

    // Hold Timing & Haptics card
    pushSectionCardStyle(dpi);
    if (ImGui::BeginChild("##ShareTimingCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), "Hold Timing & Feedback");
        ImGui::Spacing();

        m_working.holdDurationMs = (std::clamp)(m_working.holdDurationMs, 600, 2500);
        m_working.hold2DurationMs = (std::clamp)(m_working.hold2DurationMs, m_working.holdDurationMs + 500, 5000);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Tier 1 Hold Threshold (%.2f s)", m_working.holdDurationMs / 1000.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##Tier1Slider", &m_working.holdDurationMs, 600, 2500, "%d ms"))
        {
            if (m_working.hold2DurationMs < m_working.holdDurationMs + 500)
            {
                m_working.hold2DurationMs = (std::min)(5000, m_working.holdDurationMs + 500);
            }
        }

        ImGui::Spacing();

        const int minTier2 = m_working.holdDurationMs + 500;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Tier 2 Hold Threshold (%.2f s)", m_working.hold2DurationMs / 1000.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##Tier2Slider", &m_working.hold2DurationMs, minTier2, 5000, "%d ms");

        ImGui::Spacing();

        static const char *const kHaptics[] = {
            "Advanced (Ascending ramp + physical detents + LED pulse)",
            "Simple (Single confirmation pulse)",
            "Off (Silent \xE2\x80\x94 no rumble)",
        };
        int hapticIdx = static_cast<int>(m_working.hapticMode);
        if (renderComboSetting(
                "Haptic & LED Feedback", "##HapticModeCombo", hapticIdx, kHaptics, IM_ARRAYSIZE(kHaptics)))
        {
            m_working.hapticMode = static_cast<HapticMode>(hapticIdx);
        }

        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(
            "Tip: For Game Bar 'Record Last 30 Seconds', enable 'Record in the background while I'm playing a game' "
            "in Windows Settings \xE2\x86\x92 Gaming \xE2\x86\x92 Captures.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    popSectionCardStyle();

    ImGui::EndDisabled();
    ImGui::Spacing();
}

void SettingsDialog::renderKeystrokesTab(float dpi)
{
    ImGui::Spacing();

    const bool anyCustomSelected =
        m_working.provider == CaptureProvider::Custom ||
        m_working.tapAction == TapAction::CustomKey ||
        m_working.holdAction == HoldAction::CustomKey ||
        m_working.hold2Action == HoldAction::CustomKey;

    if (!anyCustomSelected)
    {
        pushSectionCardStyle(dpi);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.125f, 0.080f, 1.00f));
        if (ImGui::BeginChild("##CustomNotice", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(
                ImVec4(0.96f, 0.78f, 0.36f, 1.0f),
                "Custom keystrokes are triggered when Capture Provider is set to 'Custom Keystrokes Only' "
                "or when an action on the Share Button tab uses 'Custom Keystroke'.");
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        popSectionCardStyle();
        ImGui::Spacing();
    }

    renderKeyBindingCard(
        "Single Tap Keystroke",
        "Triggered on a quick Share button press and release",
        "Tap",
        m_working.customTap,
        dpi);
    ImGui::Spacing();

    renderKeyBindingCard(
        "Tier 1 Hold Keystroke",
        "Triggered when held past Tier 1 threshold",
        "Hold1",
        m_working.customHold,
        dpi);
    ImGui::Spacing();

    renderKeyBindingCard(
        "Tier 2 Hold Keystroke",
        "Triggered when held past Tier 2 threshold",
        "Hold2",
        m_working.customHold2,
        dpi);
    ImGui::Spacing();
}

void SettingsDialog::renderKeyBindingCard(
    const char *title,
    const char *subtitle,
    const char *idSuffix,
    KeyBinding &binding,
    float dpi)
{
    ImGui::PushID(idSuffix);
    pushSectionCardStyle(dpi);

    std::string childId = std::string("##KeyCard_") + idSuffix;
    if (ImGui::BeginChild(childId.c_str(), ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        renderSectionHeader(m_renderer.getBoldFont(), title);

        const std::string formatted = formatKeyBinding(binding);
        const float badgeW = ImGui::CalcTextSize(formatted.c_str()).x + 16.0f * dpi;
        const float rightX = ImGui::GetWindowContentRegionMax().x - badgeW;
        if (rightX > ImGui::GetCursorPosX())
        {
            ImGui::SameLine(rightX);
        }
        else
        {
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.32f, 0.86f, 0.36f, 1.0f), "%s", formatted.c_str());

        ImGui::TextDisabled("%s", subtitle);
        ImGui::Spacing();

        ImGui::Checkbox("Win", &binding.win);
        ImGui::SameLine();
        ImGui::Checkbox("Ctrl", &binding.ctrl);
        ImGui::SameLine();
        ImGui::Checkbox("Alt", &binding.alt);
        ImGui::SameLine();
        ImGui::Checkbox("Shift", &binding.shift);

        const auto &keys = getSelectableKeys();
        int keyIdx = findSelectableKeyIndex(binding.vkCode);
        if (keyIdx < 0 || keyIdx >= static_cast<int>(keys.size()))
        {
            keyIdx = 0;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##KeyCombo", keys[keyIdx].name))
        {
            for (int i = 0; i < static_cast<int>(keys.size()); ++i)
            {
                const bool selected = (i == keyIdx);
                if (ImGui::Selectable(keys[i].name, selected))
                {
                    binding.vkCode = keys[i].vk;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndChild();

    popSectionCardStyle();
    ImGui::PopID();
}
