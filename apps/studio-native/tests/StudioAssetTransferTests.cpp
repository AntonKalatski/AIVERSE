#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetTransfers.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
using aiverse::studio::StudioAssetCatalog;
using aiverse::studio::StudioTransferDecision;
using aiverse::studio::StudioTransferPlan;

void write(const fs::path& path,const std::string& text){
    std::ofstream file(path,std::ios::binary);
    assert(file);
    file<<text;
}
std::string read(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(){
    const fs::path temp=fs::temp_directory_path()/("studio-transfer-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path project=temp/"Project", external=temp/"External";
    fs::create_directories(project/"content"/"Models");
    fs::create_directories(project/"content"/"Other");
    fs::create_directories(external);
    try{
        const std::string folder=aiverse::studio::studioCreateContentFolder(project,"Models","Vehicles");
        assert(folder=="Models/Vehicles");
        write(project/"content"/"Models"/"Ship.glb","original");
        write(external/"Ship.glb","replacement");
        write(external/"Drone.glb","new");
        StudioAssetCatalog db(project);
        db.rebuild();
        const auto original=db.byPath("Models/Ship.glb");
        assert(original);
        const auto stableGuid=original->guid;

        // Preview name collision, skip by default and leave content unchanged.
        auto skip=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        assert(skip.describe()["items"][0].getStr("conflict")=="name");
        auto skipped=skip.apply(skip.token(),{{0,"skip"}});
        assert(skipped["files"].size()==0);
        assert(read(project/"content"/"Models"/"Ship.glb")=="original");

        // Keep both: no existing file touched, fresh GUID for the copy.
        auto both=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto copied=both.apply(both.token(),{{0,"keep-both"}});
        assert(copied["files"].size()==1);
        assert(read(project/"content"/"Models"/"Ship (1).glb")=="replacement");
        db.rebuild();
        assert(db.byPath("Models/Ship (1).glb")->guid!=stableGuid);

        // Replace is explicit and preserves destination sidecar/GUID.
        auto replace=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto replaced=replace.apply(replace.token(),{{0,"replace"}});
        assert(replaced["files"].size()==1);
        assert(read(project/"content"/"Models"/"Ship.glb")=="replacement");
        db.rebuild();
        assert(db.byPath("Models/Ship.glb")->guid==stableGuid);

        // A GUID lookup follows internal file+meta move to a different folder.
        auto move=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
        assert(move.describe()["items"][0].getStr("conflict").empty());
        const auto moved=move.apply(move.token(),{});
        assert(moved["files"].size()==1);
        db.rebuild();
        assert(!db.byPath("Models/Ship.glb"));
        assert(db.byGuid(stableGuid)->path=="Other/Ship.glb");

        // A cloned external sidecar is a GUID conflict even without a name collision.
        write(external/"Clone.glb","clone");
        fs::copy_file(project/"content"/"Other"/"Ship.glb.meta",
                      external/"Clone.glb.meta");
        auto guidConflict=StudioTransferPlan::external(project,"Models",{external/"Clone.glb"});
        assert(guidConflict.describe()["items"][0].getStr("conflict")=="guid");
        auto fixed=guidConflict.apply(guidConflict.token(),{{0,"new-guid"}});
        assert(fixed["files"].size()==1);
        db.rebuild();
        assert(db.byPath("Models/Clone.glb")->guid!=stableGuid);

        // Reject a stale preview token before modifying anything.
        auto bad=StudioTransferPlan::external(project,"Models",{external/"Drone.glb"});
        bool rejected=false;
        try{(void)bad.apply("incorrect-token",{});}
        catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
        assert(!fs::exists(project/"content"/"Models"/"Drone.glb"));

        // Copy a genuine nested external directory without trusting its .meta.
        fs::create_directories(external/"Textures"/"Rocks");
        write(external/"Textures"/"Rocks"/"Stone.png","pixels");
        auto folderPlan=StudioTransferPlan::external(project,"Models",{external/"Textures"});
        auto importedFolder=folderPlan.apply(folderPlan.token(),{});
        assert(importedFolder["files"].size()==1);
        assert(read(project/"content"/"Models"/"Textures"/"Rocks"/"Stone.png")=="pixels");
        db.rebuild();
        assert(db.byPath("Models/Textures/Rocks/Stone.png"));
        std::cout<<"PASS studio conflict resolution and GUID-preserving asset transfers\n";
    }catch(...){fs::remove_all(temp);throw;}
    fs::remove_all(temp);
}
