#include "baker.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "vpet/json.h"
#include "vpet/pack.h"
#include "vpet/png.h"

namespace fs = std::filesystem;

namespace vpet {
namespace {

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    return bool(f << data);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// A simple CSV table: header row, comma-separated, no quoting. Blank lines and
// lines starting with '#' are skipped.
struct Table {
    std::string file;
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> line;  // source line of each row

    std::string get(size_t row, const std::string& col) const {
        for (size_t c = 0; c < header.size(); ++c)
            if (header[c] == col) return c < rows[row].size() ? rows[row][c] : "";
        return "";
    }
    std::string where(size_t row) const { return file + " line " + std::to_string(line[row]); }
};

bool read_table(const fs::path& p, Table& t) {
    std::string text;
    if (!read_text(p, text)) return false;
    t.file = p.filename().string();
    std::istringstream in(text);
    std::string ln;
    for (int n = 1; std::getline(in, ln); ++n) {
        ln = trim(ln);
        if (ln.empty() || ln[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream ss(ln);
        for (std::string cell; std::getline(ss, cell, ',');) cells.push_back(trim(cell));
        if (ln.back() == ',') cells.push_back("");
        if (t.header.empty()) {
            t.header = cells;
        } else {
            t.rows.push_back(cells);
            t.line.push_back(n);
        }
    }
    return true;
}

// Optional integer cell: empty means "not set".
bool cell_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (*end) return false;
    out = int(v);
    return true;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (uint8_t(c) < 0x20) continue;
        else o += c;
    }
    return o + "\"";
}

std::string number(double v) {
    long long whole = static_cast<long long>(v);
    if (v == static_cast<double>(whole)) return std::to_string(whole);
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

// Serializes a parsed Json value back to text.
std::string dump(const Json& j, int indent) {
    std::string pad(size_t(indent) * 2, ' '), inner(size_t(indent + 1) * 2, ' ');
    switch (j.type()) {
        case Json::Type::Null: return "null";
        case Json::Type::Bool: return j.bool_or(false) ? "true" : "false";
        case Json::Type::Number: return number(j.num_or(0));
        case Json::Type::String: return quote(j.str_or(""));
        case Json::Type::Array: {
            if (!j.size()) return "[]";
            std::string o = "[\n";
            for (size_t i = 0; i < j.size(); ++i) o += inner + dump(j[i], indent + 1) + (i + 1 < j.size() ? ",\n" : "\n");
            return o + pad + "]";
        }
        case Json::Type::Object: {
            if (!j.size()) return "{}";
            std::string o = "{\n";
            for (size_t i = 0; i < j.size(); ++i)
                o += inner + quote(j.key(i)) + ": " + dump(j[i], indent + 1) + (i + 1 < j.size() ? ",\n" : "\n");
            return o + pad + "}";
        }
    }
    return "null";
}

// Every string in base.json that names a sprite file.
void collect_files(const Json& j, std::set<std::string>& out) {
    if (j.type() == Json::Type::String) {
        std::string s = j.str_or("");
        auto ends = [&](const char* ext) { return s.size() > 4 && s.compare(s.size() - 4, 4, ext) == 0; };
        if (ends(".png") || ends(".spr") || ends(".PNG")) out.insert(s);
    }
    for (size_t i = 0; i < j.size(); ++i) collect_files(j[i], out);
}

bool load_any_sprite(const fs::path& p, int fw, int fh, Sprite& s, std::string& err) {
    std::string data;
    if (!read_text(p, data)) return err = "cannot read " + p.string(), false;
    std::string ext = p.extension().string();
    if (ext == ".png" || ext == ".PNG") {
        Image img;
        return decode_png(data, img, err) && sprite_from_image(img, fw, fh, s, err);
    }
    return parse_sprite(data, s, err);
}

}  // namespace

bool bake_pack(const std::string& in_dir, const std::string& out_dir, BakeReport& r) {
    fs::path in(in_dir), out(out_dir);
    auto error = [&](const std::string& msg) { r.errors.push_back(msg); };

    Json base;
    std::string text, err;
    if (read_text(in / "base.json", text) && !Json::parse(text, base, err)) error("base.json: " + err);

    Table species, evolutions;
    if (!read_table(in / "species.csv", species)) {
        error("missing species.csv in " + in.string());
        return false;
    }
    for (const char* col : {"id", "stage", "sprite"}) {
        bool found = false;
        for (const std::string& h : species.header) found |= h == col;
        if (!found) error(std::string("species.csv: missing required column '") + col + "'");
    }
    bool have_evolutions = read_table(in / "evolutions.csv", evolutions);
    if (!r.errors.empty()) return false;

    // --- species -------------------------------------------------------------
    std::set<std::string> ids;
    std::vector<std::string> eggs;
    std::map<std::string, Stage> stage_of;
    std::string species_json;
    for (size_t row = 0; row < species.rows.size(); ++row) {
        std::string id = species.get(row, "id"), name = species.get(row, "name"), stage = species.get(row, "stage");
        std::string where = species.where(row);
        if (id.empty()) {
            error(where + ": empty id");
            continue;
        }
        if (!ids.insert(id).second) error(where + ": duplicate id '" + id + "'");
        Stage st;
        if (!parse_stage(stage, st)) {
            error(where + ": unknown stage '" + stage + "'");
            continue;
        }
        stage_of[id] = st;
        if (st == Stage::Egg) eggs.push_back(id);

        int fw = 0, fh = 0;
        cell_int(species.get(row, "frame_w"), fw);
        cell_int(species.get(row, "frame_h"), fh);
        fs::path src = in / species.get(row, "sprite");
        Sprite spr;
        if (!load_any_sprite(src, fw, fh, spr, err)) {
            error(where + ": sprite " + species.get(row, "sprite") + ": " + err);
            continue;
        }
        std::string spr_text, rel;
        if (write_spr(spr, spr_text, err)) {
            rel = "sprites/" + id + ".spr";
            write_text(out / rel, "# Generated by vpet_baker from " + species.get(row, "sprite") + "\n" + spr_text);
        } else {
            rel = "sprites/" + id + src.extension().string();
            fs::create_directories((out / rel).parent_path());
            fs::copy_file(src, out / rel, fs::copy_options::overwrite_existing);
            r.warnings.push_back(where + ": " + err);
        }
        ++r.sprites;

        std::string o = "    {\n      \"id\": " + quote(id) + ",\n      \"name\": " + quote(name.empty() ? id : name) +
                        ",\n      \"stage\": " + quote(stage) + ",\n      \"sprite\": " + quote(rel);
        std::string family = species.get(row, "family");
        if (!family.empty()) o += ",\n      \"family\": " + quote(family);

        std::string stats;
        for (const char* stat : {"hp", "atk", "def", "spd"}) {
            int v;
            std::string cell = species.get(row, stat);
            if (cell_int(cell, v)) stats += std::string(stats.empty() ? "" : ", ") + quote(stat) + ": " + std::to_string(v);
            else if (!cell.empty()) error(where + ": " + stat + " must be a whole number");
        }
        if (!stats.empty()) o += ",\n      \"stats\": { " + stats + " }";

        // learns: "move@level;move@level"
        std::string learns = species.get(row, "learns"), list;
        std::stringstream ls(learns);
        for (std::string entry; std::getline(ls, entry, ';');) {
            entry = trim(entry);
            if (entry.empty()) continue;
            size_t at = entry.find('@');
            int lv = 1;
            if (at != std::string::npos && !cell_int(entry.substr(at + 1), lv)) error(where + ": bad learns entry '" + entry + "'");
            list += std::string(list.empty() ? "" : ", ") + "{ \"move\": " + quote(trim(entry.substr(0, at))) +
                    ", \"level\": " + std::to_string(lv) + " }";
        }
        if (!list.empty()) o += ",\n      \"learns\": [" + list + "]";

        // Evolution rules for this species, in file order (= priority order).
        std::string rules;
        for (size_t e = 0; have_evolutions && e < evolutions.rows.size(); ++e) {
            if (evolutions.get(e, "from") != id) continue;
            std::string rule = "{ \"to\": " + quote(evolutions.get(e, "to"));
            for (const char* key :
                 {"hours_in_stage", "max_care_mistakes", "min_level", "min_effort", "min_quest_floor"}) {
                std::string cell = evolutions.get(e, key);
                if (cell.empty()) continue;
                char* end = nullptr;
                double v = std::strtod(cell.c_str(), &end);
                if (*end) error(evolutions.where(e) + ": " + key + " must be a number");
                rule += std::string(", \"") + key + "\": " + number(v);
            }
            rules += std::string(rules.empty() ? "\n        " : ",\n        ") + rule + " }";
            ++r.evolutions;
        }
        if (!rules.empty()) o += ",\n      \"evolve\": [" + rules + "\n      ]";
        o += "\n    }";
        species_json += (species_json.empty() ? "" : ",\n") + o;
        ++r.species;
    }

    for (size_t e = 0; have_evolutions && e < evolutions.rows.size(); ++e) {
        std::string from = evolutions.get(e, "from"), to = evolutions.get(e, "to");
        if (!stage_of.count(from)) error(evolutions.where(e) + ": unknown 'from' species '" + from + "'");
        if (!stage_of.count(to)) error(evolutions.where(e) + ": unknown 'to' species '" + to + "'");
        else if (stage_of.count(from) && stage_of[to] <= stage_of[from])
            r.warnings.push_back(evolutions.where(e) + ": " + to + " is not a later stage than " + from);
    }
    if (eggs.empty() && base["rosters"].is_null()) error("species.csv: no species with stage 'egg', so there is nothing to hatch");

    // --- files base.json refers to ---------------------------------------------
    std::set<std::string> files;
    collect_files(base, files);
    for (const std::string& f : files) {
        std::error_code ec;
        fs::create_directories((out / f).parent_path());
        fs::copy_file(in / f, out / f, fs::copy_options::overwrite_existing, ec);
        if (ec) error("base.json refers to " + f + ", which could not be copied: " + ec.message());
        else ++r.files_copied;
    }
    if (!r.errors.empty()) return false;

    // --- pack.json -------------------------------------------------------------
    std::string o = "{\n";
    auto field = [&](const std::string& k, const std::string& v) { o += "  " + quote(k) + ": " + v + ",\n"; };
    if (base["format"].is_null()) field("format", "1");
    if (base["id"].is_null()) field("id", quote(out.filename().string()));
    for (size_t i = 0; i < base.size(); ++i)
        if (base.key(i) != "species" && base.key(i) != "rosters") field(base.key(i), dump(base[i], 1));
    if (!base["rosters"].is_null()) {
        field("rosters", dump(base["rosters"], 1));
    } else {
        std::string list;
        for (const std::string& e : eggs) list += (list.empty() ? "" : ", ") + quote(e);
        field("rosters", "[ { \"id\": \"main\", \"name\": \"MAIN\", \"eggs\": [" + list + "] } ]");
    }
    o += "  \"species\": [\n" + species_json + "\n  ]\n}\n";
    write_text(out / "pack.json", o);
    return true;
}

}  // namespace vpet
