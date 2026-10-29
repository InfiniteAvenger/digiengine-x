// Desktop frontend. Android will reuse this file through SDL's Android port.
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "vpet/game.h"

namespace {

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
        uint8_t b = 0;
        if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_1] || k[SDL_SCANCODE_LEFT]) b |= vpet::BTN_A;
        if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_2] || k[SDL_SCANCODE_DOWN]) b |= vpet::BTN_B;
        if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_3] || k[SDL_SCANCODE_RIGHT]) b |= vpet::BTN_C;
        return b;
    }

    void present(const vpet::Framebuffer& fb) override {
        SDL_UpdateTexture(texture_, nullptr, fb.pixels(), vpet::kScreenW * int(sizeof(vpet::Color)));
        SDL_SetRenderDrawColor(renderer_, 0x10, 0x10, 0x14, 0xff);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    bool read_file(const std::string& path, std::string& out) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    std::vector<std::string> list_dirs(const std::string& path) override {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
            if (entry.is_directory()) names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }

    bool load_save(const std::string& slot, std::string& out) override {
        return read_file(save_dir_ + slot + ".sav", out);
    }

    bool write_save(const std::string& slot, const std::string& data) override {
        std::ofstream f(save_dir_ + slot + ".sav", std::ios::binary | std::ios::trunc);
        return bool(f << data);
    }

private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_;
    std::string save_dir_;
};

}  // namespace

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Start at the largest whole-number scale that fits ~80% of the screen
    // height, so pixels stay crisp. The window can still be resized freely.
    int scale = 4;
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) scale = std::max(2, mode.h * 8 / 10 / vpet::kScreenH);
    SDL_Window* window = SDL_CreateWindow("vpet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          vpet::kScreenW * scale, vpet::kScreenH * scale,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        SDL_Log("window/renderer creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, vpet::kScreenW, vpet::kScreenH);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                             vpet::kScreenW, vpet::kScreenH);

    std::string pack_dir;
    if (argc > 1) {
        pack_dir = argv[1];
    } else {
        char* base = SDL_GetBasePath();
        pack_dir = std::string(base ? base : "") + "packs";  // every installed pack
        SDL_free(base);
    }

    // Per-user data folder, e.g. %APPDATA%\vpet\vpet\ on Windows.
    char* pref = SDL_GetPrefPath("vpet", "vpet");
    std::string save_dir = pref ? pref : "";
    SDL_free(pref);

    SdlPlatform platform(renderer, texture, save_dir);
    vpet::Game game(platform);
    if (!game.init(pack_dir)) SDL_Log("failed to load pack from %s", pack_dir.c_str());

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
        }
        game.frame();
        SDL_Delay(1);  // vsync paces the loop; this keeps CPU use low if vsync is off
    }
    game.save();

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
