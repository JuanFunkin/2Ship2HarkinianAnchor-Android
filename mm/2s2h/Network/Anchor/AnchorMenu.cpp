#include "Anchor.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/window/gui/GuiWindow.h>
#include <ship/window/gui/IconsFontAwesome4.h>

#include "2s2h/BenGui/UIWidgets.hpp"
#include "2s2h/BenGui/BenMenu.h"
#include "2s2h/BenGui/BenGui.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace BenGui {
extern std::shared_ptr<BenMenu> mBenMenu;
}
using namespace BenGui;

namespace {

constexpr const char* kDefaultHost = "anchor.hm64.org";
constexpr int kDefaultPort = 43383;
constexpr const char* kGlobalRoom = "2s2h-global";

struct TextField {
    char buf[128] = {};
    bool loaded = false;
};

TextField sHost, sName, sRoom, sTeam;
bool sShowRoomId = false;

void AnchorSaveCVars() {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

// Campo de texto ligado a un CVar. Devuelve true cuando el usuario termina de editar.
bool DrawTextCVar(const char* id, const char* cvar, const char* def, TextField& f, float width, int flags = 0) {
    if (!f.loaded) {
        std::snprintf(f.buf, sizeof(f.buf), "%s", CVarGetString(cvar, def));
        f.loaded = true;
    }
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, f.buf, sizeof(f.buf), flags)) {
        CVarSetString(cvar, f.buf);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        AnchorSaveCVars();
        return true;
    }
    return false;
}

bool DrawColorCVar(const char* label, const char* cvar, uint32_t def) {
    uint32_t c = ((uint32_t)CVarGetInteger(cvar, (int)def)) & 0xFFFFFF;
    float col[3] = { ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f };
    if (ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs)) {
        uint32_t r = (uint32_t)(col[0] * 255.0f + 0.5f);
        uint32_t g = (uint32_t)(col[1] * 255.0f + 0.5f);
        uint32_t b = (uint32_t)(col[2] * 255.0f + 0.5f);
        CVarSetInteger(cvar, (int)((r << 16) | (g << 8) | b));
        return true;
    }
    return false;
}

ImVec4 ColorFromRGB(uint32_t rgb) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

bool DrawCombo(const char* id, int* value, const char* const* items, int count) {
    bool changed = false;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(id, items[*value])) {
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(items[i], *value == i)) {
                *value = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void RestoreDefaults() {
    CVarSetString("gAnchor.Host", kDefaultHost);
    CVarSetInteger("gAnchor.Port", kDefaultPort);
    CVarSetString("gAnchor.Name", "Link");
    CVarSetString("gAnchor.Room", "");
    CVarSetString("gAnchor.Team", "default");
    sHost.loaded = sName.loaded = sRoom.loaded = sTeam.loaded = false;
    AnchorSaveCVars();
}

// ------------------------------------------------------------------ ventana "Anchor Room"

void DrawPlayerRow(const std::string& name, uint32_t tunic, bool admin, const std::string& team, int scene,
                   const AnchorRemote* remote) {
    if (admin) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), ICON_FA_STAR);
    } else {
        ImGui::TextColored(ColorFromRGB(tunic), ICON_FA_USER);
    }
    ImGui::SameLine();
    ImGui::TextColored(ColorFromRGB(tunic), "%s", name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(team: %s, scene: %d)", team.c_str(), scene);
    if (remote != nullptr) {
        ImGui::SameLine();
        bool sameScene = remote->sceneId == Anchor_GetLocalScene() && Anchor_GetLocalScene() >= 0;
        bool can = Anchor_CanTeleportTo(*remote) && sameScene;
        ImGui::BeginDisabled(!can);
        std::string label = std::string("Teleport##") + std::to_string(remote->clientId);
        if (ImGui::SmallButton(label.c_str())) {
            Anchor_RequestTeleport(remote->clientId);
        }
        ImGui::EndDisabled();
        if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", sameScene ? "Teleporting to this player is disabled by the room settings."
                                              : "You can only teleport to players in the same scene.");
        }
    }
}

void DrawRoomPlayers() {
    AnchorClient& client = AnchorClient::Get();
    if (client.GetStatus() != AnchorStatus::Connected) {
        ImGui::TextDisabled("Not connected.");
        return;
    }
    uint64_t owner = Anchor_GetRoomOwnerId();
    AnchorLook mine = Anchor_GetLocalLook();
    DrawPlayerRow(Anchor_GetName() + " (you)", mine.tunic, owner != 0 && owner == client.SelfId(), Anchor_GetTeamId(),
                  Anchor_GetLocalScene(), nullptr);
    std::vector<AnchorRemote> remotes = client.Remotes();
    std::sort(remotes.begin(), remotes.end(),
              [](const AnchorRemote& a, const AnchorRemote& b) { return a.clientId < b.clientId; });
    for (const AnchorRemote& r : remotes) {
        DrawPlayerRow(r.name.empty() ? std::string("Player") : r.name, r.look.tunic, owner != 0 && owner == r.clientId,
                      r.teamId, r.sceneId, &r);
    }
}

class AnchorRoomWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
        DrawRoomPlayers();
    }
    void UpdateElement() override {
    }
};

// ------------------------------------------------------------------ columna izquierda

void DrawConnectionColumn() {
    AnchorClient& client = AnchorClient::Get();
    const AnchorStatus status = client.GetStatus();
    const float fullW = ImGui::GetContentRegionAvail().x;

    ImGui::SeparatorText("Connection Settings");

    ImGui::TextDisabled("Host & Port");
    DrawTextCVar("##anchorhost", "gAnchor.Host", kDefaultHost, sHost, fullW * 0.74f);
    ImGui::SameLine();
    {
        int port = CVarGetInteger("gAnchor.Port", kDefaultPort);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputInt("##anchorport", &port, 0, 0)) {
            CVarSetInteger("gAnchor.Port", std::max(1, std::min(port, 65535)));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            AnchorSaveCVars();
        }
    }

    ImGui::TextDisabled("Name");
    bool nameEdited = DrawTextCVar("##anchorname", "gAnchor.Name", "Link", sName, -1.0f);

    ImGui::TextDisabled("Room ID");
    DrawTextCVar("##anchorroom", "gAnchor.Room", "", sRoom, fullW - 70.0f,
                 sShowRoomId ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::Checkbox("Show", &sShowRoomId);

    ImGui::TextDisabled("Team ID (Items Shared)");
    DrawTextCVar("##anchorteam", "gAnchor.Team", "default", sTeam, -1.0f);

    const float halfW = (fullW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (UIWidgets::Button("Restore Defaults",
                          UIWidgets::ButtonOptions().Size(ImVec2(halfW, 0)).Color(UIWidgets::Colors::DarkRed))) {
        RestoreDefaults();
    }
    ImGui::SameLine();
    if (UIWidgets::Button("Global Room",
                          UIWidgets::ButtonOptions()
                              .Size(ImVec2(halfW, 0))
                              .Color(UIWidgets::Colors::DarkBlue)
                              .Tooltip("Fills in the public room used by everybody."))) {
        CVarSetString("gAnchor.Room", kGlobalRoom);
        sRoom.loaded = false;
        AnchorSaveCVars();
    }

    const bool enabled = status != AnchorStatus::Disconnected;
    if (UIWidgets::Button(enabled ? "Disable" : "Enable",
                          UIWidgets::ButtonOptions().Color(enabled ? UIWidgets::Colors::Red
                                                                   : UIWidgets::Colors::Green))) {
        if (enabled) {
            client.Disconnect();
        } else {
            AnchorSaveCVars();
            Anchor_NotifyLocalStateChanged();
            client.Connect();
        }
    }
    if (nameEdited && status == AnchorStatus::Connected) {
        Anchor_NotifyLocalStateChanged();
    }

    // ---------------------------------------------------------------- apariencia (novedad)
    ImGui::SeparatorText("Appearance");
    bool lookChanged = false;
    lookChanged |= DrawColorCVar("Tunic", "gAnchor.Look.Tunic", 0x1E691B);
    lookChanged |= DrawColorCVar("Hair", "gAnchor.Look.Hair", 0xF0D060);
    lookChanged |= DrawColorCVar("Boots", "gAnchor.Look.Boots", 0x6B4423);
    if (ImGui::SmallButton("Reset colors")) {
        AnchorLook d;
        Anchor_SetLocalLook(d);
        lookChanged = true;
    }
    ImGui::TextDisabled("Other players see your character with these colors.");
    if (lookChanged) {
        AnchorSaveCVars();
        if (status == AnchorStatus::Connected) {
            Anchor_NotifyLocalStateChanged();
        }
    }

    // ---------------------------------------------------------------- sala actual
    ImGui::SeparatorText("Current Room");
    if (status == AnchorStatus::Connected) {
        ImGui::Text(ICON_FA_CHECK " Connected");
    } else {
        ImGui::TextWrapped("%s", client.GetStatusText().c_str());
    }

    ImGui::BeginDisabled(status != AnchorStatus::Connected);
    if (UIWidgets::Button("Request Team State",
                          UIWidgets::ButtonOptions()
                              .Size(ImVec2(halfW, 0))
                              .Color(UIWidgets::Colors::Indigo)
                              .Tooltip("Asks your teammates for their items and event flags."))) {
        Anchor_RequestTeamState();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (UIWidgets::Button("Toggle Anchor Room Window", UIWidgets::ButtonOptions().Size(ImVec2(halfW, 0)))) {
        CVarSetInteger("gWindows.AnchorRoom", CVarGetInteger("gWindows.AnchorRoom", 0) ? 0 : 1);
        AnchorSaveCVars();
    }

    DrawRoomPlayers();
}

// ------------------------------------------------------------------ columna derecha

void DrawRoomSettingsColumn() {
    AnchorClient& client = AnchorClient::Get();
    const bool connected = client.GetStatus() == AnchorStatus::Connected;
    const bool admin = Anchor_IsRoomAdmin();
    AnchorRoomSettings rs = Anchor_GetRoomSettings();
    bool changed = false;

    ImGui::SeparatorText("Room Settings (Admin Only)");
    ImGui::BeginDisabled(!admin);

    if (UIWidgets::Button("Clear All Team State",
                          UIWidgets::ButtonOptions()
                              .Size(ImVec2(260, 0))
                              .Color(UIWidgets::Colors::Indigo)
                              .Tooltip("Erases the progress the server saved for every team."))) {
        Anchor_ClearAllTeamState();
    }

    static const char* const kPvp[] = { "Off", "On", "On + Friendly Fire" };
    static const char* const kScope[] = { "None", "Team", "All" };

    ImGui::Text("PvP Mode:");
    changed |= DrawCombo("##pvpmode", &rs.pvpMode, kPvp, 3);
    ImGui::Text("Show Locations For:");
    changed |= DrawCombo("##showloc", &rs.showLocationsFor, kScope, 3);
    ImGui::Text("Allow Teleporting To:");
    changed |= DrawCombo("##allowtp", &rs.allowTeleportingTo, kScope, 3);
    changed |= ImGui::Checkbox("Sync Items & Flags", &rs.syncItemsFlags);

    ImGui::EndDisabled();
    if (changed) {
        Anchor_SetRoomSettings(rs);
    }
    if (!admin) {
        ImGui::TextDisabled(connected ? "Only the room creator can change these settings."
                                      : "Connect to a room to see its settings.");
    }

    ImGui::Spacing();
    ImGui::Text("Important Notes:");
    ImGui::TextWrapped("- All players involved should start at the file select screen with Anchor disabled.");
    ImGui::TextWrapped("- If you play the Randomizer, turn off \"Sync Items & Flags\" to avoid duplicated items.");
    ImGui::TextWrapped("- Everyone in the room must run the same version of the game.");

    ImGui::SeparatorText("Co-op Usage Instructions");
    ImGui::TextWrapped("1. The host player sets up a room by entering a Room ID and enabling Anchor. (If there is "
                       "just one team, leave the Team ID as 'default'.)");
    ImGui::TextWrapped("2. Configure the room settings as desired (PvP, locations, teleporting, syncing). Only the "
                       "host can change them.");
    ImGui::TextWrapped("3. Other players enter the same Room ID and a Team ID, then enable Anchor. Players on the "
                       "same team share items and flags.");
    ImGui::TextWrapped("4. Pick your tunic, hair and boots colors under Appearance. Everyone sees them on your "
                       "character.");
    ImGui::TextWrapped("5. If you joined late, press Request Team State to receive your team's progress.");
    ImGui::TextWrapped("6. To hit other players, enable PvP in the room settings and use your sword near them.");
}

// ------------------------------------------------------------------ registro del menu

void RegisterAnchorMenu() {
    mBenMenu->AddMenuEntry("Network", "gSettings.Menu.NetworkSidebarSection");
    mBenMenu->AddSidebarEntry("Network", "Anchor", 2);

    WidgetPath path = { "Network", "Anchor", SECTION_COLUMN_1 };
    mBenMenu->AddWidget(path, "Anchor Connection", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo& info) { DrawConnectionColumn(); })
        .HideInSearch(true);

    path.column = SECTION_COLUMN_2;
    mBenMenu->AddWidget(path, "Anchor Room Settings", WIDGET_CUSTOM)
        .CustomFunction([](WidgetInfo& info) { DrawRoomSettingsColumn(); })
        .HideInSearch(true);

    static bool windowAdded = false;
    if (!windowAdded) {
        windowAdded = true;
        auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
        gui->AddGuiWindow(std::make_shared<AnchorRoomWindow>("gWindows.AnchorRoom", "Anchor Room", ImVec2(460, 320)));
    }
}

} // namespace

static RegisterMenuInitFunc sAnchorMenuInit(RegisterAnchorMenu);
