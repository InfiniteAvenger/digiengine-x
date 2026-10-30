// PC and Android frontend (Android builds this file through SDL's Android
// port). On Android the game screen sits inside a drawn handheld shell whose
// three buttons work by touch; on PC it is the plain screen and the keyboard.
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include "shell.h"
#include "vpet/game.h"

namespace {

#ifdef __ANDROID__
constexpr bool kShell = true;
#else
constexpr bool kShell = false;
#endif
// What the window shows: the whole shell, or just the game screen.
constexpr int kViewW = kShell ? vpet::kShellW : vpet::kScreenW;
constexpr int kViewH = kShell ? vpet::kShellH : vpet::kScreenH;

class SdlPlatform : public vpet::Platform {
public:
    SdlPlatform(SDL_Renderer* renderer, SDL_Texture* texture, std::string save_dir)
        : renderer_(renderer), texture_(texture), save_dir_(std::move(save_dir)) {}

    uint64_t now_ms() override {
        using namespace std::chrono;
        return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    int32_t utc_offset_s() override {
        std::time_t now = std::time(nullptr);
        std::tm local = *std::localtime(&now);  // copy: both calls share a static buffer
        std::tm utc = *std::gmtime(&now);
        int days = local.tm_yday - utc.tm_yday;
        if (local.tm_year != utc.tm_year) days = local.tm_year > utc.tm_year ? 1 : -1;
        return days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 + (local.tm_min - utc.tm_min) * 60;
    }

    uint8_t buttons() override {
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        uint8_t b = pointer_buttons();
        if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_1] || k[SDL_SCANCODE_LEFT]) b |= vpet::BTN_A;
        if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_2] || k[SDL_SCANCODE_DOWN]) b |= vpet::BTN_B;
        if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_3] || k[SDL_SCANCODE_RIGHT]) b |= vpet::BTN_C;
        return b;
    }

    // Mouse and fingers on the shell's buttons. Each finger is tracked on its
    // own, so two thumbs can hold A+C for settings.
    void handle(const SDL_Event& e) {
        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEMOTION:
                if (e.type == SDL_MOUSEMOTION && !(e.motion.state & SDL_BUTTON_LMASK)) break;
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button != SDL_BUTTON_LEFT) break;
                mouse_ = vpet::Shell::button_at(e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                                                e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) mouse_ = 0;
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION: {
                int w = 0, h = 0;
                SDL_GetWindowSize(SDL_GetWindowFromID(e.tfinger.windowID), &w, &h);
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(renderer_, int(e.tfinger.x * float(w)), int(e.tfinger.y * float(h)), &lx, &ly);
                fingers_[e.tfinger.fingerId] = vpet::Shell::button_at(int(lx), int(ly));
                break;
            }
            case SDL_FINGERUP: fingers_.erase(e.tfinger.fingerId); break;
            default: break;
        }
    }

    void set_tone(uint16_t freq_hz) override { tone_hz.store(freq_hz); }

    // Square-wave synth on SDL's audio thread; follows tone_hz.
    static void audio_callback(void* user, Uint8* stream, int len) {
        auto* self = static_cast<SdlPlatform*>(user);
        auto* out = reinterpret_cast<int16_t*>(stream);
        int n = len / int(sizeof(int16_t));
        uint16_t hz = self->tone_hz.load();
        for (int i = 0; i < n; ++i) {
            if (hz == 0) {
                out[i] = 0;
                continue;
            }
            self->phase += float(hz) / float(kSampleRate);
            if (self->phase >= 1.0f) self->phase -= 1.0f;
            out[i] = self->phase < 0.5f ? kVolume : int16_t(-kVolume);
        }
    }
    static constexpr int kSampleRate = 22050;
    static constexpr int16_t kVolume = 2500;  // quiet: about 8% of full scale
    std::atomic<uint16_t> tone_hz{0};
    float phase = 0;

    void present(const vpet::Framebuffer& fb) override {
        if (kShell) {
            shell_.compose(fb, buttons());
            SDL_UpdateTexture(texture_, nullptr, shell_.pixels(), vpet::kShellW * int(sizeof(vpet::Color)));
        } else {
            SDL_UpdateTexture(texture_, nullptr, fb.pixels(), vpet::kScreenW * int(sizeof(vpet::Color)));
        }
        SDL_SetRenderDrawColor(renderer_, 0x10, 0x10, 0x14, 0xff);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    // SDL's file reader also reads from inside the APK on Android.
    bool read_file(const std::string& path, std::string& out) override {
        SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
        if (!f) return false;
        out.clear();
        char buf[4096];
        for (size_t n; (n = SDL_RWread(f, buf, 1, sizeof buf)) > 0;) out.append(buf, n);
        SDL_RWclose(f);
        return true;
    }

    // Folders on disk; inside an APK (which can't be listed) the build writes
    // the pack names, one per line, to index.txt.
    std::vector<std::string> list_dirs(const std::string& path) override {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
            if (entry.is_directory()) names.push_back(entry.path().filename().string());
        std::string index;
        if (names.empty() && read_file(path + "/index.txt", index)) {
            std::istringstream lines(index);
            for (std::string name; std::getline(lines, name);) {
                if (!name.empty() && name.back() == '\r') name.pop_back();
                if (!name.empty()) names.push_back(name);
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool load_save(const std::string& slot, std::string& out) override {
        return read_file(save_dir_ + slot + ".sav", out);
    }

    bool write_save(const std::string& slot, const std::string& data) override {
        SDL_RWops* f = SDL_RWFromFile((save_dir_ + slot + ".sav").c_str(), "wb");
        if (!f) return false;
        bool ok = SDL_RWwrite(f, data.data(), 1, data.size()) == data.size();
        return SDL_RWclose(f) == 0 && ok;
    }

private:
    uint8_t pointer_buttons() const {
        uint8_t b = mouse_;
        for (const auto& [id, bit] : fingers_) b |= bit;
        return b;
    }

    SDL_Renderer* renderer_;
    SDL_Texture* texture_;
    std::string save_dir_;
    vpet::Shell shell_;
    uint8_t mouse_ = 0;
    std::map<SDL_FingerID, uint8_t> fingers_;
};

}  // namespace

int main(int argc, char* argv[]) {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // fingers are handled on their own
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Start at the largest whole-number scale that fits ~80% of the screen
    // height, so pixels stay crisp. The window can still be resized freely.
    int scale = 4;
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) scale = std::max(2, mode.h * 8 / 10 / kViewH);
    SDL_Window* window = SDL_CreateWindow("DigiEngine X", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          kViewW * scale, kViewH * scale,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        SDL_Log("window/renderer creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, kViewW, kViewH);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                             kViewW, kViewH);

    std::vector<std::string> pack_dirs;
    if (argc > 1) {
        pack_dirs.push_back(argv[1]);
    } else {
        // Next to the executable on PC; on Android SDL has no base path and
        // relative paths read the APK's assets.
        char* base = SDL_GetBasePath();
        pack_dirs.push_back(std::string(base ? base : "") + "packs");  // every installed pack
        SDL_free(base);
#ifdef __ANDROID__
        // Packs the player copies onto the phone, in
        // Android/data/io.github.vpet/files/packs/ (made here so it shows up
        // when the phone is plugged into a PC).
        if (const char* ext = SDL_AndroidGetExternalStoragePath()) {
            std::string dir = std::string(ext) + "/packs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            pack_dirs.push_back(dir);
        }
#endif
    }

    // Per-user data folder, e.g. %APPDATA%\vpet\vpet\ on Windows, app storage on Android.
    char* pref = SDL_GetPrefPath("vpet", "vpet");
    std::string save_dir = pref ? pref : "";
    SDL_free(pref);

    SdlPlatform platform(renderer, texture, save_dir);

    // Sound is optional: if no audio device opens, the game just stays quiet.
    SDL_AudioSpec want{}, have{};
    want.freq = SdlPlatform::kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = SdlPlatform::audio_callback;
    want.userdata = &platform;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
    vpet::Game game(platform);
    if (!game.init(pack_dirs)) SDL_Log("failed to load packs from %s", pack_dirs.front().c_str());

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
            // Android: save when the app goes to the background (it may be killed there).
            if (e.type == SDL_APP_WILLENTERBACKGROUND) game.save();
            if (kShell) platform.handle(e);
        }
        game.frame();
        SDL_Delay(1);  // vsync paces the loop; this keeps CPU use low if vsync is off
    }
    game.save();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_Quit();
    return 0;
}
