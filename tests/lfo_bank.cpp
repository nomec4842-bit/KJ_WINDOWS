#include "core/tracks.h"
#include "core/track_type_synth.h"
#include "core/mod_matrix.h"
#include "core/project_io.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    check(lfoCount()==3,"Default LFO count changed");
    for(int i=0;i<20;++i){check(sourceLfoIndex(lfoSourceId(i))==i,"LFO source mapping is not reversible");check(lfoSourceId(i)!=3&&lfoSourceId(i)!=4&&lfoSourceId(i)!=5,"LFO collides with legacy envelope or macro");}
    initTracks();int track=getTracks().front().id;setLfoCount(20);
    for(int i=0;i<20;++i){trackSetLfoRate(track,i,.5f+i*.25f);trackSetLfoShape(track,i,static_cast<LfoShape>(i%4));trackSetLfoDeform(track,i,i/20.f);}
    auto route=modMatrixCreateAssignment();route.trackId=track;route.sourceIndex=lfoSourceId(19);route.parameterIndex=0;route.normalizedAmount=.5f;modMatrixUpdateAssignment(route);
    setLfoCount(3);check(trackGetLfoRate(track,19)==5.25f,"Reducing count destroyed LFO settings");check(modMatrixGetAssignment(route.id)->sourceIndex==22,"Reducing count destroyed routing");
    check(argc>1&&saveProjectToFile(argv[1]),"Failed to save LFO project");check(loadProjectFromFile(argv[1]),"Failed to recall LFO project");track=getTracks().front().id;
    for(int i=0;i<20;++i){check(trackGetLfoRate(track,i)==.5f+i*.25f,"LFO rate lost on recall");check(trackGetLfoShape(track,i)==static_cast<LfoShape>(i%4),"LFO shape lost on recall");check(std::abs(trackGetLfoDeform(track,i)-i/20.f)<.00001f,"LFO deform lost on recall");}
    auto routes=modMatrixGetAssignments();check(routes.size()==1&&routes[0].sourceIndex==22,"LFO 20 route lost on recall");
    setLfoCount(99);check(lfoCount()==20,"LFO count upper limit failed");setLfoCount(0);check(lfoCount()==1,"LFO count lower limit failed");std::cout<<"20 LFO settings, stable source IDs, and project recall passed\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
