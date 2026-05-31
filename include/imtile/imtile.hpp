// SPDX-License-Identifier: GPL-2.0-or-later
// imtile — Cockpit-Layout-Library für Dear ImGui
// Copyright (C) 2026 Kai Dieki
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>

namespace imtile {

// ---------------------------------------------------------------------------
// Node — deklarativer Layout-Baum
// ---------------------------------------------------------------------------

struct Node {
    enum class Kind { Window, Tabs, HSplit, VSplit, HSplitPx, VSplitPx };

    Kind                     kind;
    float                    ratio  = 0.f;   // für HSplit/VSplit: Anteil des ersten Kindes
    float                    pixels = 0.f;   // für HSplitPx/VSplitPx: feste Pixelgröße des ersten Kindes
    std::string              name;           // für Window
    std::vector<std::string> tab_names;      // für Tabs
    std::vector<Node>        children;       // 2 Einträge für Splits
};

inline Node window(std::string_view name)
{
    Node n;
    n.kind = Node::Kind::Window;
    n.name = name;
    return n;
}

inline Node tabs(std::initializer_list<std::string_view> names)
{
    Node n;
    n.kind = Node::Kind::Tabs;
    for (auto s : names)
        n.tab_names.emplace_back(s);
    return n;
}

inline Node hsplit(float left_ratio, Node left, Node right)
{
    Node n;
    n.kind  = Node::Kind::HSplit;
    n.ratio = left_ratio;
    n.children.push_back(std::move(left));
    n.children.push_back(std::move(right));
    return n;
}

// Pixel-feste Splits: erstes Kind bekommt genau `pixels` px,
// zweites bekommt den Rest. Auf kleinen Monitoren wird auf min. 5 % komprimiert.
inline Node hsplit_px(float left_px, Node left, Node right)
{
    Node n;
    n.kind   = Node::Kind::HSplitPx;
    n.pixels = left_px;
    n.children.push_back(std::move(left));
    n.children.push_back(std::move(right));
    return n;
}

inline Node vsplit_px(float top_px, Node top, Node bottom)
{
    Node n;
    n.kind   = Node::Kind::VSplitPx;
    n.pixels = top_px;
    n.children.push_back(std::move(top));
    n.children.push_back(std::move(bottom));
    return n;
}

inline Node vsplit(float top_ratio, Node top, Node bottom)
{
    Node n;
    n.kind  = Node::Kind::VSplit;
    n.ratio = top_ratio;
    n.children.push_back(std::move(top));
    n.children.push_back(std::move(bottom));
    return n;
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Options {
    std::string_view ini_file      = "";    // leer → kein automatisches ini
    std::string_view layout_file   = "";    // leer → kein explizites Laden beim Start
    bool             show_controls = true;  // draw_controls() aktiv
};

// ---------------------------------------------------------------------------
// detail — interner DockBuilder-Traversal
// ---------------------------------------------------------------------------

namespace detail {

inline void apply_node(ImGuiID id, const Node& node, ImVec2 avail)
{
    switch (node.kind) {
    case Node::Kind::Window:
        ImGui::DockBuilderDockWindow(node.name.c_str(), id);
        break;

    case Node::Kind::Tabs:
        for (const auto& name : node.tab_names)
            ImGui::DockBuilderDockWindow(name.c_str(), id);
        break;

    case Node::Kind::HSplit: {
        ImGuiID left_id = 0, right_id = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, node.ratio, &left_id, &right_id);
        float left_px = avail.x * node.ratio;
        apply_node(left_id,  node.children[0], {left_px,            avail.y});
        apply_node(right_id, node.children[1], {avail.x - left_px,  avail.y});
        break;
    }
    case Node::Kind::VSplit: {
        ImGuiID top_id = 0, bot_id = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Up, node.ratio, &top_id, &bot_id);
        float top_px = avail.y * node.ratio;
        apply_node(top_id, node.children[0], {avail.x, top_px});
        apply_node(bot_id, node.children[1], {avail.x, avail.y - top_px});
        break;
    }
    case Node::Kind::HSplitPx: {
        // Pixel-fest: Ratio aus fester Pixelgröße und tatsächlich verfügbarem Platz
        float ratio = avail.x > 1.f
            ? std::clamp(node.pixels / avail.x, 0.05f, 0.95f)
            : 0.5f;
        ImGuiID left_id = 0, right_id = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, ratio, &left_id, &right_id);
        float actual_left = std::min(node.pixels, avail.x * 0.95f);
        apply_node(left_id,  node.children[0], {actual_left,           avail.y});
        apply_node(right_id, node.children[1], {avail.x - actual_left, avail.y});
        break;
    }
    case Node::Kind::VSplitPx: {
        float ratio = avail.y > 1.f
            ? std::clamp(node.pixels / avail.y, 0.05f, 0.95f)
            : 0.5f;
        ImGuiID top_id = 0, bot_id = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Up, ratio, &top_id, &bot_id);
        float actual_top = std::min(node.pixels, avail.y * 0.95f);
        apply_node(top_id, node.children[0], {avail.x, actual_top});
        apply_node(bot_id, node.children[1], {avail.x, avail.y - actual_top});
        break;
    }
    }
}

// Minimales JSON-Schreiben ohne externe Deps: nur ein flaches Objekt mit
// string-Schlüsseln und double/bool-Werten.
struct JsonWriter {
    FILE* f = nullptr;
    bool  first = true;

    explicit JsonWriter(const char* path) : f(std::fopen(path, "w")) {}
    ~JsonWriter() { if (f) { std::fputs("\n}\n", f); std::fclose(f); } }

    [[nodiscard]] bool ok() const { return f != nullptr; }

    void begin() { if (f) std::fputs("{\n", f); }

    void kv(const char* key, double val) {
        if (!f) return;
        std::fprintf(f, "%s  \"%s\": %.17g", first ? "" : ",\n", key, val);
        first = false;
    }
    void kv(const char* key, bool val) {
        if (!f) return;
        std::fprintf(f, "%s  \"%s\": %s", first ? "" : ",\n", key, val ? "true" : "false");
        first = false;
    }
};

// Minimales JSON-Lesen: sucht einen Schlüssel und gibt dessen double-Wert zurück.
// Nur für das einfache Format das wir selbst schreiben.
inline bool json_read_double(const char* buf, const char* key, double& out)
{
    // suche "key": <zahl>
    std::string pattern = std::string("\"") + key + "\":";
    const char* pos = std::strstr(buf, pattern.c_str());
    if (!pos) {
        // auch mit Leerzeichen: "key" : <zahl>
        pattern = std::string("\"") + key + "\" :";
        pos = std::strstr(buf, pattern.c_str());
    }
    if (!pos) return false;
    pos += pattern.size();
    while (*pos == ' ') ++pos;
    char* end = nullptr;
    out = std::strtod(pos, &end);
    return end != pos;
}

// Sammelt alle Proportionen aus dem Node-Baum (Preorder) in einen Vektor.
inline void collect_ratios(const Node& node, std::vector<float>& out)
{
    if (node.kind == Node::Kind::HSplit || node.kind == Node::Kind::VSplit) {
        out.push_back(node.ratio);
        for (const auto& child : node.children)
            collect_ratios(child, out);
    } else if (node.kind == Node::Kind::HSplitPx || node.kind == Node::Kind::VSplitPx) {
        out.push_back(node.pixels);   // als Pixel-Wert gespeichert
        for (const auto& child : node.children)
            collect_ratios(child, out);
    }
}

// Schreibt Proportionen aus dem Node-Baum als JSON.
inline bool write_ratios_json(const char* path, const Node& root)
{
    std::vector<float> ratios;
    collect_ratios(root, ratios);

    JsonWriter w(path);
    if (!w.ok()) return false;
    w.begin();
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "r%zu", i);
        w.kv(key, static_cast<double>(ratios[i]));
    }
    return true;
}

// Liest Proportionen aus JSON und schreibt sie in den Node-Baum.
inline bool read_ratios_json(const char* path, Node& root)
{
    FILE* f = std::fopen(path, "r");
    if (!f) return false;

    // Datei einlesen
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    if (sz <= 0 || sz > 65536) { std::fclose(f); return false; }

    std::string buf(static_cast<std::size_t>(sz), '\0');
    std::fread(buf.data(), 1, static_cast<std::size_t>(sz), f);
    std::fclose(f);

    // Proportionen lesen und in Baum schreiben (Preorder, gleiche Reihenfolge)
    std::size_t idx = 0;
    std::function<void(Node&)> apply = [&](Node& node) {
        if (node.kind == Node::Kind::HSplit || node.kind == Node::Kind::VSplit) {
            char key[32];
            std::snprintf(key, sizeof(key), "r%zu", idx++);
            double val = 0.0;
            if (json_read_double(buf.c_str(), key, val) && val > 0.01 && val < 0.99)
                node.ratio = static_cast<float>(val);
            for (auto& child : node.children)
                apply(child);
        }
    };
    apply(root);
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Cockpit
// ---------------------------------------------------------------------------

class Cockpit {
public:
    explicit Cockpit(Node layout, Options opts = {})
        : layout_(std::move(layout))
        , ini_file_(opts.ini_file)
        , show_controls_(opts.show_controls)
    {
        // ini_file als null-terminierter String für io.IniFilename
        if (!ini_file_.empty())
            ini_file_buf_ = ini_file_;

        if (!opts.layout_file.empty())
            load_layout(opts.layout_file);
    }

    // Einmal pro Frame aufrufen, BEVOR die eigenen Panels gezeichnet werden.
    void begin_frame()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = ini_file_buf_.empty() ? nullptr : ini_file_buf_.c_str();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        constexpr ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_NoTitleBar        |
            ImGuiWindowFlags_NoCollapse        |
            ImGuiWindowFlags_NoResize          |
            ImGuiWindowFlags_NoMove            |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus        |
            ImGuiWindowFlags_NoDocking         |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {0.f, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin("##imtile_host", nullptr, host_flags);
        ImGui::PopStyleVar(3);

        ImGuiID ds_id = ImGui::GetID("##imtile_dock");

        // Layout aufbauen wenn: erster Start, Reset angefordert, oder DockSpace leer
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(ds_id);
        if (reset_ || node == nullptr || node->IsLeafNode()) {
            ImGui::DockBuilderRemoveNode(ds_id);
            ImGui::DockBuilderAddNode(ds_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(ds_id, vp->WorkSize);
            detail::apply_node(ds_id, layout_, vp->WorkSize);
            ImGui::DockBuilderFinish(ds_id);
            reset_ = false;
        }

        ImGui::DockSpace(ds_id, {0.f, 0.f}, ImGuiDockNodeFlags_None);
        ImGui::End();
    }

    // Beim nächsten Frame Default-Layout neu aufbauen.
    void request_reset() { reset_ = true; }

    // Layout aus Datei laden (JSON-Proportionen). Setzt reset_ = true.
    bool load_layout(std::string_view path)
    {
        std::string p(path);
        if (!detail::read_ratios_json(p.c_str(), layout_))
            return false;
        reset_ = true;
        return true;
    }

    // Aktuellen Layout-Zustand in Datei speichern (JSON-Proportionen).
    bool save_layout(std::string_view path) const
    {
        std::string p(path);
        return detail::write_ratios_json(p.c_str(), layout_);
    }

    // Unaufdringliches Layout-Widget — in Menüleiste, Statuszeile o.ä. platzieren.
    // Gibt true zurück wenn eine Aktion den State verändert hat.
    bool draw_controls()
    {
        if (!show_controls_) return false;

        bool changed = false;

        // Toggle-Buttons für ausgeblendete Panels
        collect_window_names(layout_, window_names_cache_);
        for (const auto& name : window_names_cache_) {
            ImGuiWindow* win = ImGui::FindWindowByName(name.c_str());
            if (win && !win->Active) {
                ImGui::PushID(name.c_str());
                if (ImGui::SmallButton(name.c_str())) {
                    // Fenster durch Re-Dock sichtbar machen
                    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
                    changed = true;
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
        }

        // Reset-Button
        if (ImGui::SmallButton("⟳")) {
            request_reset();
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Layout zurücksetzen");

        ImGui::SameLine();

        // Laden-Button
        if (ImGui::SmallButton("📂")) {
            open_load_dialog_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Layout laden…");

        ImGui::SameLine();

        // Speichern-Button
        if (ImGui::SmallButton("💾")) {
            open_save_dialog_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Layout speichern…");

        // Lade-Dialog
        if (open_load_dialog_) {
            ImGui::OpenPopup("##imtile_load");
            open_load_dialog_ = false;
        }
        if (ImGui::BeginPopup("##imtile_load")) {
            ImGui::Text("Layout laden:");
            ImGui::SetNextItemWidth(300.f);
            if (ImGui::InputText("##path_load", path_buf_, sizeof(path_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (load_layout(path_buf_))
                    changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Laden")) {
                if (load_layout(path_buf_))
                    changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Abbrechen"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Speicher-Dialog
        if (open_save_dialog_) {
            ImGui::OpenPopup("##imtile_save");
            open_save_dialog_ = false;
        }
        if (ImGui::BeginPopup("##imtile_save")) {
            ImGui::Text("Layout speichern:");
            ImGui::SetNextItemWidth(300.f);
            if (ImGui::InputText("##path_save", path_buf_, sizeof(path_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                save_layout(path_buf_);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Speichern")) {
                save_layout(path_buf_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Abbrechen"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        return changed;
    }

private:
    Node        layout_;
    std::string ini_file_;
    std::string ini_file_buf_;
    bool        show_controls_;
    bool        reset_            = false;
    bool        open_load_dialog_ = false;
    bool        open_save_dialog_ = false;
    char        path_buf_[512]    = "layout.json";

    // Cache der Fensternamen für draw_controls() (wird jedes Mal neu befüllt,
    // aber der Vektor wird nicht reallociert wenn die Größe gleich bleibt)
    mutable std::vector<std::string> window_names_cache_;

    static void collect_window_names(const Node& node, std::vector<std::string>& out)
    {
        out.clear();
        collect_window_names_impl(node, out);
    }

    static void collect_window_names_impl(const Node& node, std::vector<std::string>& out)
    {
        switch (node.kind) {
        case Node::Kind::Window:
            out.push_back(node.name);
            break;
        case Node::Kind::Tabs:
            for (const auto& name : node.tab_names)
                out.push_back(name);
            break;
        case Node::Kind::HSplit:
        case Node::Kind::VSplit:
        case Node::Kind::HSplitPx:
        case Node::Kind::VSplitPx:
            for (const auto& child : node.children)
                collect_window_names_impl(child, out);
            break;
        }
    }
};

} // namespace imtile
