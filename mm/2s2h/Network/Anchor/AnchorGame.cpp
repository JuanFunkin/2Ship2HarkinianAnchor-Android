#include "Anchor.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/NameTag/NameTag.h"
#include "2s2h/ShipInit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "z64actor.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern FlexSkeletonHeader* gPlayerSkeletons[];
s32 Player_InflictDamage(PlayState* play, s32 damage);
}

using json = nlohmann::json;

namespace {

constexpr s16 kGhostMagic = 0x7ABC; // marca en home.rot.z para reconocer a nuestros actores

struct Ghost {
    uint64_t clientId = 0;
    Actor* actor = nullptr;
    Player* dummy = nullptr; // Player "falso" que sólo sirve para dibujar el modelo
    int form = -1;
    AnchorLook look;
    bool tagged = false;
    float tx = 0, ty = 0, tz = 0;
};

std::unordered_map<uint64_t, Ghost*> sGhostsById;
std::unordered_map<Actor*, Ghost*> sGhostsByActor;
Ghost* sPendingGhost = nullptr;

bool sInPlay = false;
bool sApplyingRemote = false;
bool sWasConnected = false;
bool sSwingSent = false;
int sPvpCooldown = 0;
int sStateTimer = 0;

// Último estado enviado, para detectar cambios.
int sLastScene = -1;
int sLastForm = -1;
int sLastHealth = -1;

bool CvarBool(const char* name, bool def) {
    return CVarGetInteger(name, def ? 1 : 0) != 0;
}

// ------------------------------------------------------------------------------ estado local

json BuildLocalState() {
    json s;
    AnchorLook look = Anchor_GetLocalLook();
    s["name"] = Anchor_GetName();
    s["teamId"] = std::string(CVarGetString("gAnchor.Team", "default"));
    s["pvp"] = CvarBool("gAnchor.Pvp", false);
    s["tunic"] = (int64_t)look.tunic;
    s["hair"] = (int64_t)look.hair;
    s["boots"] = (int64_t)look.boots;
    s["game"] = "2s2h";
    s["isSaveLoaded"] = sInPlay;
    if (sInPlay && gPlayState != nullptr) {
        Player* p = GET_PLAYER(gPlayState);
        s["sceneId"] = (int)gPlayState->sceneId;
        s["form"] = p != nullptr ? (int)p->transformation : 4;
        s["health"] = (int)gSaveContext.save.saveInfo.playerData.health;
    } else {
        s["sceneId"] = -1;
        s["form"] = 4;
        s["health"] = 48;
    }
    return s;
}

void SendLocalState() {
    AnchorClient::Get().SetLocalState(BuildLocalState());
}

void SendPose(Player* p) {
    json j;
    j["type"] = "PLAYER_POSE";
    j["quiet"] = true;
    j["sceneId"] = (int)gPlayState->sceneId;
    j["x"] = (int)std::lround(p->actor.world.pos.x);
    j["y"] = (int)std::lround(p->actor.world.pos.y);
    j["z"] = (int)std::lround(p->actor.world.pos.z);
    j["ry"] = (int)p->actor.shape.rot.y;
    j["form"] = (int)p->transformation;
    j["health"] = (int)gSaveContext.save.saveInfo.playerData.health;

    const int16_t* jt = reinterpret_cast<const int16_t*>(p->skelAnime.jointTable);
    json joints = json::array();
    for (int i = 0; i < kAnchorJointCount * 3; i++) {
        joints.push_back((int)jt[i]);
    }
    j["j"] = std::move(joints);
    AnchorClient::Get().Send(std::move(j));
}

// ------------------------------------------------------------------------------ fantasmas

s32 GhostOverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, Actor* thisx) {
    s32 result = Player_OverrideLimbDrawGameplayDefault(play, limbIndex, dList, pos, rot, thisx);
    auto it = sGhostsByActor.end();
    for (auto i = sGhostsByActor.begin(); i != sGhostsByActor.end(); ++i) {
        if (&i->second->dummy->actor == thisx) {
            it = i;
            break;
        }
    }
    if (it != sGhostsByActor.end() && result == 0 && dList != nullptr && *dList != nullptr) {
        *dList = (Gfx*)Anchor_GetRecoloredDL((void*)*dList, it->second->look);
    }
    return result;
}

void GhostInitSkeleton(PlayState* play, Ghost* g, int form) {
    if (form < 0 || form >= PLAYER_FORM_MAX) {
        form = PLAYER_FORM_HUMAN;
    }
    Player* d = g->dummy;
    std::memset(d, 0, sizeof(Player));
    d->transformation = (PlayerTransformation)form;
    SkelAnime_InitPlayer(play, &d->skelAnime, gPlayerSkeletons[form], (PlayerAnimationHeader*)gPlayerAnim_link_normal_wait, 1 | 8,
                         d->jointTableBuffer, d->morphTableBuffer, PLAYER_LIMB_MAX);
    static Vec3s sBaseTransl = { -57, 3377, 0 };
    d->skelAnime.baseTransl = sBaseTransl;
    g->form = form;
}

void GhostDestroy(Actor* actor, PlayState* play) {
    NameTag_RemoveAllForActor(actor);
    auto it = sGhostsByActor.find(actor);
    if (it != sGhostsByActor.end()) {
        Ghost* g = it->second;
        sGhostsById.erase(g->clientId);
        sGhostsByActor.erase(it);
        delete g->dummy;
        delete g;
    }
}

void GhostUpdate(Actor* actor, PlayState* play) {
    auto it = sGhostsByActor.find(actor);
    if (it == sGhostsByActor.end()) {
        Actor_Kill(actor);
        return;
    }
    Ghost* g = it->second;

    AnchorRemote r;
    if (!AnchorClient::Get().GetRemote(g->clientId, &r) || r.sceneId != (int)play->sceneId || !r.hasPose ||
        (Anchor_NowMs() - r.lastPoseMs) > 5000) {
        Actor_Kill(actor);
        return;
    }

    if (r.form != g->form) {
        GhostInitSkeleton(play, g, r.form);
    }
    g->look = r.look;

    // Suavizado de movimiento
    float dx = r.x - actor->world.pos.x;
    float dy = r.y - actor->world.pos.y;
    float dz = r.z - actor->world.pos.z;
    if (dx * dx + dz * dz > 400.0f * 400.0f) {
        actor->world.pos.x = r.x;
        actor->world.pos.y = r.y;
        actor->world.pos.z = r.z;
    } else {
        actor->world.pos.x += dx * 0.6f;
        actor->world.pos.y += dy * 0.6f;
        actor->world.pos.z += dz * 0.6f;
    }
    actor->shape.rot.y = r.rotY;
    actor->world.rot.y = r.rotY;
    actor->focus.pos = actor->world.pos;
    actor->focus.pos.y += 40.0f;

    Player* d = g->dummy;
    d->actor.world.pos = actor->world.pos;
    d->actor.shape.rot.y = r.rotY;
    d->actor.scale.x = d->actor.scale.y = d->actor.scale.z = 0.01f;
    std::memcpy(d->skelAnime.jointTable, r.joints, sizeof(Vec3s) * kAnchorJointCount);

    if (!g->tagged) {
        NameTagOptions opts = {};
        opts.tag = "anchor";
        opts.yOffset = 0;
        opts.textColor = { (uint8_t)((r.look.tunic >> 16) & 0xFF), (uint8_t)((r.look.tunic >> 8) & 0xFF),
                           (uint8_t)(r.look.tunic & 0xFF), 255 };
        opts.noZBuffer = 0;
        std::string label = r.name.empty() ? std::string("Jugador") : r.name;
        NameTag_RegisterForActorWithOptions(actor, label.c_str(), opts);
        g->tagged = true;
    }
}

void GhostDraw(Actor* actor, PlayState* play) {
    auto it = sGhostsByActor.find(actor);
    if (it == sGhostsByActor.end() || it->second->form < 0) {
        return;
    }
    Player* d = it->second->dummy;

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    CLOSE_DISPS(play->state.gfxCtx);

    Player_DrawImpl(play, d->skelAnime.skeleton, d->skelAnime.jointTable, d->skelAnime.dListCount, 0,
                    (PlayerTransformation)d->transformation, 0, 0, GhostOverrideLimbDraw,
                    Player_PostLimbDrawGameplay, &d->actor);
}

void OnActorInitBindGhost(Actor* actor) {
    if (sPendingGhost == nullptr || actor->id != ACTOR_EN_ITEM00 || actor->home.rot.z != kGhostMagic) {
        return;
    }
    Ghost* g = sPendingGhost;
    sPendingGhost = nullptr;
    g->actor = actor;
    sGhostsByActor[actor] = g;

    actor->update = GhostUpdate;
    actor->draw = GhostDraw;
    actor->destroy = GhostDestroy;
    actor->shape.yOffset = 0.0f;
    actor->world.rot.x = actor->world.rot.z = 0;
    actor->shape.rot.x = actor->shape.rot.z = 0;
    Actor_SetScale(actor, 0.01f);
}

void SpawnGhost(PlayState* play, const AnchorRemote& r) {
    Ghost* g = new Ghost();
    g->clientId = r.clientId;
    g->dummy = new Player();
    std::memset(g->dummy, 0, sizeof(Player));
    GhostInitSkeleton(play, g, r.form);
    g->look = r.look;

    sPendingGhost = g;
    sGhostsById[r.clientId] = g;
    Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ITEM00, r.x, r.y, r.z, 0, r.rotY, kGhostMagic,
                           ITEM00_NOTHING);
    if (a == nullptr || sPendingGhost != nullptr) {
        // no se pudo crear o no se enlazó: limpiar
        sPendingGhost = nullptr;
        sGhostsById.erase(r.clientId);
        delete g->dummy;
        delete g;
        if (a != nullptr) {
            Actor_Kill(a);
        }
    }
}

void SyncGhosts(PlayState* play) {
    for (const AnchorRemote& r : AnchorClient::Get().Remotes()) {
        if (!r.hasPose || r.sceneId != (int)play->sceneId || (Anchor_NowMs() - r.lastPoseMs) > 5000) {
            continue;
        }
        if (sGhostsById.find(r.clientId) == sGhostsById.end()) {
            SpawnGhost(play, r);
        }
    }
}

// ------------------------------------------------------------------------------ PvP

void TickPvp(PlayState* play, Player* player) {
    if (sPvpCooldown > 0) {
        sPvpCooldown--;
    }
    if (!CvarBool("gAnchor.Pvp", false)) {
        return;
    }
    if (player->meleeWeaponState <= 0) {
        sSwingSent = false;
        return;
    }
    if (sSwingSent) {
        return;
    }
    for (auto& kv : sGhostsById) {
        Ghost* g = kv.second;
        AnchorRemote r;
        if (g->actor == nullptr || !AnchorClient::Get().GetRemote(g->clientId, &r) || !r.pvp) {
            continue;
        }
        float dx = g->actor->world.pos.x - player->actor.world.pos.x;
        float dz = g->actor->world.pos.z - player->actor.world.pos.z;
        float dy = g->actor->world.pos.y - player->actor.world.pos.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 85.0f || std::fabs(dy) > 70.0f) {
            continue;
        }
        s16 yawTo = (s16)(std::atan2(dx, dz) * (32768.0f / 3.14159265f));
        s16 diff = (s16)(yawTo - player->actor.shape.rot.y);
        if (std::abs((int)diff) > 0x3000) {
            continue;
        }
        json hit;
        hit["type"] = "PVP_HIT";
        hit["targetClientId"] = r.clientId;
        hit["damage"] = CVarGetInteger("gAnchor.PvpDamage", 8);
        AnchorClient::Get().Send(std::move(hit));
        sSwingSent = true;
        break;
    }
}

void ApplyPvpHit(PlayState* play, const json& j) {
    if (!CvarBool("gAnchor.Pvp", false) || sPvpCooldown > 0) {
        return;
    }
    int dmg = j.value("damage", 8);
    dmg = std::max(1, std::min(dmg, 64));
    sPvpCooldown = 30;
    Player_InflictDamage(play, -dmg);
}

// ------------------------------------------------------------------------------ eventos remotos

void ProcessEvents() {
    for (const json& j : AnchorClient::Get().PopGameEvents()) {
        const std::string type = j.value("type", "");
        if (!sInPlay || gPlayState == nullptr) {
            continue;
        }
        if (type == "PVP_HIT") {
            ApplyPvpHit(gPlayState, j);
        } else if (type == "ITEM_GIVE" && CvarBool("gAnchor.ShareItems", true)) {
            int item = j.value("item", -1);
            if (item >= 0 && item < 0xFF) {
                sApplyingRemote = true;
                Item_Give(gPlayState, (u8)item);
                sApplyingRemote = false;
            }
        } else if (type == "FLAG_SET" && CvarBool("gAnchor.ShareFlags", false)) {
            int flagType = j.value("flagType", 0);
            int flag = j.value("flag", -1);
            if (flag < 0) {
                continue;
            }
            sApplyingRemote = true;
            if (flagType == FLAG_WEEK_EVENT_REG) {
                Flags_SetWeekEventReg(flag);
            } else if (flagType == FLAG_EVENT_INF) {
                Flags_SetEventInf(flag);
            }
            sApplyingRemote = false;
        }
    }
}

void KillAllGhosts() {
    std::vector<Actor*> actors;
    for (auto& kv : sGhostsByActor) {
        actors.push_back(kv.first);
    }
    for (Actor* a : actors) {
        Actor_Kill(a);
    }
}

// ------------------------------------------------------------------------------ bucle principal

void OnUpdate() {
    AnchorClient& c = AnchorClient::Get();
    bool connected = c.GetStatus() == AnchorStatus::Connected;

    if (connected && !sWasConnected) {
        SendLocalState();
    }
    sWasConnected = connected;

    if (!connected) {
        if (sInPlay && !sGhostsByActor.empty()) {
            KillAllGhosts();
        }
        return;
    }

    ProcessEvents();

    if (!sInPlay || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    int scene = (int)gPlayState->sceneId;
    int form = (int)player->transformation;
    int health = (int)gSaveContext.save.saveInfo.playerData.health;
    if (scene != sLastScene || form != sLastForm || ((health != sLastHealth) && (++sStateTimer > 20))) {
        sLastScene = scene;
        sLastForm = form;
        sLastHealth = health;
        sStateTimer = 0;
        SendLocalState();
    }

    SendPose(player);
    SyncGhosts(gPlayState);
    TickPvp(gPlayState, player);
}

void RegisterAnchorHooks() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorInit>(OnActorInitBindGhost);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateUpdate>(OnUpdate);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](s8 sceneId, s8 spawnNum) {
        sInPlay = true;
        sLastScene = -1; // fuerza reenviar el estado
        // los fantasmas de la escena anterior ya fueron destruidos con ella
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>([]() {
        sInPlay = false;
        sSwingSent = false;
        sLastScene = -1;
        AnchorClient& c = AnchorClient::Get();
        if (c.GetStatus() == AnchorStatus::Connected) {
            SendLocalState();
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnItemGive>([](u8 item) {
        if (sApplyingRemote || !CvarBool("gAnchor.ShareItems", true)) {
            return;
        }
        AnchorClient& c = AnchorClient::Get();
        if (c.GetStatus() != AnchorStatus::Connected) {
            return;
        }
        json j;
        j["type"] = "ITEM_GIVE";
        j["targetTeamId"] = std::string(CVarGetString("gAnchor.Team", "default"));
        j["item"] = (int)item;
        c.Send(std::move(j));
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnFlagSet>([](FlagType flagType, u32 flag) {
        if (sApplyingRemote || !CvarBool("gAnchor.ShareFlags", false)) {
            return;
        }
        if (flagType != FLAG_WEEK_EVENT_REG && flagType != FLAG_EVENT_INF) {
            return;
        }
        AnchorClient& c = AnchorClient::Get();
        if (c.GetStatus() != AnchorStatus::Connected) {
            return;
        }
        json j;
        j["type"] = "FLAG_SET";
        j["targetTeamId"] = std::string(CVarGetString("gAnchor.Team", "default"));
        j["flagType"] = (int)flagType;
        j["flag"] = (int)flag;
        c.Send(std::move(j));
    });
}

} // namespace

void Anchor_NotifyLocalStateChanged() {
    SendLocalState();
}

static RegisterShipInitFunc sAnchorInit(RegisterAnchorHooks);
