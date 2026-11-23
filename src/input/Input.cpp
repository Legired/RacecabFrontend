#include "input/Input.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace {
struct ActionInfo {
    const char* id;
    const char* label;
    bool repeat;
};
constexpr ActionInfo kActions[] = {
    {"Prev", "Jeu précédent", true},          {"Next", "Jeu suivant", true},
    {"CatPrev", "Catégorie précédente", true}, {"CatNext", "Catégorie suivante", true},
    {"Select", "Valider / lancer", false},     {"Back", "Retour", false},
    {"Favorite", "Favori", false},             {"Menu", "Menu de la borne", false},
    {"Quit", "Quitter le frontend", false},
};

const char* HatName(int m) {
    switch (m) {
        case SDL_HAT_LEFT: return "left";
        case SDL_HAT_RIGHT: return "right";
        case SDL_HAT_UP: return "up";
        case SDL_HAT_DOWN: return "down";
    }
    return "?";
}
const char* HatLabel(int m) {
    switch (m) {
        case SDL_HAT_LEFT: return "gauche";
        case SDL_HAT_RIGHT: return "droite";
        case SDL_HAT_UP: return "haut";
        case SDL_HAT_DOWN: return "bas";
    }
    return "?";
}
int HatFromName(const std::string& s) {
    if (s == "left") return SDL_HAT_LEFT;
    if (s == "right") return SDL_HAT_RIGHT;
    if (s == "up") return SDL_HAT_UP;
    if (s == "down") return SDL_HAT_DOWN;
    return 0;
}
int FirstHatDir(int value) {
    for (int m : {SDL_HAT_LEFT, SDL_HAT_RIGHT, SDL_HAT_UP, SDL_HAT_DOWN})
        if (value & m) return m;
    return 0;
}
const char* TypeName(Binding::Type t) {
    switch (t) {
        case Binding::Type::Key: return "key";
        case Binding::Type::PadButton: return "pad";
        case Binding::Type::JoyButton: return "joybutton";
        case Binding::Type::JoyAxis: return "joyaxis";
        case Binding::Type::JoyHat: return "joyhat";
    }
    return "?";
}
}  // namespace

const char* ActionName(Action a) { return kActions[(int)a].id; }
const char* ActionLabel(Action a) { return kActions[(int)a].label; }
bool ActionFromName(const std::string& s, Action& out) {
    for (int i = 0; i < (int)Action::Count; ++i)
        if (s == kActions[i].id) {
            out = (Action)i;
            return true;
        }
    return false;
}

std::string Binding::Describe() const {
    std::string s;
    switch (type) {
        case Type::Key: s = std::format("Clavier : {}", SDL_GetKeyName((SDL_Keycode)code)); break;
        case Type::PadButton: {
            const char* n = SDL_GetGamepadStringForButton((SDL_GamepadButton)code);
            s = std::format("Manette : {}", n ? n : "?");
            break;
        }
        case Type::JoyButton: s = std::format("Joystick : bouton {}", code); break;
        case Type::JoyAxis: s = std::format("Joystick : axe {} {}", code, dir > 0 ? "+" : "-"); break;
        case Type::JoyHat: s = std::format("Joystick : chapeau {} {}", code, HatLabel(dir)); break;
    }
    if (!device.empty()) s += " [" + device + "]";
    return s;
}

void Input::Init(const InputTiming& timing) {
    timing_ = timing;
    ResetDefaults();
}

void Input::ResetDefaults() {
    using T = Binding::Type;
    auto key = [](Action a, SDL_Keycode k) { return Binding{a, T::Key, (int)k, 0, {}}; };
    auto pad = [](Action a, SDL_GamepadButton b) { return Binding{a, T::PadButton, (int)b, 0, {}}; };
    SetBindings({
        key(Action::Prev, SDLK_LEFT),       key(Action::Next, SDLK_RIGHT),
        key(Action::CatPrev, SDLK_UP),      key(Action::CatNext, SDLK_DOWN),
        key(Action::Select, SDLK_RETURN),   key(Action::Select, SDLK_KP_ENTER),
        key(Action::Back, SDLK_ESCAPE),     key(Action::Back, SDLK_BACKSPACE),
        key(Action::Favorite, SDLK_F),      key(Action::Menu, SDLK_TAB),
        key(Action::Quit, SDLK_F10),
        pad(Action::Prev, SDL_GAMEPAD_BUTTON_DPAD_LEFT),   pad(Action::Next, SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
        pad(Action::CatPrev, SDL_GAMEPAD_BUTTON_DPAD_UP),  pad(Action::CatNext, SDL_GAMEPAD_BUTTON_DPAD_DOWN),
        pad(Action::CatPrev, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), pad(Action::CatNext, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
        pad(Action::Select, SDL_GAMEPAD_BUTTON_SOUTH),     pad(Action::Back, SDL_GAMEPAD_BUTTON_EAST),
        pad(Action::Favorite, SDL_GAMEPAD_BUTTON_NORTH),   pad(Action::Menu, SDL_GAMEPAD_BUTTON_START),
        // Joysticks génériques (vJoy / BackForceFeeder, panneaux de boutons USB...)
        {Action::Prev, T::JoyHat, 0, SDL_HAT_LEFT, {}},    {Action::Next, T::JoyHat, 0, SDL_HAT_RIGHT, {}},
        {Action::CatPrev, T::JoyHat, 0, SDL_HAT_UP, {}},   {Action::CatNext, T::JoyHat, 0, SDL_HAT_DOWN, {}},
        {Action::Prev, T::JoyAxis, 0, -1, {}},             {Action::Next, T::JoyAxis, 0, +1, {}},
        {Action::Select, T::JoyButton, 0, 0, {}},          {Action::Back, T::JoyButton, 1, 0, {}},
        {Action::Favorite, T::JoyButton, 2, 0, {}},        {Action::Menu, T::JoyButton, 3, 0, {}},
    });
}

void Input::SetBindings(std::vector<Binding> list) {
    bindings_ = std::move(list);
    bindingReleased_.assign(bindings_.size(), 0);
    Clear();
}

void Input::AddBinding(const Binding& b) {
    for (const Binding& o : bindings_)
        if (o.action == b.action && o.SameInput(b)) return;
    bindings_.push_back(b);
    bindingDown_.push_back(false);
    bindingCounted_.push_back(false);
    bindingReleased_.push_back(0);
}

void Input::RemoveBinding(size_t i) {
    if (i >= bindings_.size()) return;
    bindings_.erase(bindings_.begin() + i);
    bindingReleased_.assign(bindings_.size(), 0);
    Clear();
}

void Input::SetBindingDevice(size_t i, const std::string& device) {
    if (i < bindings_.size()) bindings_[i].device = device;
}

void Input::ReplaceFamily(const Binding& b) {
    // Familles : clavier / manette reconnue / joystick générique (vJoy, volant, panneau USB).
    auto family = [](Binding::Type t) {
        return t == Binding::Type::Key ? 0 : t == Binding::Type::PadButton ? 1 : 2;
    };
    std::vector<Binding> list;
    for (const Binding& o : bindings_) {
        bool sameFamily = family(o.type) == family(b.type);
        if (o.action == b.action && sameFamily) continue;
        // Une entrée physique ne sert qu'à une action : on retire aussi les associations
        // « tout périphérique » qui réagiraient à la même entrée.
        bool overlaps = o.type == b.type && o.code == b.code && o.dir == b.dir &&
                        (o.device.empty() || b.device.empty() || o.device == b.device);
        if (overlaps) continue;
        list.push_back(o);
    }
    list.push_back(b);
    SetBindings(std::move(list));
}

std::optional<Action> Input::ActionUsing(const Binding& b) const {
    for (const Binding& o : bindings_)
        if (o.SameInput(b)) return o.action;
    return std::nullopt;
}

bool Input::Load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        logx::Error("input.json invalide ({}) : mapping par défaut conservé", e.what());
        return false;
    }
    std::vector<Binding> list;
    for (const auto& o : j.value("bindings", nlohmann::json::array())) {
        Binding b;
        if (!ActionFromName(o.value("action", ""), b.action)) continue;
        std::string type = o.value("type", "");
        b.device = o.value("device", "");
        if (type == "key") {
            b.type = Binding::Type::Key;
            b.code = (int)SDL_GetKeyFromName(o.value("key", "").c_str());
            if (b.code == SDLK_UNKNOWN) continue;
        } else if (type == "pad") {
            b.type = Binding::Type::PadButton;
            b.code = (int)SDL_GetGamepadButtonFromString(o.value("button", "").c_str());
            if (b.code == SDL_GAMEPAD_BUTTON_INVALID) continue;
        } else if (type == "joybutton") {
            b.type = Binding::Type::JoyButton;
            b.code = o.value("button", 0);
        } else if (type == "joyaxis") {
            b.type = Binding::Type::JoyAxis;
            b.code = o.value("axis", 0);
            b.dir = o.value("dir", 1) >= 0 ? 1 : -1;
        } else if (type == "joyhat") {
            b.type = Binding::Type::JoyHat;
            b.code = o.value("hat", 0);
            b.dir = HatFromName(o.value("dir", ""));
            if (!b.dir) continue;
        } else {
            continue;
        }
        list.push_back(b);
    }
    if (list.empty()) return false;
    SetBindings(std::move(list));
    logx::Info("Mapping chargé : {} associations", bindings_.size());
    return true;
}

bool Input::Save(const fs::path& file) const {
    nlohmann::json arr = nlohmann::json::array();
    for (const Binding& b : bindings_) {
        nlohmann::json o = {{"action", ActionName(b.action)}, {"type", TypeName(b.type)}};
        if (!b.device.empty()) o["device"] = b.device;
        switch (b.type) {
            case Binding::Type::Key: o["key"] = SDL_GetKeyName((SDL_Keycode)b.code); break;
            case Binding::Type::PadButton: {
                const char* n = SDL_GetGamepadStringForButton((SDL_GamepadButton)b.code);
                o["button"] = n ? n : "";
                break;
            }
            case Binding::Type::JoyButton: o["button"] = b.code; break;
            case Binding::Type::JoyAxis: o["axis"] = b.code; o["dir"] = b.dir; break;
            case Binding::Type::JoyHat: o["hat"] = b.code; o["dir"] = HatName(b.dir); break;
        }
        arr.push_back(o);
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << nlohmann::json{{"bindings", arr}}.dump(2);
    return (bool)f;
}

std::vector<std::string> Input::DeviceNames() const {
    std::vector<std::string> out;
    for (auto& [id, d] : devices_) out.push_back(d.name + (d.isPad ? "  (manette reconnue)" : "  (joystick générique)"));
    return out;
}

void Input::TestAddDevice(SDL_JoystickID id, const std::string& name, bool isGamepad) {
    Device d;
    d.name = name;
    d.isPad = isGamepad;
    devices_[id] = d;
}

bool Input::IsPad(SDL_JoystickID id) const {
    auto it = devices_.find(id);
    return it != devices_.end() && it->second.isPad;
}

std::string Input::DeviceName(SDL_JoystickID id) const {
    auto it = devices_.find(id);
    return it != devices_.end() ? it->second.name : std::string("Périphérique inconnu");
}

bool Input::DeviceMatches(const Binding& b, SDL_JoystickID id, bool isPadEvent) const {
    if (!b.device.empty()) return DeviceName(id) == b.device;
    // Sans périphérique précisé, les associations « joystick générique » ignorent les manettes
    // reconnues, qui ont leurs propres associations (évite les doubles actions).
    if (!isPadEvent && IsPad(id)) return false;
    return true;
}

float Input::AxisBaseline(SDL_JoystickID id, int axis, float current) {
    auto key = std::make_pair(id, axis);
    auto it = axisBase_.find(key);
    if (it == axisBase_.end()) it = axisBase_.emplace(key, current).first;
    return it->second;
}

bool Input::IsUserActivity(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN: return !e.key.repeat;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_DOWN: return true;
        case SDL_EVENT_JOYSTICK_HAT_MOTION: return e.jhat.value != SDL_HAT_CENTERED;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
            float v = std::clamp(e.jaxis.value / 32767.0f, -1.0f, 1.0f);
            return std::abs(v - AxisBaseline(e.jaxis.which, e.jaxis.axis, v)) > 0.35f;  // pas le bruit d'un axe au repos
        }
        default: return false;
    }
}

void Input::BeginCapture() {
    capturing_ = true;
    captured_.reset();
    Clear();
    // Position de repos actuelle de chaque axe (pédales au repos à -1, volant au centre...)
    axisBase_.clear();
    for (auto& [id, d] : devices_) {
        if (!d.joy) continue;
        int n = SDL_GetNumJoystickAxes(d.joy);
        for (int a = 0; a < n; ++a) axisBase_[{id, a}] = SDL_GetJoystickAxis(d.joy, a) / 32767.0f;
    }
}

void Input::EndCapture() {
    capturing_ = false;
    captured_.reset();
    Clear();
}

std::optional<Binding> Input::TakeCaptured() {
    auto r = captured_;
    captured_.reset();
    return r;
}

void Input::Capture(const Binding& b) {
    if (capturing_ && !captured_) {
        captured_ = b;
        logx::Info("Entrée détectée pour l'assignation : {}", b.Describe());
    }
}

void Input::HandleEvent(const SDL_Event& e) {
    const uint64_t now = SDL_GetTicks();
    using T = Binding::Type;
    switch (e.type) {
        case SDL_EVENT_JOYSTICK_ADDED: {
            SDL_JoystickID id = e.jdevice.which;
            if (devices_.count(id)) break;
            Device d;
            d.joy = SDL_OpenJoystick(id);
            d.isPad = SDL_IsGamepad(id);
            if (d.isPad) d.pad = SDL_OpenGamepad(id);
            const char* n = d.joy ? SDL_GetJoystickName(d.joy) : nullptr;
            d.name = n ? n : "Périphérique inconnu";
            logx::Info("Périphérique connecté : {} ({}, {} boutons, {} axes, {} chapeaux)", d.name,
                       d.isPad ? "manette reconnue" : "joystick générique", d.joy ? SDL_GetNumJoystickButtons(d.joy) : 0,
                       d.joy ? SDL_GetNumJoystickAxes(d.joy) : 0, d.joy ? SDL_GetNumJoystickHats(d.joy) : 0);
            devices_[id] = d;
            break;
        }
        case SDL_EVENT_JOYSTICK_REMOVED: {
            auto it = devices_.find(e.jdevice.which);
            if (it == devices_.end()) break;
            logx::Info("Périphérique déconnecté : {}", it->second.name);
            if (it->second.pad) SDL_CloseGamepad(it->second.pad);
            if (it->second.joy) SDL_CloseJoystick(it->second.joy);
            for (auto r = rawActive_.begin(); r != rawActive_.end();)
                r = r->find(std::format(":{}:", (unsigned)e.jdevice.which)) != std::string::npos ? rawActive_.erase(r) : std::next(r);
            devices_.erase(it);
            Clear();
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (e.key.repeat) break;  // la répétition est gérée ici, pas par Windows
            bool down = e.type == SDL_EVENT_KEY_DOWN;
            std::string rk = std::format("k:{}", (unsigned)e.key.key);
            if (down) {
                rawActive_.insert(rk);
                lastRaw_ = std::format("Clavier : {}", SDL_GetKeyName(e.key.key));
                Capture({Action::Select, T::Key, (int)e.key.key, 0, {}});
            } else {
                rawActive_.erase(rk);
            }
            if (capturing_) break;
            for (size_t i = 0; i < bindings_.size(); ++i)
                if (bindings_[i].type == T::Key && bindings_[i].code == (int)e.key.key) SetBinding(i, down, now);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            std::string rk = std::format("p:{}:{}", (unsigned)e.gbutton.which, e.gbutton.button);
            if (down) {
                rawActive_.insert(rk);
                const char* n = SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button);
                lastRaw_ = std::format("{} : bouton manette {}", DeviceName(e.gbutton.which), n ? n : "?");
                Capture({Action::Select, T::PadButton, e.gbutton.button, 0, DeviceName(e.gbutton.which)});
            } else {
                rawActive_.erase(rk);
            }
            if (capturing_) break;
            for (size_t i = 0; i < bindings_.size(); ++i) {
                const Binding& b = bindings_[i];
                if (b.type == T::PadButton && b.code == e.gbutton.button && DeviceMatches(b, e.gbutton.which, true))
                    SetBinding(i, down, now);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP: {
            bool down = e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN;
            bool pad = IsPad(e.jbutton.which);
            std::string rk = std::format("b:{}:{}", (unsigned)e.jbutton.which, e.jbutton.button);
            if (down) {
                if (!pad) {  // une manette reconnue est déjà suivie par ses événements « manette »
                    rawActive_.insert(rk);
                    lastRaw_ = std::format("{} : bouton {}", DeviceName(e.jbutton.which), e.jbutton.button);
                    Capture({Action::Select, T::JoyButton, e.jbutton.button, 0, DeviceName(e.jbutton.which)});
                }
            } else {
                rawActive_.erase(rk);
            }
            if (capturing_) break;
            for (size_t i = 0; i < bindings_.size(); ++i) {
                const Binding& b = bindings_[i];
                if (b.type == T::JoyButton && b.code == e.jbutton.button && DeviceMatches(b, e.jbutton.which, false))
                    SetBinding(i, down, now);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
            float v = std::clamp(e.jaxis.value / 32767.0f, -1.0f, 1.0f);
            float base = AxisBaseline(e.jaxis.which, e.jaxis.axis, v);
            std::string rk = std::format("a:{}:{}", (unsigned)e.jaxis.which, e.jaxis.axis);
            if (std::abs(v - base) > 0.5f) rawActive_.insert(rk);
            else if (std::abs(v - base) < 0.3f) rawActive_.erase(rk);
            if (std::abs(v) > timing_.axisThreshold)
                lastRaw_ = std::format("{} : axe {} {}", DeviceName(e.jaxis.which), e.jaxis.axis, v > 0 ? "+" : "-");
            if (capturing_) {
                int dir = v > 0 ? 1 : -1;
                // Retenu seulement si l'axe a vraiment bougé et qu'il n'est pas déjà « enfoncé » au repos.
                if (std::abs(v - base) > 0.5f && std::abs(v) > timing_.axisThreshold &&
                    base * dir < timing_.axisThreshold * 0.7f)
                    Capture({Action::Select, T::JoyAxis, e.jaxis.axis, dir, DeviceName(e.jaxis.which)});
                break;
            }
            for (size_t i = 0; i < bindings_.size(); ++i) {
                const Binding& b = bindings_[i];
                if (b.type != T::JoyAxis || b.code != e.jaxis.axis || !DeviceMatches(b, e.jaxis.which, false)) continue;
                float dv = v * b.dir;
                // Hystérésis : on relâche à 70 % du seuil pour éviter les oscillations autour du seuil.
                bool down = bindingDown_[i] ? dv > timing_.axisThreshold * 0.7f : dv > timing_.axisThreshold;
                SetBinding(i, down, now);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_HAT_MOTION: {
            bool pad = IsPad(e.jhat.which);
            std::string rk = std::format("h:{}:{}", (unsigned)e.jhat.which, e.jhat.hat);
            if (e.jhat.value != SDL_HAT_CENTERED && !pad) {
                rawActive_.insert(rk);
                lastRaw_ = std::format("{} : chapeau {} {}", DeviceName(e.jhat.which), e.jhat.hat, HatLabel(FirstHatDir(e.jhat.value)));
                Capture({Action::Select, T::JoyHat, e.jhat.hat, FirstHatDir(e.jhat.value), DeviceName(e.jhat.which)});
            } else {
                rawActive_.erase(rk);
            }
            if (capturing_) break;
            for (size_t i = 0; i < bindings_.size(); ++i) {
                const Binding& b = bindings_[i];
                if (b.type == T::JoyHat && b.code == e.jhat.hat && DeviceMatches(b, e.jhat.which, false))
                    SetBinding(i, (e.jhat.value & b.dir) != 0, now);
            }
            break;
        }
        default: break;
    }
}

void Input::SetBinding(size_t i, bool down, uint64_t now) {
    if (bindingDown_[i] == down) return;
    bindingDown_[i] = down;
    Action a = bindings_[i].action;
    ActState& s = act_[(int)a];
    if (down) {
        // Anti-rebond par entrée physique : un contact qui rebondit juste après son relâchement est ignoré.
        bool bounce = bindingReleased_[i] && now - bindingReleased_[i] < (uint64_t)timing_.debounceMs;
        bindingCounted_[i] = !bounce;
        if (bounce) return;
        if (++s.downCount == 1) Press(a, now);
    } else {
        bindingReleased_[i] = now;
        if (!bindingCounted_[i]) return;
        bindingCounted_[i] = false;
        if (s.downCount > 0 && --s.downCount == 0) Release(a, now);
    }
}

void Input::Push(Action a, bool hold) {
    if (queue_.size() < 16) queue_.push_back({a, hold});
}

void Input::Press(Action a, uint64_t now) {
    ActState& s = act_[(int)a];
    lastActivity_ = now;
    s.accepted = false;
    s.holdFired = false;
    if (now < blockedUntil_) return;  // garde après un jeu
    if (a == Action::Select && s.lastFired && now - s.lastFired < (uint64_t)timing_.selectGuardMs) return;
    s.accepted = true;
    s.pressedAt = now;
    s.nextRepeat = now + timing_.repeatDelayMs;
    if (!FiresOnRelease(a)) {
        s.lastFired = now;
        Push(a, false);
    }
}

void Input::Release(Action a, uint64_t now) {
    ActState& s = act_[(int)a];
    s.releasedAt = now;
    if (s.accepted && FiresOnRelease(a) && !s.holdFired) {
        s.lastFired = now;
        Push(a, false);
    }
    s.accepted = false;
}

void Input::Update(uint64_t now) {
    for (int i = 0; i < (int)Action::Count; ++i) {
        ActState& s = act_[i];
        if (s.downCount <= 0 || !s.accepted) continue;
        if (!s.holdFired && timing_.holdMs > 0 && now - s.pressedAt >= (uint64_t)timing_.holdMs) {
            s.holdFired = true;
            Push((Action)i, true);
        }
        if (!kActions[i].repeat || now < s.nextRepeat) continue;
        int rate = timing_.repeatRateMs;
        if (timing_.accelAfterMs > 0 && now - s.pressedAt > (uint64_t)timing_.accelAfterMs) rate = std::max(15, rate / 3);
        // Si une frame a pris du retard, on n'accumule pas une rafale de répétitions.
        if (queue_.size() < 4) Push((Action)i, false);
        s.nextRepeat = std::max(s.nextRepeat + rate, now + rate / 2);
        lastActivity_ = now;
    }
}

bool Input::Pop(InputEvent& out) {
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void Input::Clear() {
    bindingDown_.assign(bindings_.size(), false);
    bindingCounted_.assign(bindings_.size(), false);
    bindingReleased_.resize(bindings_.size(), 0);
    for (auto& s : act_) {
        s.downCount = 0;
        s.nextRepeat = 0;
        s.accepted = false;
        s.holdFired = false;
    }
    queue_.clear();
    rawActive_.clear();  // un relâchement peut avoir été perdu pendant un jeu (fenêtre sans focus)
}
