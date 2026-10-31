#include "Anchor.h"
#include "AnchorSocket.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

uint64_t Anchor_NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t ReadColorCVar(const char* name, uint32_t def) {
    return ((uint32_t)CVarGetInteger(name, (int)def)) & 0xFFFFFF;
}

AnchorLook Anchor_GetLocalLook() {
    AnchorLook l;
    l.tunic = ReadColorCVar("gAnchor.Look.Tunic", l.tunic);
    l.hair = ReadColorCVar("gAnchor.Look.Hair", l.hair);
    l.boots = ReadColorCVar("gAnchor.Look.Boots", l.boots);
    return l;
}

void Anchor_SetLocalLook(const AnchorLook& look) {
    CVarSetInteger("gAnchor.Look.Tunic", (int)look.tunic);
    CVarSetInteger("gAnchor.Look.Hair", (int)look.hair);
    CVarSetInteger("gAnchor.Look.Boots", (int)look.boots);
}

std::string Anchor_GetName() {
    std::string n = CVarGetString("gAnchor.Name", "");
    if (n.empty()) {
        n = "Link";
    }
    if (n.size() > 24) {
        n.resize(24);
    }
    return n;
}

// ---------------------------------------------------------------------------------------------

struct AnchorClient::Impl {
    std::atomic<AnchorStatus> status{ AnchorStatus::Disconnected };
    std::atomic<bool> wantConnected{ false };
    std::atomic<uint64_t> selfId{ 0 };

    mutable std::mutex textMutex;
    std::string statusText = "Desconectado";

    std::thread worker;
    std::thread writer;

    std::mutex outMutex;
    std::condition_variable outCv;
    std::deque<std::string> outQueue;

    std::mutex sockMutex;
    AnchorSocket* activeSock = nullptr;

    mutable std::mutex remoteMutex;
    std::unordered_map<uint64_t, AnchorRemote> remotes;

    std::mutex eventMutex;
    std::vector<json> events;

    std::mutex stateMutex;
    json localState = json::object();

    std::string host;
    uint16_t port = 43383;
    std::string room;

    void SetStatus(AnchorStatus s, const std::string& text) {
        status = s;
        std::lock_guard<std::mutex> lock(textMutex);
        statusText = text;
    }

    void SleepInterruptible(int ms) {
        for (int waited = 0; waited < ms && wantConnected; waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // --- aplicar estados de clientes remotos ---
    static void ApplyState(AnchorRemote& r, const json& s) {
        if (s.contains("name") && s["name"].is_string()) {
            r.name = s["name"].get<std::string>();
        }
        if (s.contains("teamId") && s["teamId"].is_string()) {
            r.teamId = s["teamId"].get<std::string>();
        }
        if (s.contains("sceneId") && s["sceneId"].is_number()) {
            r.sceneId = s["sceneId"].get<int>();
        }
        if (s.contains("form") && s["form"].is_number()) {
            r.form = s["form"].get<int>();
        }
        if (s.contains("health") && s["health"].is_number()) {
            r.health = s["health"].get<int>();
        }
        if (s.contains("pvp") && s["pvp"].is_boolean()) {
            r.pvp = s["pvp"].get<bool>();
        }
        if (s.contains("tunic") && s["tunic"].is_number()) {
            r.look.tunic = (uint32_t)s["tunic"].get<int64_t>() & 0xFFFFFF;
        }
        if (s.contains("hair") && s["hair"].is_number()) {
            r.look.hair = (uint32_t)s["hair"].get<int64_t>() & 0xFFFFFF;
        }
        if (s.contains("boots") && s["boots"].is_number()) {
            r.look.boots = (uint32_t)s["boots"].get<int64_t>() & 0xFFFFFF;
        }
    }

    void HandleAllClientState(const json& j) {
        if (!j.contains("state") || !j["state"].is_array()) {
            return;
        }
        std::unordered_set<uint64_t> seen;
        std::lock_guard<std::mutex> lock(remoteMutex);
        for (const auto& s : j["state"]) {
            if (!s.is_object() || !s.contains("clientId") || !s["clientId"].is_number()) {
                continue;
            }
            uint64_t id = s["clientId"].get<uint64_t>();
            if (s.value("self", false)) {
                selfId = id;
                continue;
            }
            if (id == 0 || !s.value("online", true)) {
                continue;
            }
            seen.insert(id);
            AnchorRemote& r = remotes[id];
            r.clientId = id;
            r.online = true;
            ApplyState(r, s);
        }
        for (auto it = remotes.begin(); it != remotes.end();) {
            if (seen.find(it->first) == seen.end()) {
                it = remotes.erase(it);
            } else {
                ++it;
            }
        }
    }

    void HandlePose(uint64_t from, const json& j) {
        std::lock_guard<std::mutex> lock(remoteMutex);
        auto it = remotes.find(from);
        if (it == remotes.end()) {
            return;
        }
        AnchorRemote& r = it->second;
        r.x = j.value("x", r.x);
        r.y = j.value("y", r.y);
        r.z = j.value("z", r.z);
        r.rotY = (int16_t)j.value("ry", (int)r.rotY);
        r.sceneId = j.value("sceneId", r.sceneId);
        r.form = j.value("form", r.form);
        r.health = j.value("health", r.health);
        if (j.contains("j") && j["j"].is_array() && j["j"].size() >= (size_t)(kAnchorJointCount * 3)) {
            for (int i = 0; i < kAnchorJointCount * 3; i++) {
                r.joints[i] = (int16_t)j["j"][i].get<int>();
            }
        }
        r.hasPose = true;
        r.lastPoseMs = Anchor_NowMs();
    }

    void HandleUpdateClientState(uint64_t from, const json& j) {
        if (!j.contains("state") || !j["state"].is_object()) {
            return;
        }
        std::lock_guard<std::mutex> lock(remoteMutex);
        auto it = remotes.find(from);
        if (it == remotes.end()) {
            return;
        }
        ApplyState(it->second, j["state"]);
    }

    void HandlePacket(const std::string& text) {
        try {
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded() || !j.is_object()) {
                return;
            }
            const std::string type = j.value("type", "");
            uint64_t from = 0;
            if (j.contains("clientId") && j["clientId"].is_number()) {
                from = j["clientId"].get<uint64_t>();
            }
            if (from != 0 && from == selfId) {
                return;
            }

            if (type == "ALL_CLIENT_STATE") {
                HandleAllClientState(j);
            } else if (type == "PLAYER_POSE") {
                HandlePose(from, j);
            } else if (type == "UPDATE_CLIENT_STATE") {
                HandleUpdateClientState(from, j);
            } else if (type == "HEARTBEAT" || type == "STATS" || type == "UPDATE_ROOM_STATE") {
                // sin acción
            } else {
                std::lock_guard<std::mutex> lock(eventMutex);
                if (events.size() < 2048) {
                    events.push_back(std::move(j));
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_WARN("[Anchor] paquete ignorado: {}", e.what());
        }
    }

    // --- hilos ---
    json BuildHandshake() {
        json state;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = localState;
        }
        state["online"] = true;
        json hs;
        hs["type"] = "HANDSHAKE";
        hs["roomId"] = room;
        hs["clientId"] = selfId.load();
        hs["roomState"] = json::object();
        hs["clientState"] = state;
        return hs;
    }

    void WorkerMain() {
        while (wantConnected) {
            SetStatus(AnchorStatus::Connecting, "Conectando a " + host + ":" + std::to_string(port) + "...");
            AnchorSocket sock;
            std::string err;
            if (!sock.Connect(host, port, 8000, err)) {
                SetStatus(AnchorStatus::Connecting, err + ". Reintentando...");
                SleepInterruptible(4000);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(outMutex);
                outQueue.clear();
            }
            {
                std::lock_guard<std::mutex> lock(sockMutex);
                activeSock = &sock;
            }

            std::string hs = BuildHandshake().dump();
            hs.push_back('\0');
            bool ok;
            {
                std::lock_guard<std::mutex> lock(sockMutex);
                ok = sock.SendAll(hs.data(), hs.size());
            }

            if (ok) {
                SetStatus(AnchorStatus::Connected, "Conectado a la sala \"" + room + "\"");
                std::string acc;
                std::vector<char> buf(64 * 1024);
                while (wantConnected) {
                    int n = sock.Recv(buf.data(), buf.size());
                    if (n < 0) {
                        break;
                    }
                    if (n == 0) {
                        continue;
                    }
                    acc.append(buf.data(), (size_t)n);
                    size_t pos;
                    while ((pos = acc.find('\0')) != std::string::npos) {
                        if (pos > 0) {
                            HandlePacket(acc.substr(0, pos));
                        }
                        acc.erase(0, pos + 1);
                    }
                    if (acc.size() > 8 * 1024 * 1024) {
                        break; // paquete absurdo, reconectar
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(sockMutex);
                activeSock = nullptr;
            }
            sock.Close();
            {
                std::lock_guard<std::mutex> lock(remoteMutex);
                remotes.clear();
            }

            if (wantConnected) {
                SetStatus(AnchorStatus::Connecting, "Conexion perdida. Reintentando...");
                SleepInterruptible(3000);
            }
        }
        SetStatus(AnchorStatus::Disconnected, "Desconectado");
    }

    void WriterMain() {
        while (true) {
            std::string packet;
            {
                std::unique_lock<std::mutex> lock(outMutex);
                outCv.wait(lock, [&] { return !outQueue.empty() || !wantConnected; });
                if (!wantConnected) {
                    return;
                }
                packet = std::move(outQueue.front());
                outQueue.pop_front();
            }
            packet.push_back('\0');
            std::lock_guard<std::mutex> lock(sockMutex);
            if (activeSock != nullptr) {
                activeSock->SendAll(packet.data(), packet.size());
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------

AnchorClient::AnchorClient() : mImpl(new Impl()) {
}

AnchorClient::~AnchorClient() {
    Disconnect();
    delete mImpl;
}

AnchorClient& AnchorClient::Get() {
    static AnchorClient* instance = new AnchorClient();
    return *instance;
}

void AnchorClient::Connect() {
    Disconnect();

    mImpl->host = CVarGetString("gAnchor.Host", "anchor.hm64.org");
    if (mImpl->host.empty()) {
        mImpl->host = "anchor.hm64.org";
    }
    int port = CVarGetInteger("gAnchor.Port", 43383);
    mImpl->port = (uint16_t)((port > 0 && port < 65536) ? port : 43383);
    mImpl->room = CVarGetString("gAnchor.Room", "");
    if (mImpl->room.empty()) {
        mImpl->SetStatus(AnchorStatus::Disconnected, "Escribe el nombre de una sala primero");
        return;
    }

    mImpl->selfId = 0;
    mImpl->wantConnected = true;
    mImpl->worker = std::thread([this] { mImpl->WorkerMain(); });
    mImpl->writer = std::thread([this] { mImpl->WriterMain(); });
}

void AnchorClient::Disconnect() {
    mImpl->wantConnected = false;
    mImpl->outCv.notify_all();
    {
        std::lock_guard<std::mutex> lock(mImpl->sockMutex);
        if (mImpl->activeSock != nullptr) {
            mImpl->activeSock->Shutdown();
        }
    }
    if (mImpl->worker.joinable()) {
        mImpl->worker.join();
    }
    if (mImpl->writer.joinable()) {
        mImpl->writer.join();
    }
    {
        std::lock_guard<std::mutex> lock(mImpl->remoteMutex);
        mImpl->remotes.clear();
    }
    if (mImpl->status != AnchorStatus::Disconnected) {
        mImpl->SetStatus(AnchorStatus::Disconnected, "Desconectado");
    }
}

AnchorStatus AnchorClient::GetStatus() const {
    return mImpl->status;
}

std::string AnchorClient::GetStatusText() const {
    std::lock_guard<std::mutex> lock(mImpl->textMutex);
    return mImpl->statusText;
}

uint64_t AnchorClient::SelfId() const {
    return mImpl->selfId;
}

void AnchorClient::Send(json packet) {
    if (mImpl->status != AnchorStatus::Connected) {
        return;
    }
    if (!packet.contains("clientId")) {
        packet["clientId"] = mImpl->selfId.load();
    }
    std::string text = packet.dump();
    {
        std::lock_guard<std::mutex> lock(mImpl->outMutex);
        if (mImpl->outQueue.size() > 1024) {
            return;
        }
        mImpl->outQueue.push_back(std::move(text));
    }
    mImpl->outCv.notify_one();
}

void AnchorClient::SetLocalState(const json& clientState) {
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->localState = clientState;
    }
    json p;
    p["type"] = "UPDATE_CLIENT_STATE";
    p["state"] = clientState;
    Send(std::move(p));
}

std::vector<json> AnchorClient::PopGameEvents() {
    std::vector<json> out;
    std::lock_guard<std::mutex> lock(mImpl->eventMutex);
    out.swap(mImpl->events);
    return out;
}

std::vector<AnchorRemote> AnchorClient::Remotes() const {
    std::vector<AnchorRemote> out;
    std::lock_guard<std::mutex> lock(mImpl->remoteMutex);
    out.reserve(mImpl->remotes.size());
    for (const auto& kv : mImpl->remotes) {
        out.push_back(kv.second);
    }
    return out;
}

bool AnchorClient::GetRemote(uint64_t id, AnchorRemote* out) const {
    std::lock_guard<std::mutex> lock(mImpl->remoteMutex);
    auto it = mImpl->remotes.find(id);
    if (it == mImpl->remotes.end()) {
        return false;
    }
    *out = it->second;
    return true;
}
