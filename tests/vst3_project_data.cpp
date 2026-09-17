#include "core/project_io.h"
#include "core/tracks.h"
#include "core/mod_matrix.h"
#ifdef KJ_ENABLE_VST3
#include "hosting/TrackVST3.h"
#endif
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected scratch project path"); initTracks(); int id = getTracks().front().id;
        check(trackGetFxOrder(id).empty()&&!trackGetEqEnabled(id),"Initial track has default effects");
        int added=addTrack().id;
        check(trackGetFxOrder(added).empty()&&!trackGetEqEnabled(added),"Added track has default effects");
        check(saveProjectToFile(argv[1])&&loadProjectFromFile(argv[1]),"Empty rack save/load failed");
        for(const auto& t:getTracks())check(trackGetFxOrder(t.id).empty()&&!trackGetEqEnabled(t.id),"Empty rack gained effects on recall");
        id=getTracks().front().id;
        kj::Vst3SlotState slot; slot.id = "stable-slot"; slot.name = "Missing synth"; slot.instrument = true;
        slot.path = "Z:/not-installed.vst3"; slot.classId = "1234"; slot.bypass = true;
        slot.state.hasComponent = slot.state.hasController = true;
        slot.state.component = std::string("\0\xff\"\n", 4); slot.state.controller = "controller";
        slot.state.parameters = {{4294967295u, 0.12345678901234567}};
        slot.learnedParameters = {{4294967295u, "Cutoff \"Hz\""}};
        ModMatrixAssignment route; route.id=1;route.trackId=id;route.parameterIndex=-1;route.vstSlotId=slot.id;
        route.vstParameterId=4294967295u;route.vstParameterName="Cutoff \"Hz\"";route.normalizedAmount=-0.25f;
        modMatrixSetAssignments({route});
        trackSetVst3State(id, {slot}); trackSetType(id, TrackType::Vst3);
        const std::vector<std::string> nativeOrder{"kj:delay","kj:eq","kj:sidechain"};
        trackSetEqShape(id,0,4);trackSetEqShape(id,1,5);trackSetEqShape(id,2,2);
        trackSetFxOrder(id,nativeOrder); trackSetDelayEnabled(id,true);
#ifdef KJ_ENABLE_VST3
        kj::restoreTrackVst3(id, {slot}, 44100);
#endif
        check(saveProjectToFile(argv[1]) && loadProjectFromFile(argv[1]), "Missing plugin save/load failed");
        auto restored = trackGetVst3State(getTracks().front().id);
        check(trackGetEqShape(getTracks().front().id,0)==4&&trackGetEqShape(getTracks().front().id,1)==5&&trackGetEqShape(getTracks().front().id,2)==2,"EQ shape recall failed");
        check(trackGetFxOrder(getTracks().front().id)==nativeOrder&&trackGetDelayEnabled(getTracks().front().id),"Native order/bypass recall failed");
        check(restored.size() == 1 && restored[0].id == slot.id && restored[0].bypass && restored[0].instrument, "Metadata lost");
        check(restored[0].state.component == slot.state.component && restored[0].state.controller == slot.state.controller && restored[0].state.parameters == slot.state.parameters, "Binary/parameter state changed");
        check(restored[0].learnedParameters.size()==1 && restored[0].learnedParameters[0].id==4294967295u && restored[0].learnedParameters[0].name==slot.learnedParameters[0].name,"Learned parameter lost");
        auto routes=modMatrixGetAssignments();check(routes.size()==1 && routes[0].vstSlotId==slot.id && routes[0].vstParameterId==4294967295u && routes[0].vstParameterName==route.vstParameterName && routes[0].normalizedAmount==-.25f,"Plugin route lost");
        { std::ofstream f(argv[1]); f << R"({"tracks":[{"vst3":[{"component":"invalid"}]}]})"; }
        check(!loadProjectFromFile(argv[1]), "Malformed plugin stream accepted");
        check(trackGetVst3State(getTracks().front().id)[0].id == slot.id, "Malformed load destroyed session");
        { std::ofstream f(argv[1]); f << R"({"version":1,"tracks":[{"name":"Old project","type":"Synth"}]})"; }
        check(loadProjectFromFile(argv[1]) && trackGetVst3State(getTracks().front().id).empty(), "Legacy project did not load cleanly");
        check(trackGetEqShape(getTracks().front().id,0)==0,"Legacy bell silently converted to shelf");
        check(trackGetFxOrder(getTracks().front().id)==std::vector<std::string>({"kj:eq","kj:compressor","kj:delay","kj:sidechain"}),"Legacy native order changed");
#ifdef KJ_ENABLE_VST3
        check(!kj::hasTrackVst3(), "Old project retained plugins");
#endif
        std::cout << "PASS binary state and parameter preservation, malformed input, legacy project\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
