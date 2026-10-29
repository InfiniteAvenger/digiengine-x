// vpet_baker: build packs from PNG sheets + CSV tables, and convert sprites.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "baker.h"
#include "vpet/png.h"

namespace {

int usage() {
    std::puts(
        "usage:\n"
        "  vpet_baker build <source_dir> <pack_dir>     bake species.csv/evolutions.csv/base.json + sprites\n"
        "  vpet_baker png2spr <in.png> <out.spr> [frame_w frame_h]\n"
        "  vpet_baker spr2png <in.spr> <out.png>        export .spr art as a PNG strip");
    return 2;
}

bool slurp(const char* path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool spit(const char* path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    return bool(f << data);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) return usage();
    std::string cmd = argv[1], data, err;

    if (cmd == "build") {
        vpet::BakeReport r;
        bool ok = vpet::bake_pack(argv[2], argv[3], r);
        for (const std::string& w : r.warnings) std::printf("warning: %s\n", w.c_str());
        for (const std::string& e : r.errors) std::printf("error: %s\n", e.c_str());
        std::printf("%s: %d species, %d sprites, %d evolution rules, %d extra files -> %s\n", ok ? "baked" : "FAILED",
                    r.species, r.sprites, r.evolutions, r.files_copied, argv[3]);
        return ok ? 0 : 1;
    }

    if (cmd == "png2spr") {
        vpet::Image img;
        vpet::Sprite s;
        std::string spr;
        int fw = argc > 5 ? std::atoi(argv[4]) : 0, fh = argc > 5 ? std::atoi(argv[5]) : 0;
        if (!slurp(argv[2], data)) return std::printf("error: cannot read %s\n", argv[2]), 1;
        if (!vpet::decode_png(data, img, err) || !vpet::sprite_from_image(img, fw, fh, s, err) ||
            !vpet::write_spr(s, spr, err))
            return std::printf("error: %s\n", err.c_str()), 1;
        if (!spit(argv[3], spr)) return std::printf("error: cannot write %s\n", argv[3]), 1;
        std::printf("%dx%d, %d frame(s), %d colors -> %s\n", s.w, s.h, s.frames, int(s.palette.size()) - 1, argv[3]);
        return 0;
    }

    if (cmd == "spr2png") {
        vpet::Sprite s;
        if (!slurp(argv[2], data)) return std::printf("error: cannot read %s\n", argv[2]), 1;
        if (!vpet::parse_sprite(data, s, err)) return std::printf("error: %s\n", err.c_str()), 1;
        if (!spit(argv[3], vpet::encode_png(vpet::image_from_sprite(s)))) return std::printf("error: cannot write %s\n", argv[3]), 1;
        std::printf("%d frame(s) of %dx%d -> %s\n", s.frames, s.w, s.h, argv[3]);
        return 0;
    }
    return usage();
}
