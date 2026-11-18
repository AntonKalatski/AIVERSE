#include "engine/view/View.h"
#include <cassert>
#include <cstddef>
#include <variant>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::view;
    ScenePacketBatch batch;
    batch.reserve(5);
    Object object;object.id=42;object.generation=7;
    MeshObject mesh;mesh.flags=128;
    LineObject line{{0,0,0},{0,1,0},0xff};
    TriangleObject triangle{{0,0,0},{1,0,0},{0,1,0},0xffffffff};
    CircleXZObject circle{{3,0,4},.7f,0xff00ff00u,24,.05f};
    batch.add(object);
    batch.add(mesh);
    batch.add(line);
    batch.add(triangle);
    batch.add(circle);
    assert(batch.size()==5);
    const auto& packets=batch.packets();
    assert(std::holds_alternative<Object>(packets[0]));
    assert(std::get<Object>(packets[0]).id==42);
    assert(std::holds_alternative<MeshObject>(packets[1]));
    assert(std::get<MeshObject>(packets[1]).flags==128);
    assert(std::holds_alternative<LineObject>(packets[2]));
    assert(std::get<LineObject>(packets[2]).end.y==1.f);
    assert(std::holds_alternative<TriangleObject>(packets[3]));
    assert(std::get<TriangleObject>(packets[3]).c.y==1.f);
    assert(std::holds_alternative<CircleXZObject>(packets[4]));
    assert(std::get<CircleXZObject>(packets[4]).segments==24);
    batch.clear();assert(batch.size()==0);
}
