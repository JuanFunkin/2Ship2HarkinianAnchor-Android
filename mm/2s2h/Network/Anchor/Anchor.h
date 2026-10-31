#pragma once
#ifdef __cplusplus

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ---------------------------------------------------------------------------------------------
// Anchor para 2Ship: cliente multijugador (PC + Android). Habla con el servidor de
// garrettjoecox/anchor (TCP, JSON terminado en NUL). Ver ANCHOR_PROTOCOL.md.
// ---------------------------------------------------------------------------------------------

constexpr int kAnchorJointCount = 23; // 22 limbs + 1 entrada con ojos/boca (jointTable[PLAYER_LIMB_MAX])

struct AnchorLook {
    uint32_t tunic = 0x1E691B; // 0xRRGGBB
    uint32_t hair = 0xF0D060;
    uint32_t boots = 0x6B4423;

    bool operator==(const AnchorLook& o) const {
        return tunic == o.tunic && hair == o.hair && boots == o.boots;
    }
};

struct AnchorRemote {
    uint64_t clientId = 0;
    std::string name;
    std::string teamId;
    bool online = true;
    bool pvp = false;
    int sceneId = -1;
    int form = 4; // PLAYER_FORM_HUMAN
    int health = 48;
    AnchorLook look;
    float x = 0, y = 0, z = 0;
    int16_t rotY = 0;
    int16_t joints[kAnchorJointCount * 3] = {};
    bool hasPose = false;
    uint64_t lastPoseMs = 0;
};

enum class AnchorStatus { Disconnected, Connecting, Connected };

class AnchorClient {
  public:
    static AnchorClient& Get();

    void Connect();    // lee la configuración de los CVars gAnchor.*
    void Disconnect(); // y cierra la sesión
    AnchorStatus GetStatus() const;
    std::string GetStatusText() const;
    uint64_t SelfId() const;

    // Seguro desde cualquier hilo.
    void Send(nlohmann::json packet);
    void SetLocalState(const nlohmann::json& clientState);

    // Sólo hilo del juego.
    std::vector<nlohmann::json> PopGameEvents();

    // Copias seguras de los jugadores remotos.
    std::vector<AnchorRemote> Remotes() const;
    bool GetRemote(uint64_t id, AnchorRemote* out) const;

  private:
    AnchorClient();
    ~AnchorClient();
    struct Impl;
    Impl* mImpl;
};

// Utilidades de configuración (CVars).
AnchorLook Anchor_GetLocalLook();
void Anchor_SetLocalLook(const AnchorLook& look);
std::string Anchor_GetName();
uint64_t Anchor_NowMs();

// Lado del juego (AnchorGame.cpp / AnchorRecolor.cpp / AnchorMenu.cpp).
void Anchor_DrawMenu();
void Anchor_NotifyLocalStateChanged(); // reenvía nombre/colores/PvP al servidor
// Devuelve una copia de la display list con colores de túnica/pelo/botas cambiados (o la original).
// Se usa void* para no depender de los headers del motor desde este archivo.
void* Anchor_GetRecoloredDL(void* srcGfx, const AnchorLook& look);

#endif // __cplusplus
