// Water from splines (docs/water.md): straits, rivers and the open sea of a map ("waterNetwork"), their shore types
// (rules "shoreTypes": embankment, cliff, beach, natural bank) and the shore segments each bank is cut into.
//
// This is presentation and editor-time data. The simulation never reads it: the editor bakes the result into the
// map's cells ('~' water, '#' under cliffs, optional tile chars), and the simulation reads those as before. So float
// math is fine here, and the view, the editor and the headless tools see the same baked map.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../../engine/data/Json.h"

namespace aiverse::game::water {

enum class Kind { Strait, River, Sea };
enum class ShoreKind { Embankment, Cliff, Beach, Natural };
const char* kindName(Kind k);
const char* shoreKindName(ShoreKind k);

struct ShoreProp {
    std::string shape;        // built-in: lamp, bench, bollard, umbrella, lounger, lifeguard, boulder
    std::string model;        // a model of the game instead of the shape
    float spacing = 20;       // metres along the bank
    float phase = 0;          // metres from the segment start
    float offset = -1;        // metres from the water's edge landwards (-1: the kind's default place)
    float scale = 1;
};

// Values in metres (cellMeters turns them into cells), angles in degrees.
struct ShoreType {
    std::string id, name;
    ShoreKind kind = ShoreKind::Embankment;
    // embankment
    float wallHeight = 1.2f;           // coping over the water surface (at least the ground + 0.15)
    std::string wallSurface = "stone";
    std::string parapet = "railing";   // none | railing | stone
    float parapetHeight = 0.9f, postSpacing = 3;
    float promenade = 5;               // paved strip behind the wall
    std::string promenadeSurface = "paving";
    float stairs = 120;                // a flight of steps down to the water every N metres (0: none)
    // cliff
    float heightMin = 6, heightMax = 15, slopeMin = 70, slopeMax = 85;
    float top = 6;                     // flat top behind the edge, then the back slope down to the ground
    std::string rockSurface = "rock", topSurface = "grass";
    bool rubble = true, foam = true;
    // beach / natural bank
    float widthMin = 15, widthMax = 30;
    std::string sandSurface = "sand";
    bool surf = true;
    std::vector<ShoreProp> props;
    std::map<std::string, std::string> textures;   // surface -> the game's texture (stone, rock, sand, paving, ...)
    bool builtin = false;
};

struct ShoreTypes {
    float cellMeters = 4;
    std::vector<ShoreType> list;
    int find(const std::string& id) const;
    // the default type of a kind (the first one of that kind)
    int defaultOf(ShoreKind k) const;
};
// rules "shoreTypes" over the built-in library ("embankment", "embankment_stone", "cliff", "beach", "bank");
// throws std::runtime_error with a readable message
ShoreTypes loadShoreTypes(const Json& rulesShoreTypes);
Json shoreTypeToJson(const ShoreType& t);

struct Segment {
    int side = 1;              // strait / river: -1 left bank, 1 right bank (seen along the points); sea: 1
    float from = 0, to = 0;    // metres along the bank
    std::string type;          // shore type id
    bool generated = false;    // made by "auto" (recomputed on every bake); else drawn by hand and kept
};

struct Body {
    int id = 0;
    Kind kind = Kind::River;
    std::vector<std::array<float, 3>> points;   // strait / river: centre line x, z (cells), width (metres); sea: the shore line x, z
    std::string side = "south";                // sea: the map edge the water reaches (north | south | east | west)
    float depth = 3;                           // metres under the surface at the shore (walls, beaches)
    bool autoShores = true;                    // "shores": "auto" or a list with generated entries
    std::vector<Segment> shores;
};

struct Network {
    std::vector<Body> bodies;
    std::map<std::string, std::string> tiles;  // tile chars written by the bake: "water", "cliff" (maps with a tiles layer)
    Json baked;                                // what the last bake changed (restored by the next one)
    bool empty() const { return bodies.empty(); }
    int index(int id) const;
};
// map "waterNetwork"; throws std::runtime_error with a readable message (`water body 3: unknown shore type 'x'`)
Network parseNetwork(const Json& j, const ShoreTypes& types);
Json networkToJson(const Network& n);

// Banks: the water's edge as a polyline, landward normals, metres along.
struct BankPoint { float x = 0, z = 0, nx = 0, nz = 0, s = 0; bool open = false; };   // open: water on the land side too (a mouth), no shore
struct Bank {
    int body = 0;              // index into Network::bodies
    int side = 1;
    std::vector<BankPoint> pts;
    float length = 0;          // metres
    bool openStart = false, openEnd = false;   // the bank runs off the map there
};
struct Geometry {
    std::vector<Bank> banks;
    std::vector<std::string> issues;
};
Geometry buildGeometry(const Network& n, int w, int h, float cellMeters);
// bank of body index `bi`, side `side` (nullptr: none)
const Bank* findBank(const Geometry& g, int bi, int side);
// shore type at metre `s` of a bank (index into types, -1: none)
int shoreAt(const Network& n, const ShoreTypes& T, int bi, int side, float s);

// Cells under water (1) for a w x h map: every cell within 0.75 cell of the water (so no land cell reaches over the
// bank line; the shores cover the rest).
std::vector<uint8_t> waterCells(const Network& n, const Geometry& g, int w, int h, float cellMeters);
// Marks bank points with water on their land side too (where a strait or river meets the sea or another body): no
// shore is built there.
void markOpen(Geometry& g, const std::vector<uint8_t>& wet, int w, int h);

// Auto shores. built(x, z): a road, a building or a blocked cell there (the editor gathers it from the map).
// strait: embankment on both banks; river: embankment where the town reaches the water, else a natural bank; sea:
// embankment by the town, cliffs at headlands, map ends and between beaches, beaches on open land. Segments of at
// least 30 m; neighbours of one type merged. Hand-drawn segments are kept and auto fills the rest.
void classify(Network& n, const Geometry& g, const ShoreTypes& T, const std::function<bool(int, int)>& built, int w, int h);

// Cells a cliff stands on (blocked): 1. Land under the rock mass, from the edge to the foot of the back slope.
std::vector<uint8_t> cliffCells(const Network& n, const Geometry& g, const ShoreTypes& T, int w, int h);

// Cliff height (metres) at metre s of a bank inside segment [from, to): noise between the type's limits, tapering
// towards neighbours of another type (a stony slope there). Shared by the view and the bake.
float cliffHeight(const ShoreType& t, int body, int side, float s, float from, float to, bool taperStart, bool taperEnd);
float beachWidth(const ShoreType& t, int body, int side, float s);
// cells of land a shore needs behind the water's edge
float footprintCells(const ShoreType& t, float cellMeters);
float hash01(int a, int b, int c);   // deterministic noise 0..1
float noise1(float s, int seed);     // smooth value noise along a bank, 0..1

} // namespace aiverse::game::water
