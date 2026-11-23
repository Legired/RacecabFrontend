// Tests automatisés du module d'entrées, avec des événements SDL simulés (aucun matériel requis).
// Lancer : build\Release\input_tests.exe   (ou ctest -C Release dans build)
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

#include "core/Log.h"
#include "input/Input.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else {                                                                   \
            ++g_fail;                                                            \
            std::printf("  ECHEC ligne %d : %s\n", __LINE__, #cond);              \
        }                                                                        \
    } while (0)

namespace {

constexpr SDL_JoystickID kVJoy = 101, kPad = 102, kOther = 103;

SDL_Event Key(SDL_Keycode k, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = k;
    e.key.down = down;
    return e;
}
SDL_Event JoyButton(SDL_JoystickID id, int b, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_JOYSTICK_BUTTON_DOWN : SDL_EVENT_JOYSTICK_BUTTON_UP;
    e.jbutton.which = id;
    e.jbutton.button = (Uint8)b;
    e.jbutton.down = down;
    return e;
}
SDL_Event JoyAxis(SDL_JoystickID id, int axis, float v) {
    SDL_Event e{};
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    e.jaxis.which = id;
    e.jaxis.axis = (Uint8)axis;
    e.jaxis.value = (Sint16)(v * 32767);
    return e;
}
SDL_Event JoyHat(SDL_JoystickID id, int hat, int value) {
    SDL_Event e{};
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    e.jhat.which = id;
    e.jhat.hat = (Uint8)hat;
    e.jhat.value = (Uint8)value;
    return e;
}

int Drain(Input& in, Action a, bool hold = false) {  // nombre d'événements de ce type dans la file
    int n = 0;
    InputEvent ev;
    while (in.Pop(ev))
        if (ev.action == a && ev.hold == hold) ++n;
    return n;
}
int DrainAll(Input& in) {
    int n = 0;
    InputEvent ev;
    while (in.Pop(ev)) ++n;
    return n;
}

Input Fresh() {
    Input in;
    in.Init(InputTiming{});
    in.TestAddDevice(kVJoy, "vJoy Device", false);
    in.TestAddDevice(kPad, "Xbox Controller", true);
    in.TestAddDevice(kOther, "Panneau USB", false);
    return in;
}

void Section(const char* name) { std::printf("- %s\n", name); }

}  // namespace

int main(int, char**) {
    SDL_Init(SDL_INIT_EVENTS);
    logx::Init(fs::temp_directory_path() / L"racecab-tests");

    {
        Section("Clavier : un appui = une action");
        Input in = Fresh();
        in.HandleEvent(Key(SDLK_RIGHT, true));
        in.HandleEvent(Key(SDLK_RIGHT, false));
        CHECK(Drain(in, Action::Next) == 1);
    }
    {
        Section("Répétition puis accélération sur appui maintenu");
        Input in = Fresh();
        uint64_t t = SDL_GetTicks();
        in.HandleEvent(Key(SDLK_RIGHT, true));
        CHECK(Drain(in, Action::Next) == 1);
        in.Update(t + 300);
        CHECK(Drain(in, Action::Next) == 0);  // avant le délai de répétition (350 ms)
        in.Update(t + 360);
        CHECK(Drain(in, Action::Next) == 1);
        int slow = 0, fast = 0;
        for (uint64_t ms = t + 361; ms < t + 1361; ms += 5) { in.Update(ms); slow += Drain(in, Action::Next); }
        for (uint64_t ms = t + 2000; ms < t + 3000; ms += 5) { in.Update(ms); fast += Drain(in, Action::Next); }
        std::printf("    répétitions/s : %d avant accélération, %d après\n", slow, fast);
        CHECK(slow >= 12 && slow <= 16);  // 70 ms
        CHECK(fast >= 35);                // 23 ms
        in.HandleEvent(Key(SDLK_RIGHT, false));
        in.Update(t + 4000);
        CHECK(Drain(in, Action::Next) == 0);
    }
    {
        Section("Retour : court = au relâchement, long = menu sans retour");
        Input in = Fresh();
        uint64_t t = SDL_GetTicks();
        in.HandleEvent(Key(SDLK_ESCAPE, true));
        CHECK(DrainAll(in) == 0);  // rien à l'appui
        in.HandleEvent(Key(SDLK_ESCAPE, false));
        CHECK(Drain(in, Action::Back) == 1);
        SDL_Delay(60);
        t = SDL_GetTicks();
        in.HandleEvent(Key(SDLK_ESCAPE, true));
        in.Update(t + 1300);
        CHECK(Drain(in, Action::Back, true) == 1);
        in.HandleEvent(Key(SDLK_ESCAPE, false));
        CHECK(Drain(in, Action::Back) == 0);
    }
    {
        Section("Anti-rebond : rebond de contact ignoré");
        Input in = Fresh();
        in.HandleEvent(JoyButton(kVJoy, 2, true));
        in.HandleEvent(JoyButton(kVJoy, 2, false));
        in.HandleEvent(JoyButton(kVJoy, 2, true));  // < 40 ms après le relâchement
        in.HandleEvent(JoyButton(kVJoy, 2, false));
        CHECK(Drain(in, Action::Favorite) == 1);
        SDL_Delay(60);
        in.HandleEvent(JoyButton(kVJoy, 2, true));
        CHECK(Drain(in, Action::Favorite) == 1);
    }
    {
        Section("Anti double validation");
        Input in = Fresh();
        in.HandleEvent(Key(SDLK_RETURN, true));
        in.HandleEvent(Key(SDLK_RETURN, false));
        SDL_Delay(60);
        in.HandleEvent(Key(SDLK_RETURN, true));  // < 700 ms
        in.HandleEvent(Key(SDLK_RETURN, false));
        CHECK(Drain(in, Action::Select) == 1);
    }
    {
        Section("Associations génériques : joystick oui, manette reconnue non (pas de double action)");
        Input in = Fresh();
        in.HandleEvent(JoyButton(kPad, 0, true));
        CHECK(Drain(in, Action::Select) == 0);
        in.HandleEvent(JoyButton(kVJoy, 0, true));
        CHECK(Drain(in, Action::Select) == 1);
        in.HandleEvent(JoyHat(kOther, 0, SDL_HAT_LEFT));
        CHECK(Drain(in, Action::Prev) == 1);
    }
    {
        Section("Association liée à un périphérique précis");
        Input in = Fresh();
        in.SetBindings({{Action::Select, Binding::Type::JoyButton, 5, 0, "vJoy Device"}});
        in.HandleEvent(JoyButton(kOther, 5, true));
        CHECK(Drain(in, Action::Select) == 0);
        in.HandleEvent(JoyButton(kVJoy, 5, true));
        CHECK(Drain(in, Action::Select) == 1);
    }
    {
        Section("Axe du volant avec hystérésis");
        Input in = Fresh();
        in.HandleEvent(JoyAxis(kVJoy, 0, 0.0f));
        in.HandleEvent(JoyAxis(kVJoy, 0, 0.65f));
        CHECK(Drain(in, Action::Next) == 1);
        in.HandleEvent(JoyAxis(kVJoy, 0, 0.5f));  // sous le seuil mais au-dessus de 70 % : toujours enfoncé
        CHECK(Drain(in, Action::Next) == 0);
        in.HandleEvent(JoyAxis(kVJoy, 0, 0.3f));
        SDL_Delay(60);
        in.HandleEvent(JoyAxis(kVJoy, 0, 0.65f));
        CHECK(Drain(in, Action::Next) == 1);
        in.HandleEvent(JoyAxis(kVJoy, 0, -0.9f));
        CHECK(Drain(in, Action::Prev) == 1);
    }
    {
        Section("Capture : bouton avec nom du périphérique, aucune action produite");
        Input in = Fresh();
        in.BeginCapture();
        in.HandleEvent(JoyButton(kVJoy, 7, true));
        CHECK(DrainAll(in) == 0);
        auto b = in.TakeCaptured();
        CHECK(b && b->type == Binding::Type::JoyButton && b->code == 7 && b->device == "vJoy Device");
        CHECK(in.AnyRawActive());
        in.HandleEvent(JoyButton(kVJoy, 7, false));
        CHECK(!in.AnyRawActive());
        in.EndCapture();
    }
    {
        Section("Capture : pédale au repos à -1, enfoncée vers +1");
        Input in = Fresh();
        in.BeginCapture();
        in.HandleEvent(JoyAxis(kOther, 2, -1.0f));  // repos
        CHECK(!in.TakeCaptured());
        in.HandleEvent(JoyAxis(kOther, 2, 0.9f));
        auto b = in.TakeCaptured();
        CHECK(b && b->type == Binding::Type::JoyAxis && b->code == 2 && b->dir == 1);
        in.EndCapture();
    }
    {
        Section("Capture : manette reconnue -> bouton manette, pas bouton joystick");
        Input in = Fresh();
        in.BeginCapture();
        in.HandleEvent(JoyButton(kPad, 0, true));
        CHECK(!in.TakeCaptured());
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.which = kPad;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
        e.gbutton.down = true;
        in.HandleEvent(e);
        auto b = in.TakeCaptured();
        CHECK(b && b->type == Binding::Type::PadButton && b->device == "Xbox Controller");
        in.EndCapture();
    }
    {
        Section("Remplacement : l'entrée capturée retire l'association générique en conflit");
        Input in = Fresh();
        in.ReplaceFamily({Action::Back, Binding::Type::JoyButton, 0, 0, "vJoy Device"});
        in.HandleEvent(JoyButton(kVJoy, 0, true));
        in.HandleEvent(JoyButton(kVJoy, 0, false));
        InputEvent ev;
        int select = 0, back = 0;
        while (in.Pop(ev)) {
            select += ev.action == Action::Select;
            back += ev.action == Action::Back;
        }
        CHECK(select == 0 && back == 1);
        // Le clavier et la manette reconnue ne sont jamais touchés par une capture joystick
        in.HandleEvent(Key(SDLK_ESCAPE, true));
        in.HandleEvent(Key(SDLK_ESCAPE, false));
        CHECK(Drain(in, Action::Back) == 1);
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        e.gbutton.which = kPad;
        e.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
        e.gbutton.down = true;
        in.HandleEvent(e);
        e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.down = false;
        in.HandleEvent(e);
        CHECK(Drain(in, Action::Back) == 1);
    }
    {
        Section("Garde après un jeu : appuis ignorés");
        Input in = Fresh();
        in.BlockUntil(SDL_GetTicks() + 10000);
        in.HandleEvent(Key(SDLK_RETURN, true));
        in.HandleEvent(Key(SDLK_RETURN, false));
        CHECK(DrainAll(in) == 0);
    }
    {
        Section("Enregistrement / relecture du mapping");
        Input a = Fresh();
        a.ReplaceFamily({Action::Menu, Binding::Type::JoyHat, 0, SDL_HAT_UP, "vJoy Device"});
        fs::path f = fs::temp_directory_path() / L"racecab-input-test.json";
        CHECK(a.Save(f));
        Input b = Fresh();
        CHECK(b.Load(f));
        CHECK(a.Bindings().size() == b.Bindings().size());
        bool found = false;
        for (const Binding& x : b.Bindings())
            found |= x.action == Action::Menu && x.type == Binding::Type::JoyHat && x.dir == SDL_HAT_UP && x.device == "vJoy Device";
        CHECK(found);
        fs::remove(f);
    }

    std::printf("\n%d vérifications réussies, %d échec(s)\n", g_pass, g_fail);
    SDL_Quit();
    return g_fail ? 1 : 0;
}
