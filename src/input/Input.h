#pragma once
#include <SDL3/SDL.h>

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Settings.h"

enum class Action { Prev, Next, CatPrev, CatNext, Select, Back, Favorite, Menu, Quit, Count };
const char* ActionName(Action a);       // identifiant JSON ("Next")
const char* ActionLabel(Action a);      // libellé français
bool ActionFromName(const std::string& s, Action& out);

// Une entrée physique associée à une action.
struct Binding {
    enum class Type { Key, PadButton, JoyButton, JoyAxis, JoyHat };
    Action action = Action::Select;
    Type type = Type::Key;
    int code = 0;        // SDL_Keycode, SDL_GamepadButton, n° de bouton, n° d'axe, n° de chapeau
    int dir = 0;         // axe : -1 / +1 ; chapeau : masque SDL_HAT_*
    std::string device;  // nom du périphérique ; vide = n'importe lequel
    std::string Describe() const;
    bool SameInput(const Binding& o) const {  // même entrée physique (quelle que soit l'action)
        return type == o.type && code == o.code && dir == o.dir && device == o.device;
    }
};

// Événement produit : appui (court) ou appui long.
struct InputEvent {
    Action action;
    bool hold = false;
};

// Gestion des entrées : clavier, manettes reconnues (XInput...) et joysticks génériques
// (DirectInput / HID, dont le joystick virtuel vJoy de BackForceFeeder), via SDL3.
// Produit une file d'actions abstraites avec répétition (accélérée), appui long,
// anti-rebond et anti double validation. Mode « capture » pour l'assignation par détection.
class Input {
public:
    void Init(const InputTiming& timing);
    bool Load(const fs::path& file);
    bool Save(const fs::path& file) const;
    void ResetDefaults();

    void HandleEvent(const SDL_Event& e);
    void Update(uint64_t nowMs);  // répétitions et appuis longs
    bool Pop(InputEvent& out);
    void Clear();                 // relâche tout (ex. retour d'un jeu)
    void BlockUntil(uint64_t ms) { blockedUntil_ = ms; }  // ignore les appuis jusqu'à cet instant
    bool Blocked(uint64_t now) const { return now < blockedUntil_; }

    // Capture (assignation par détection) : aucune action n'est produite pendant la capture.
    void BeginCapture();
    void EndCapture();
    bool Capturing() const { return capturing_; }
    std::optional<Binding> TakeCaptured();
    bool AnyRawActive() const { return !rawActive_.empty(); }  // une entrée physique est-elle maintenue ?
    bool ActionHeld(Action a) const { return act_[(int)a].downCount > 0; }  // action actuellement maintenue

    // Édition des associations
    const std::vector<Binding>& Bindings() const { return bindings_; }
    void SetBindings(std::vector<Binding> list);
    void AddBinding(const Binding& b);
    void RemoveBinding(size_t i);
    void SetBindingDevice(size_t i, const std::string& device);
    // Remplace les associations de l'action pour la même famille que b (clavier / manette / joystick).
    void ReplaceFamily(const Binding& b);
    std::optional<Action> ActionUsing(const Binding& b) const;

    InputTiming& Timing() { return timing_; }
    std::vector<std::string> DeviceNames() const;
    const std::string& LastRawInput() const { return lastRaw_; }  // diagnostic : dernière entrée vue
    uint64_t LastActivityMs() const { return lastActivity_; }
    // Vrai pour toute action physique de l'utilisateur, même sur une entrée non associée
    // (touche, bouton, chapeau, axe sorti de sa position de repos). Sert à interrompre la démo.
    bool IsUserActivity(const SDL_Event& e);

    // Tests automatisés : déclare un périphérique sans matériel réel.
    void TestAddDevice(SDL_JoystickID id, const std::string& name, bool isGamepad);

private:
    struct Device {
        SDL_Joystick* joy = nullptr;
        SDL_Gamepad* pad = nullptr;
        std::string name;
        bool isPad = false;
    };
    void SetBinding(size_t i, bool down, uint64_t now);
    void Press(Action a, uint64_t now);
    void Release(Action a, uint64_t now);
    void Push(Action a, bool hold);
    bool DeviceMatches(const Binding& b, SDL_JoystickID id, bool isPadEvent) const;
    bool IsPad(SDL_JoystickID id) const;
    std::string DeviceName(SDL_JoystickID id) const;
    float AxisBaseline(SDL_JoystickID id, int axis, float current);
    void Capture(const Binding& b);
    static bool FiresOnRelease(Action a) { return a == Action::Back; }  // Retour : court = relâché, long = menu

    InputTiming timing_;
    std::vector<Binding> bindings_;
    std::vector<bool> bindingDown_;
    std::vector<bool> bindingCounted_;        // appui retenu (pas un rebond)
    std::vector<uint64_t> bindingReleased_;   // dernier relâchement, pour l'anti-rebond
    std::unordered_map<SDL_JoystickID, Device> devices_;
    struct ActState {
        int downCount = 0;
        uint64_t pressedAt = 0, nextRepeat = 0, releasedAt = 0, lastFired = 0;
        bool accepted = false;   // appui retenu (pas un rebond)
        bool holdFired = false;
    } act_[(int)Action::Count];
    std::deque<InputEvent> queue_;
    std::string lastRaw_;
    uint64_t lastActivity_ = 0;
    uint64_t blockedUntil_ = 0;

    // Capture et activité brute
    bool capturing_ = false;
    std::optional<Binding> captured_;
    std::set<std::string> rawActive_;
    std::map<std::pair<SDL_JoystickID, int>, float> axisBase_;
};
