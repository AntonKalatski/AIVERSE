// «Дороги»: spline roads (docs/roads.md § Editor). Draw roads in the Scene like a city builder: straight, curved
// (a control point) or free (continues the last road's direction); ends snap to nodes and edges (a junction is made
// there), to the grid and to 15° steps; a road may start / end on chosen lanes of another road (ramps, merges);
// every point has a height (PageUp / PageDown): high stretches become bridges, low ones tunnels. Edit mode moves
// nodes and control points, changes the type of an edge; the type builder makes the game's own road types (lanes,
// curbs, barriers, sidewalks, props). Agents do the same with road_* operations (core/RoadOps.cpp).
#include <algorithm>
#include <cmath>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/RoadOps.h"
#include "../../../modules/rts/view/RtsRoadView.h"

namespace aiverse::editor {

using namespace game;

namespace {
Fixed fx(float v) { return Fixed::fromRaw(int64_t(std::llround(double(v) * 65536.0))); }
float fl(Fixed v) { return float(v.toDouble()); }
Vec2 v2(float x, float z) { return {fx(x), fx(z)}; }
const char* const kCats[] = {"city", "rural", "highway", "path"};
const char* const kCatNames[] = {"Город", "Загород", "Шоссе", "Дорожки"};
const char* const kModes[] = {"Прямая", "Кривая", "Свободная", "Правка", "Остановка", "Удалить"};
const char* const kCurbs[] = {"none", "low", "high", "sloped", "shoulder"};
const char* const kCurbNames[] = {"нет", "низкий", "высокий", "скошенный", "обочина"};
const char* const kBarriers[] = {"none", "guardrail", "concrete", "fence", "wall", "hedge"};
const char* const kBarrierNames[] = {"нет", "отбойник", "бетонный парапет", "забор", "стена", "живая изгородь"};
const char* const kSurfaces[] = {"asphalt", "concrete", "cobble", "paving", "gravel", "dirt", "grass"};
const char* const kSurfaceNames[] = {"асфальт", "бетон", "брусчатка", "плитка", "гравий", "грунт", "трава"};
const char* const kShapes[] = {"lamp", "tree", "bush", "flowerbed", "bollard", "pole", "bench", "sign"};
const char* const kShapeNames[] = {"фонарь", "дерево", "куст", "клумба", "столбик", "столб ЛЭП", "скамейка", "знак"};
const char* const kSides[] = {"обе стороны", "слева", "справа", "разделитель"};
} // namespace

class RoadsWindow : public EditorWindow {
public:
    const char* id() const override { return "roads"; }
    const char* title() const override { return "Дороги"; }
    const char* group() const override { return "world"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 3; }
    const char* icon() const override { return ICON_ROADS; }

    void draw(Editor& ed) override {
        refresh(ed);
        bool active = ed.tool == Tool::Count && owner_;
        if (ui::checkbox(ICON_ROADS "  Рисовать в «Сцене»", &active)) setActive(ed, active);
        ImGui::SetItemTooltip("ЛКМ — точки дороги, ПКМ / Esc — закончить, PageUp / PageDown — высота точки, Shift — шаг 15°");
        ui::segmented("##mode", mode_, kModes, 6);
        if (ImGui::IsItemEdited()) { reset(); setActive(ed, true); }
        if (ui::beginProps("##draw")) {
            ui::prop("Высота", "Метры над землёй для новых точек: выше 1,5 м — мост, ниже −2,5 м — тоннель (PageUp / PageDown)");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##elev", &elev_, -20, 30, "%.1f м");
            ui::propWide();
            ui::checkbox("От полос / к полосам", &lanes_);
            ImGui::SetItemTooltip("Начать или закончить дорогу на выбранных полосах другой дороги (съезд, слияние). Shift + щелчок — добавить соседнюю полосу.");
            ui::propWide();
            ui::checkbox("Шаг 15°", &angleSnap_);
            ui::endProps();
        }
        if (!err_.empty()) ui::callout(ui::Tone::Error, err_);
        if (mode_ == 3) inspector(ed);
        if (mode_ == 4) ui::hint("Щелчок по дороге у бордюра — карман и павильон на этой стороне; Shift + щелчок — убрать ближайший.");
        palette(ed);
        builder(ed);
        issues(ed);
    }

    void update(Editor& ed, double) override {
        if (ed.tool != Tool::Count) owner_ = false;
        if (!owner_ || ed.playing() || !ed.sceneVisible()) { clearGhost(ed); return; }
        refresh(ed);
        Viewport& vp = *ed.sceneViewport;
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) elev_ = std::min(30.0f, elev_ + 1);
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) elev_ = std::max(-20.0f, elev_ - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) reset();
            if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && mode_ == 3 && vp.hovered) deleteSelected(ed);
        }
        if (vp.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) reset();
        float x, z;
        hoverOk_ = vp.hovered && ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z);
        if (hoverOk_) hover_ = snapAt(ed, x, z, io.KeyShift);
        if (mode_ <= 2) drawInput(ed, io);
        else if (mode_ == 3) editInput(ed, io, vp);
        else if (mode_ == 4) { if (vp.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) stopAt(ed, io.KeyShift); }
        else if (vp.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) eraseAt(ed);
        updateGhost(ed);
        overlay(ed, vp);
    }

private:
    // ---------------------------------------------------------------- state
    int mode_ = 0;
    float elev_ = 0;
    bool lanes_ = false, angleSnap_ = false, owner_ = false, hoverOk_ = false;
    std::string type_ = "street_2", err_;
    std::vector<RoadTypeDef> types_;
    RoadNetwork net_;
    RoadGeometry geom_;
    uint64_t rev_ = ~0ull;
    std::string rulesKey_;
    roads::Snap hover_;
    // drawing
    int step_ = 0;                     // 0 nothing, 1 start set, 2 (curve) control set
    roads::Snap start_;
    Fixed startY_{};
    Vec2 ctrl_{};
    bool haveTangent_ = false;
    Vec2 tangent_{};                    // free mode: the direction the last road ended with
    RoadAttach fromLanes_, toLanes_;
    // editing
    int32_t selNode_ = -1, selEdge_ = -1;
    int dragging_ = 0;                  // 1 node, 2 c1, 3 c2
    float dragY0_ = 0, dragElev0_ = 0;
    bool editBegun_ = false;
    // type builder
    Json draft_ = Json::object();
    std::string draftId_ = "my_road", draftBase_ = "street_2";

    void setActive(Editor& ed, bool on) {
        owner_ = on;
        ed.tool = on ? Tool::Count : Tool::Select;
        if (on) ed.focusWindow("scene");
        reset();
    }
    void reset() { step_ = 0; fromLanes_ = toLanes_ = {}; haveTangent_ = false; dragging_ = 0; }
    void clearGhost(Editor& ed) {
        if (ed.view().roads().types().empty()) return;
        static RtsRoadView::Ghost none;
        ed.view().roads().setGhost(none);
    }

    void refresh(Editor& ed) {
        const std::string key = ed.ws().project.rulesJson["roadTypes"].dump(0) + ed.ws().project.dir;
        if (key != rulesKey_) { types_ = roads::types(ed.ws()); rulesKey_ = key; rev_ = ~0ull; }
        if (ed.doc().revision == rev_) return;
        rev_ = ed.doc().revision;
        try { net_ = roads::read(ed.doc(), types_); err_.clear(); }
        catch (const std::exception& e) { net_ = RoadNetwork{}; err_ = e.what(); }
        geom_ = buildRoadGeometry(net_, types_, {}, Fixed::fromRatio(1, 2), roadCellMeters(ed.ws().project.rulesJson["roadTypes"]));
        if (net_.nodeIndex(selNode_) < 0) selNode_ = -1;
        if (net_.edgeIndex(selEdge_) < 0) selEdge_ = -1;
    }
    float cm(Editor& ed) const { return float(roadCellMeters(ed.ws().project.rulesJson["roadTypes"]).toDouble()); }

    roads::Snap snapAt(Editor& ed, float x, float z, bool shift) {
        const RoadTypeDef* t = typeOf(type_);
        const Fixed radius = fx(std::max(1.0f, t ? fl(t->halfWidth()) * 0.8f : 1.5f));
        roads::Snap s = roads::snap(net_, geom_, v2(x, z), radius);
        if (s.kind == roads::Snap::None) {
            float px = x, pz = z;
            if (ed.snap) { px = std::round(px * 2) / 2; pz = std::round(pz * 2) / 2; }
            if ((angleSnap_ || shift) && step_ >= 1) {   // 15° steps from the start point
                const float sx = fl(start_.p.x), sz = fl(start_.p.y);
                const float a = std::atan2(pz - sz, px - sx), l = std::hypot(px - sx, pz - sz);
                const float q = std::round(a / 0.2617994f) * 0.2617994f;
                px = sx + std::cos(q) * l; pz = sz + std::sin(q) * l;
            }
            s.p = v2(px, pz);
        }
        return s;
    }
    const RoadTypeDef* typeOf(const std::string& id) const { const int i = findRoadType(types_, id); return i >= 0 ? &types_[size_t(i)] : nullptr; }

    // lanes of the snapped edge under the cursor: the attach (lane range) for a start / end on lanes
    bool pickLanes(const roads::Snap& s, RoadAttach& at, bool extend) {
        if (s.kind != roads::Snap::Edge) return false;
        const RoadEdge& e = net_.edges[size_t(s.edge)];
        int dir = 0, lane = 0;
        if (!roads::laneAt(types_[size_t(e.typeIndex)], e, s.lateral, dir, lane)) return false;
        if (extend && at.edge == s.edge && at.dir == dir) { at.lane0 = int8_t(std::min<int>(at.lane0, lane)); at.lane1 = int8_t(std::max<int>(at.lane1, lane)); }
        else { at.edge = s.edge; at.dir = int8_t(dir); at.lane0 = at.lane1 = int8_t(lane); }
        return true;
    }

    // ---------------------------------------------------------------- drawing
    void drawInput(Editor& ed, ImGuiIO& io) {
        if (!hoverOk_ || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
        err_.clear();
        if (step_ == 0) {
            start_ = hover_;
            startY_ = start_.kind == roads::Snap::Node ? net_.nodes[size_t(net_.nodeIndex(start_.node))].y : fx(elev_);
            fromLanes_ = {};
            if (lanes_) pickLanes(start_, fromLanes_, false);
            haveTangent_ = false;
            if (start_.kind == roads::Snap::Node) {   // free mode: continue the road that ends here
                for (auto& e : net_.edges) {
                    const int ei = net_.edgeIndex(e.id);
                    const auto& g = geom_.edges[size_t(ei)];
                    if (g.pts.empty()) continue;
                    if (e.b == start_.node) { tangent_ = g.pts.back().t; haveTangent_ = true; }
                    else if (e.a == start_.node) { tangent_ = {-g.pts.front().t.x, -g.pts.front().t.y}; haveTangent_ = true; }
                }
            }
            if (fromLanes_.edge >= 0) { tangent_ = geom_.edges[size_t(fromLanes_.edge)].pts.front().t; if (fromLanes_.dir * (net_.edges[size_t(fromLanes_.edge)].reversed ? -1 : 1) < 0) tangent_ = {-tangent_.x, -tangent_.y}; haveTangent_ = true; }
            step_ = 1;
            return;
        }
        if (lanes_ && io.KeyShift && step_ == 1 && hover_.kind == roads::Snap::Edge && fromLanes_.edge == hover_.edge) { pickLanes(hover_, fromLanes_, true); return; }
        if (mode_ == 1 && step_ == 1) { ctrl_ = hover_.p; step_ = 2; return; }
        // commit
        roads::AddSpec spec;
        if (!build(ed, hover_, spec)) return;
        ed.doc().beginEdit();
        try {
            RoadNetwork n = net_;
            const int32_t id = roads::addEdge(n, types_, spec);
            const int bridges = roads::bridgeWater(n, types_, id, roads::waterOf(ed.doc()), roadCellMeters(ed.ws().project.rulesJson["roadTypes"]));
            parseRoadNetwork(roadNetworkToJson(n), types_);
            roads::write(ed.doc(), n);
            ed.doc().touched();
            ed.status = "Дорога добавлена: " + (typeOf(type_) ? typeOf(type_)->name : type_) +
                        (bridges > 0 ? " (через воду — асфальтовый мост)" : "");
            refresh(ed);
            // chain on from the end
            const RoadEdge& e = net_.edges[size_t(net_.edgeIndex(id))];
            start_ = {};
            start_.kind = roads::Snap::Node; start_.node = e.b; start_.p = net_.nodes[size_t(net_.nodeIndex(e.b))].p;
            startY_ = net_.nodes[size_t(net_.nodeIndex(e.b))].y;
            tangent_ = geom_.edges[size_t(net_.edgeIndex(id))].pts.back().t; haveTangent_ = true;
            fromLanes_ = {};
            step_ = spec.attachB.edge >= 0 || hover_.kind != roads::Snap::None ? 0 : 1;
        } catch (const std::exception& e) { err_ = e.what(); ed.status = err_; }
    }

    // the edge from start_ to `end` as the current mode makes it
    bool build(Editor& ed, const roads::Snap& end, roads::AddSpec& s) {
        (void)ed;
        s.type = type_;
        s.a = start_;
        s.b = end;
        s.ya = startY_;
        s.yb = end.kind == roads::Snap::Node ? net_.nodes[size_t(net_.nodeIndex(end.node))].y : fx(elev_);
        s.attachA = fromLanes_;
        if (lanes_) { RoadAttach at; if (pickLanes(end, at, false)) s.attachB = at; }
        const Vec2 A = startPoint(), B = end.p;
        if ((B - A).length() < Fixed::fromRatio(1, 2)) return false;
        if (mode_ == 1 && step_ == 2) { s.curved = true; roads::quadraticToCubic(A, ctrl_, B, s.c1, s.c2); }
        else if (mode_ == 2 && haveTangent_) {
            const Fixed d = (B - A).length() / 2;
            roads::quadraticToCubic(A, A + tangent_ * d, B, s.c1, s.c2);
            s.curved = true;
        } else if (s.attachA.edge >= 0 && haveTangent_) {   // a lane branch leaves tangent to its road
            const Fixed d = (B - A).length() / 2;
            roads::quadraticToCubic(A, A + tangent_ * d, B, s.c1, s.c2);
            s.curved = true;
        }
        if (s.curved) {
            s.heights = true;
            s.y1 = s.ya + (s.yb - s.ya) / 3;
            s.y2 = s.ya + (s.yb - s.ya) * Fixed::fromRatio(2, 3);
        }
        return true;
    }
    Vec2 startPoint() const { return start_.p; }

    void updateGhost(Editor& ed) {
        RtsRoadView::Ghost g;
        if (mode_ <= 2 && step_ >= 1 && hoverOk_) {
            roads::AddSpec s;
            if (build(ed, hover_, s) && typeOf(type_)) {
                RoadNetwork n;
                RoadNode a{1, startPoint(), s.ya}, b{2, hover_.p, s.yb};
                // a branch starts on its lanes: show it there
                if (s.attachA.edge >= 0) {
                    const RoadEdge& P = net_.edges[size_t(s.attachA.edge)];
                    const RoadTypeDef& pt = types_[size_t(P.typeIndex)];
                    const Fixed off = (roadLaneOffset(pt, s.attachA.dir * (P.reversed ? -1 : 1), s.attachA.lane0) + roadLaneOffset(pt, s.attachA.dir * (P.reversed ? -1 : 1), s.attachA.lane1)) / 2;
                    Fixed along, lat;
                    roadProject(geom_.edges[size_t(s.attachA.edge)], a.p, along, lat);
                    const RoadSample r = roadSampleAt(geom_.edges[size_t(s.attachA.edge)], along);
                    a.p = r.p + roadNormal(r.t) * off;
                }
                n.nodes = {a, b};
                RoadEdge e;
                e.id = 1; e.a = 1; e.b = 2; e.type = type_; e.typeIndex = findRoadType(types_, type_);
                e.curved = s.curved; e.c1 = s.c1; e.c2 = s.c2;
                e.y1 = s.curved ? s.y1 : s.ya + (s.yb - s.ya) / 3; e.y2 = s.curved ? s.y2 : s.ya + (s.yb - s.ya) * Fixed::fromRatio(2, 3);
                if (s.attachA.edge >= 0) { e.c1 = e.curved ? e.c1 + (a.p - startPoint()) : e.c1; }
                n.edges.push_back(e);
                g.on = true; g.net = n; g.ok = true;
            }
        }
        // only rebuild the ghost when it changed
        const std::string key = g.on ? roadNetworkToJson(g.net).dump(0) : std::string();
        if (key == ghostKey_) return;
        ghostKey_ = key;
        ed.view().roads().setGhost(g);
    }
    std::string ghostKey_;

    // ---------------------------------------------------------------- editing
    void editInput(Editor& ed, ImGuiIO& io, Viewport& vp) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp.hovered && hoverOk_) {
            dragging_ = 0;
            // a handle of the selected edge first
            if (selEdge_ >= 0) {
                const RoadEdge& e = net_.edges[size_t(net_.edgeIndex(selEdge_))];
                if (e.curved) {
                    float x, z;
                    ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z);
                    if (std::hypot(fl(e.c1.x) - x, fl(e.c1.y) - z) < 1.0f) dragging_ = 2;
                    else if (std::hypot(fl(e.c2.x) - x, fl(e.c2.y) - z) < 1.0f) dragging_ = 3;
                }
            }
            if (!dragging_) {
                if (hover_.kind == roads::Snap::Node) { selNode_ = hover_.node; selEdge_ = -1; dragging_ = 1; }
                else if (hover_.kind == roads::Snap::Edge) { selEdge_ = net_.edges[size_t(hover_.edge)].id; selNode_ = -1; }
                else { selEdge_ = selNode_ = -1; }
            }
            if (dragging_) { editBegun_ = false; dragY0_ = io.MousePos.y; dragElev0_ = selNode_ >= 0 ? fl(net_.nodes[size_t(net_.nodeIndex(selNode_))].y) : 0; }
        }
        if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
            float x, z;
            if (!ed.groundAt(vp, ed.sceneCam, io.MousePos.x, io.MousePos.y, x, z)) return;
            if (ed.snap) { x = std::round(x * 2) / 2; z = std::round(z * 2) / 2; }
            if (!editBegun_) { ed.doc().beginEdit(); editBegun_ = true; }
            RoadNetwork n = net_;
            try {
                if (dragging_ == 1) {
                    const RoadNode& d = n.nodes[size_t(n.nodeIndex(selNode_))];
                    if (io.KeyShift) roads::moveNode(n, selNode_, d.p, fx(dragElev0_ + (dragY0_ - io.MousePos.y) * 0.1f));   // Shift: height
                    else roads::moveNode(n, selNode_, v2(x, z), d.y);
                } else {
                    RoadEdge& e = n.edges[size_t(n.edgeIndex(selEdge_))];
                    (dragging_ == 2 ? e.c1 : e.c2) = v2(x, z);
                }
                roads::write(ed.doc(), n);
                ed.doc().touched();
                refresh(ed);
            } catch (const std::exception& e) { err_ = e.what(); }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = 0;
    }

    void apply(Editor& ed, const std::function<void(RoadNetwork&)>& fn, const char* what) {
        ed.doc().beginEdit();
        RoadNetwork n = net_;
        try {
            fn(n);
            parseRoadNetwork(roadNetworkToJson(n), types_);
            roads::write(ed.doc(), n);
            ed.doc().touched();
            ed.status = what;
            err_.clear();
            refresh(ed);
        } catch (const std::exception& e) { err_ = e.what(); }
    }
    void deleteSelected(Editor& ed) {
        if (selEdge_ >= 0) { const int32_t id = selEdge_; apply(ed, [id](RoadNetwork& n) { roads::deleteEdge(n, id); }, "Дорога удалена"); selEdge_ = -1; }
        else if (selNode_ >= 0) { const int32_t id = selNode_; apply(ed, [id](RoadNetwork& n) { roads::deleteNode(n, id); }, "Узел удалён"); selNode_ = -1; }
    }
    void eraseAt(Editor& ed) {
        if (hover_.kind == roads::Snap::Edge) { const int32_t id = net_.edges[size_t(hover_.edge)].id; apply(ed, [id](RoadNetwork& n) { roads::deleteEdge(n, id); }, "Дорога удалена"); }
        else if (hover_.kind == roads::Snap::Node) { const int32_t id = hover_.node; apply(ed, [id](RoadNetwork& n) { roads::deleteNode(n, id); }, "Узел удалён"); }
    }

    // a bus stop bay where the road is clicked, on the side clicked; Shift: remove the nearest one
    void stopAt(Editor& ed, bool remove) {
        if (hover_.kind != roads::Snap::Edge) { ed.status = "Щёлкните по дороге у бордюра — на той стороне появится остановка с карманом"; return; }
        Json q = Json::object();
        q.set("edge", Json::number(int64_t(net_.edges[size_t(hover_.edge)].id)));
        Json p = Json::array();
        p.push(Json::number(hover_.p.x + roadNormal(roadSampleAt(geom_.edges[size_t(hover_.edge)], hover_.s).t).x * hover_.lateral));
        p.push(Json::number(hover_.p.y + roadNormal(roadSampleAt(geom_.edges[size_t(hover_.edge)], hover_.s).t).y * hover_.lateral));
        q.set("point", p);
        if (remove) q.set("remove", Json::fromBool(true));
        const Json r = ed.ws().call("road_stop_set", q);
        if (r.has("error")) err_ = r.getStr("error"); else { err_.clear(); ed.status = remove ? "Остановка удалена" : "Остановка с карманом добавлена"; }
    }

    void inspector(Editor& ed) {
        if (selEdge_ >= 0) {
            const int ei = net_.edgeIndex(selEdge_);
            if (ei < 0) return;
            const RoadEdge e = net_.edges[size_t(ei)];
            ui::section(ICON_ROADS, ("Дорога " + std::to_string(e.id)).c_str());
            if (ui::beginProps("##edge")) {
                ui::prop("Тип");
                ImGui::SetNextItemWidth(-1);
                const RoadTypeDef* t = typeOf(e.type);
                if (ImGui::BeginCombo("##etype", t ? t->name.c_str() : e.type.c_str())) {
                    for (auto& tt : types_)
                        if (ImGui::Selectable(tt.name.c_str(), tt.id == e.type)) {
                            const std::string id = tt.id;
                            const int32_t eid = e.id;
                            apply(ed, [&, id, eid](RoadNetwork& n) { auto& x = n.edges[size_t(n.edgeIndex(eid))]; x.type = id; x.typeIndex = findRoadType(types_, id); }, "Тип дороги изменён");
                        }
                    ImGui::EndCombo();
                }
                float y1 = fl(e.y1), y2 = fl(e.y2);
                ui::prop("Высота ручек", "Метры над землёй у контрольных точек сплайна");
                ImGui::SetNextItemWidth(-1);
                float yy[2] = {y1, y2};
                if (ImGui::DragFloat2("##yy", yy, 0.1f, -30, 40, "%.1f м")) {
                    const int32_t eid = e.id;
                    const float a = yy[0], b = yy[1];
                    apply(ed, [eid, a, b](RoadNetwork& n) { auto& x = n.edges[size_t(n.edgeIndex(eid))]; x.y1 = fx(a); x.y2 = fx(b); }, "Высота дороги");
                }
                ui::endProps();
            }
            const int32_t eid = e.id;
            if (ui::wideButton(ICON_ROADS, "Развернуть", 0, 3)) apply(ed, [eid](RoadNetwork& n) { auto& x = n.edges[size_t(n.edgeIndex(eid))]; x.reversed = !x.reversed; }, "Направление изменено");
            ImGui::SameLine();
            if (ui::wideButton(ICON_ROADS, "Выпрямить", 1, 3)) apply(ed, [eid](RoadNetwork& n) { n.edges[size_t(n.edgeIndex(eid))].curved = false; }, "Дорога выпрямлена");
            ImGui::SameLine();
            if (ui::wideButton(ICON_ERASER, "Удалить", 2, 3)) deleteSelected(ed);
        } else if (selNode_ >= 0) {
            const int k = net_.nodeIndex(selNode_);
            if (k < 0) return;
            const RoadNode d = net_.nodes[size_t(k)];
            ui::section(ICON_ROADS, ("Узел " + std::to_string(d.id)).c_str());
            float p[3] = {fl(d.p.x), fl(d.p.y), fl(d.y)};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##node", p, 0.1f, -1000, 1000, "%.2f")) {
                const int32_t id = d.id;
                const float a = p[0], b = p[1], c = p[2];
                apply(ed, [id, a, b, c](RoadNetwork& n) { roads::moveNode(n, id, v2(a, b), fx(c)); }, "Узел перемещён");
            }
            ui::hint("x, z (клетки), высота (м). В «Сцене»: перетаскивание — место, Shift + перетаскивание — высота.");
        } else ui::hint("Щёлкните дорогу или узел в «Сцене». Ручки кривой выбранной дороги тоже можно тянуть.");
    }

    void palette(Editor& ed) {
        (void)ed;
        ui::section(ICON_ROADS, "Тип дороги");
        for (int c = 0; c < 4; ++c) {
            bool any = false;
            for (auto& t : types_) any |= t.category == kCats[c];
            if (!any) continue;
            if (!ui::collapsing(kCatNames[c], c == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
            for (auto& t : types_) {
                if (t.category != kCats[c]) continue;
                char label[200];
                const float w = fl(t.halfWidth(-1) + t.halfWidth(1));
                std::snprintf(label, sizeof label, "%s##%s", t.name.c_str(), t.id.c_str());
                if (ImGui::Selectable(label, type_ == t.id)) { type_ = t.id; if (mode_ > 2) mode_ = 0; }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
                ImGui::TextDisabled("%d+%d  %.1f", t.lanes(1), t.lanes(-1), w);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("полосы туда + обратно, ширина в клетках");
            }
        }
    }

    // the game's own road type: lanes, median, curbs, sidewalks, barriers, props
    void builder(Editor& ed) {
        if (!ui::collapsing("Свой тип дороги")) return;
        Json& d = draft_;
        auto num = [&](const char* label, const char* path, float def, float lo, float hi, const char* fmt, const char* tip = nullptr) {
            Json* o = &d;
            std::string p = path, key = p;
            if (auto dot = p.find('.'); dot != std::string::npos) { o = &d.member(p.substr(0, dot)); key = p.substr(dot + 1); }
            float v = float((*o)[key].toDouble(def));
            ui::prop(label, tip);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat((std::string("##") + path).c_str(), &v, 0.05f, lo, hi, fmt)) o->set(key, Json::number(double(v), 2));
        };
        auto pick = [&](const char* label, Json& o, const char* key, const char* const* ids, const char* const* names, int n) {
            int cur = 0;
            const std::string v = o.getStr(key, ids[0]);
            for (int i = 0; i < n; ++i) if (v == ids[i]) cur = i;
            ui::prop(label);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo((std::string("##") + label + key).c_str(), &cur, names, n)) o.set(key, Json::string(ids[cur]));
        };
        if (ui::beginProps("##builder")) {
            ui::prop("Id");
            ImGui::SetNextItemWidth(-1);
            ui::inputHint("##did", "my_road", draftId_);
            ui::prop("Основа");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##base", draftBase_.c_str())) {
                for (auto& t : types_) if (ImGui::Selectable(t.name.c_str(), t.id == draftBase_)) draftBase_ = t.id;
                ImGui::EndCombo();
            }
            std::string name = d.getStr("name");
            ui::prop("Название");
            ImGui::SetNextItemWidth(-1);
            if (ui::inputHint("##dname", "Моя дорога", name)) d.set("name", Json::string(name));
            Json& lanes = d.member("lanes");
            int fw = int(lanes.getInt("forward", typeOf(draftBase_) ? typeOf(draftBase_)->lanes(1) : 1));
            int bw = int(lanes.getInt("backward", typeOf(draftBase_) ? typeOf(draftBase_)->lanes(-1) : 1));
            ui::prop("Полосы туда");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##fw", &fw, 0, 6)) lanes.set("forward", Json::number(int64_t(fw)));
            ui::prop("Полосы обратно");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##bw", &bw, 0, 6)) lanes.set("backward", Json::number(int64_t(bw)));
            num("Ширина полосы", "lanes.width", 3.5f, 2.5f, 5, "%.2f м");
            num("Разделитель", "median.width", 0, 0, 20, "%.1f м", "Ширина разделительной полосы между направлениями");
            Json& med = d.member("median");
            pick("Покрытие разд.", med, "surface", kSurfaces, kSurfaceNames, 7);
            pick("Ограждение разд.", med, "barrier", kBarriers, kBarrierNames, 6);
            pick("Покрытие", d, "surface", kSurfaces, kSurfaceNames, 7);
            for (int side = 0; side < 2; ++side) {
                Json& s = d.member(side ? "right" : "left");
                const char* w = side ? "Справа: " : "Слева: ";
                pick((std::string(w) + "бордюр").c_str(), s, "curb", kCurbs, kCurbNames, 5);
                num((std::string(w) + "тротуар").c_str(), side ? "right.sidewalk" : "left.sidewalk", 2.5f, 0, 10, "%.1f м");
                num((std::string(w) + "обочина").c_str(), side ? "right.shoulder" : "left.shoulder", 0, 0, 5, "%.1f м");
                pick((std::string(w) + "ограждение").c_str(), s, "barrier", kBarriers, kBarrierNames, 6);
            }
            ui::endProps();
        }
        // props
        Json& props = d.member("props");
        if (!props.isArray()) d.set("props", Json::array());
        Json& pl = *d.find("props");
        for (size_t i = 0; i < pl.size(); ++i) {
            Json& p = pl.at(i);
            ImGui::PushID(int(i));
            int shape = 0, side = 0;
            for (int k = 0; k < 8; ++k) if (p.getStr("shape") == kShapes[k]) shape = k;
            const int64_t sv = p.getInt("side", 0);
            side = sv == -1 ? 1 : sv == 1 ? 2 : sv == 2 ? 3 : 0;
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("##shape", &shape, kShapeNames, 8)) p.set("shape", Json::string(kShapes[shape]));
            ImGui::SameLine(); ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("##side", &side, kSides, 4)) p.set("side", Json::number(int64_t(side == 1 ? -1 : side == 2 ? 1 : side == 3 ? 2 : 0)));
            float sp = float(p["spacing"].toDouble(20));
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            if (ImGui::DragFloat("##sp", &sp, 0.5f, 2, 200, "шаг %.0f м")) p.set("spacing", Json::number(double(sp), 1));
            ImGui::SameLine();
            if (ui::toolButton(ICON_ERASER, "Убрать")) { pl.eraseAt(i); ImGui::PopID(); break; }
            ImGui::PopID();
        }
        if (ui::button(ICON_PLUS, "Пропс")) { Json p = Json::object(); p.set("shape", Json::string("lamp")); p.set("spacing", Json::number(int64_t(24))); pl.push(p); }
        ImGui::SameLine();
        if (ui::button(ICON_CHECK, "Сохранить тип", true)) {
            Json t = d;
            t.set("extends", Json::string(draftBase_));
            if (t.getStr("name").empty()) t.set("name", Json::string(draftId_));
            if (t["props"].size() == 0) t.erase("props");
            Json q = Json::object();
            q.set("id", Json::string(draftId_));
            q.set("type", t);
            const Json r = ed.ws().call("road_type_set", q);
            if (r.has("error")) err_ = r.getStr("error");
            else { err_.clear(); type_ = draftId_; ed.reloadRules(); rulesKey_.clear(); ed.status = "Тип дороги сохранён: " + draftId_; }
        }
    }

    void issues(Editor& ed) {
        (void)ed;
        if (geom_.issues.empty()) return;
        ui::section(ICON_WARNING, "Проблемы");
        for (auto& s : geom_.issues) ui::toneText(ui::Tone::Warn, "%s", s.c_str());
    }

    // ---------------------------------------------------------------- scene overlay
    void overlay(Editor& ed, Viewport& vp) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->PushClipRect(ImVec2(vp.originX, vp.originY), ImVec2(vp.originX + vp.pointsW, vp.originY + vp.pointsH), true);
        const float k = 1.0f / cm(ed);
        auto toScreen = [&](Vec2 p, Fixed y, ImVec2& out) {
            const float x = fl(p.x), z = fl(p.y);
            render::float2 s;
            if (!ed.sceneCam.worldToScreen({x, ed.view().heightAt(ed.editGrid(), x, z) + fl(y) * k + 0.05f, z}, vp.pointsW, vp.pointsH, s)) return false;
            out = ImVec2(vp.originX + s.x, vp.originY + s.y);
            return true;
        };
        const ImU32 nodeCol = IM_COL32(255, 255, 255, 200), selCol = IM_COL32(255, 200, 60, 255), hovCol = IM_COL32(90, 200, 255, 255);
        for (auto& d : net_.nodes) {
            ImVec2 s;
            if (!toScreen(d.p, d.y, s)) continue;
            dl->AddCircleFilled(s, d.id == selNode_ ? 6.0f : 4.0f, d.id == selNode_ ? selCol : nodeCol);
            if (d.y.raw) { char b[32]; std::snprintf(b, sizeof b, "%.1f м", fl(d.y)); dl->AddText(ImVec2(s.x + 6, s.y - 14), IM_COL32(255, 255, 255, 220), b); }
        }
        // selected edge: centre line and the control polygon
        if (selEdge_ >= 0) {
            const int ei = net_.edgeIndex(selEdge_);
            const auto& g = geom_.edges[size_t(ei)];
            ImVec2 prev; bool have = false;
            for (auto& p : g.pts) { ImVec2 s; if (toScreen(p.p, p.e, s)) { if (have) dl->AddLine(prev, s, selCol, 2.5f); prev = s; have = true; } else have = false; }
            const RoadEdge& e = net_.edges[size_t(ei)];
            if (e.curved) {
                ImVec2 a, b, c1, c2;
                const RoadNode& A = net_.nodes[size_t(net_.nodeIndex(e.a))];
                const RoadNode& B = net_.nodes[size_t(net_.nodeIndex(e.b))];
                if (toScreen(A.p, A.y, a) && toScreen(e.c1, e.y1, c1)) { dl->AddLine(a, c1, IM_COL32(255, 255, 255, 140), 1.5f); dl->AddCircleFilled(c1, 5, hovCol); }
                if (toScreen(B.p, B.y, b) && toScreen(e.c2, e.y2, c2)) { dl->AddLine(b, c2, IM_COL32(255, 255, 255, 140), 1.5f); dl->AddCircleFilled(c2, 5, hovCol); }
            }
        }
        // lanes under the cursor (lane mode) and the chosen ones
        auto drawLanes = [&](int ei, int dir, int l0, int l1, ImU32 col) {
            const RoadEdge& e = net_.edges[size_t(ei)];
            const RoadTypeDef& t = types_[size_t(e.typeIndex)];
            const auto& g = geom_.edges[size_t(ei)];
            for (int l = l0; l <= l1; ++l) {
                ImVec2 prev; bool have = false;
                for (auto& p : g.pts) {
                    ImVec2 s;
                    if (toScreen(p.p + roadNormal(p.t) * roadLaneOffset(t, dir, l), p.e, s)) { if (have) dl->AddLine(prev, s, col, 3.0f); prev = s; have = true; }
                }
            }
        };
        if (lanes_ && hover_.kind == roads::Snap::Edge) {
            const RoadEdge& e = net_.edges[size_t(hover_.edge)];
            int dir, lane;
            if (roads::laneAt(types_[size_t(e.typeIndex)], e, hover_.lateral, dir, lane)) drawLanes(hover_.edge, dir, lane, lane, IM_COL32(90, 200, 255, 200));
        }
        if (fromLanes_.edge >= 0 && step_ >= 1) drawLanes(fromLanes_.edge, fromLanes_.dir, fromLanes_.lane0, fromLanes_.lane1, selCol);
        // the snap point and the curve control point
        if (hoverOk_) {
            ImVec2 s;
            if (toScreen(hover_.p, fx(elev_), s)) {
                const ImU32 c = hover_.kind == roads::Snap::Node ? selCol : hover_.kind == roads::Snap::Edge ? hovCol : IM_COL32(255, 255, 255, 230);
                dl->AddCircle(s, 8, c, 20, 2.0f);
                char b[96];
                std::snprintf(b, sizeof b, "%s  %.1f м", hover_.kind == roads::Snap::Node ? "узел" : hover_.kind == roads::Snap::Edge ? "на дороге" : "", elev_);
                dl->AddText(ImVec2(s.x + 10, s.y + 6), IM_COL32(255, 255, 255, 230), b);
            }
        }
        if (step_ == 2) { ImVec2 s; if (toScreen(ctrl_, fx(elev_), s)) dl->AddCircleFilled(s, 5, hovCol); }
        dl->PopClipRect();
    }
};

AIVERSE_EDITOR_WINDOW(RoadsWindow);

} // namespace aiverse::editor
