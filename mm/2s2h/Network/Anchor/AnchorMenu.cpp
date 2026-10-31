#include "Anchor.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/BenGui/UIWidgets.hpp" // trae imgui, Context y CVars

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct TextField {
    char buf[128] = {};
    bool loaded = false;
};

void DrawTextCVar(const char* label, const char* cvar, const char* def, TextField& f, float width = 260.0f) {
    if (!f.loaded) {
        std::snprintf(f.buf, sizeof(f.buf), "%s", CVarGetString(cvar, def));
        f.loaded = true;
    }
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, f.buf, sizeof(f.buf))) {
        CVarSetString(cvar, f.buf);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
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

bool DrawCheckCVar(const char* label, const char* cvar, bool def) {
    bool v = CVarGetInteger(cvar, def ? 1 : 0) != 0;
    if (ImGui::Checkbox(label, &v)) {
        CVarSetInteger(cvar, v ? 1 : 0);
        return true;
    }
    return false;
}

void AnchorSaveCVars() {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

} // namespace

void Anchor_DrawMenu() {
    static TextField host, port, room, team, name;
    AnchorClient& client = AnchorClient::Get();
    AnchorStatus status = client.GetStatus();

    ImGui::SeparatorText("Conexion");
    DrawTextCVar("Servidor", "gAnchor.Host", "anchor.hm64.org", host);
    {
        int p = CVarGetInteger("gAnchor.Port", 43383);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Puerto", &p, 0, 0)) {
            CVarSetInteger("gAnchor.Port", p);
            AnchorSaveCVars();
        }
    }
    DrawTextCVar("Sala", "gAnchor.Room", "", room);
    DrawTextCVar("Equipo", "gAnchor.Team", "default", team);

    ImGui::SeparatorText("Tu jugador");
    DrawTextCVar("Nombre", "gAnchor.Name", "Link", name);
    bool lookChanged = false;
    lookChanged |= DrawColorCVar("Tunica", "gAnchor.Look.Tunic", 0x1E691B);
    lookChanged |= DrawColorCVar("Pelo", "gAnchor.Look.Hair", 0xF0D060);
    lookChanged |= DrawColorCVar("Botas", "gAnchor.Look.Boots", 0x6B4423);
    bool pvpChanged = DrawCheckCVar("Activar PvP (golpear con la espada a otros jugadores)", "gAnchor.Pvp", false);
    if (CVarGetInteger("gAnchor.Pvp", 0)) {
        int dmg = CVarGetInteger("gAnchor.PvpDamage", 8);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderInt("Dano por golpe (16 = 1 corazon)", &dmg, 4, 48)) {
            CVarSetInteger("gAnchor.PvpDamage", dmg);
            AnchorSaveCVars();
        }
    }
    DrawCheckCVar("Compartir objetos con el equipo", "gAnchor.ShareItems", true);
    DrawCheckCVar("Compartir flags de eventos con el equipo (experimental)", "gAnchor.ShareFlags", false);

    if (lookChanged || pvpChanged) {
        AnchorSaveCVars();
        if (status == AnchorStatus::Connected) {
            Anchor_NotifyLocalStateChanged();
        }
    }

    ImGui::Spacing();
    if (status == AnchorStatus::Disconnected) {
        if (ImGui::Button("Conectar")) {
            AnchorSaveCVars();
            Anchor_NotifyLocalStateChanged();
            client.Connect();
        }
    } else {
        if (ImGui::Button("Desconectar")) {
            client.Disconnect();
        }
        if (status == AnchorStatus::Connected) {
            ImGui::SameLine();
            if (ImGui::Button("Reenviar mis datos")) {
                Anchor_NotifyLocalStateChanged();
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextWrapped("Estado: %s", client.GetStatusText().c_str());

    if (status == AnchorStatus::Connected) {
        ImGui::SeparatorText("Jugadores en la sala");
        std::vector<AnchorRemote> remotes = client.Remotes();
        if (remotes.empty()) {
            ImGui::TextDisabled("Nadie mas por ahora");
        }
        for (const AnchorRemote& r : remotes) {
            ImVec4 c(((r.look.tunic >> 16) & 0xFF) / 255.0f, ((r.look.tunic >> 8) & 0xFF) / 255.0f,
                     (r.look.tunic & 0xFF) / 255.0f, 1.0f);
            ImGui::ColorButton(("##c" + std::to_string(r.clientId)).c_str(), c, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(14, 14));
            ImGui::SameLine();
            ImGui::Text("%s  (equipo: %s, escena: %d%s)", r.name.empty() ? "Jugador" : r.name.c_str(),
                        r.teamId.c_str(), r.sceneId, r.pvp ? ", PvP" : "");
        }
    }
}
