#include "gui/editing_panel_model.h"
#include <stdexcept>
#include <iostream>
void check(bool ok){if(!ok)throw std::runtime_error("Editing panel invariant failed");}
int main(){try{
    editing::Binding fx,mod;
    fx.follow(1,false);mod.follow(1,false);fx.pinned=true;
    fx.follow(2,true);mod.follow(2,true);check(fx.track==1&&mod.track==2);
    mod.pinned=true;fx.follow(3,false);mod.follow(3,true);
    check(!fx.pinned&&fx.track==3&&mod.pinned&&mod.track==2);
    mod.pinned=false;mod.follow(3,true);check(mod.track==3);
    fx.reset();mod.reset();check(!fx.pinned&&!mod.pinned&&fx.track==0&&mod.track==0);
    for(int dpi:{96,144,192}){
        int threshold=846*dpi/96;
        check(!editing::splitFits(true,threshold-1,dpi));
        check(editing::splitFits(true,threshold,dpi));
        check(!editing::splitFits(false,threshold,dpi));
        for(int height:{514,900,1400}){
            check(height-editing::panelHeight(height)==304);
            check(editing::panelHeight(height+100)-editing::panelHeight(height)==100);
        }
        check(editing::panelHeight(200)==0);
        check(editing::panelHeight(0)==0);
        // The default window's client height does not grow with display DPI.
        // Both toolbar and pane header must leave visible editor content.
        int actualClientHeight=514;
        int panel=editing::panelHeight(actualClientHeight);
        check(actualClientHeight-panel==304);
        check(panel-(36+34)*dpi/96>=48);
    }
    std::cout<<"Panel bindings and layout passed at 100%, 150%, 200%\n";
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
