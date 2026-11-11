// Explicit OFFLINE authored sun for private runtime validation. No inverse fitting.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "resolved_sun_fixture.h"
#include "lightrecon/cache.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: lightrecon_sun_fixture_tool private-cache-root dependency-console.log");
        std::ifstream in(argv[2]); if(!in) throw std::runtime_error("Missing dependency log");
        Disk::Fingerprint f; std::string expected, line;
        while(std::getline(in,line)) {
            auto p=line.find("dependency-manifest algorithm=");
            if(p!=std::string::npos) {
                f={}; f.complete.fill(true); std::istringstream row(line.substr(p)); std::string token; row>>token;
                while(row>>token) {
                    auto split=token.find('='); auto k=token.substr(0,split),v=token.substr(split+1);
                    if(k=="algorithm") f.algorithm=v;
                    else if(k=="version") f.algorithmVersion=uint32_t(std::stoul(v));
                    else if(k=="world-units") f.units.worldUnitsPerMeter=std::stof(v);
                    else if(k=="physical-to-scene") f.units.physicalToScene=std::stof(v);
                    else if(k=="key") expected=v;
                }
            }
            p=line.find("dependency kind="); if(p==std::string::npos) continue;
            std::istringstream row(line.substr(p)); std::string token; row>>token; Disk::InputContent c;
            while(row>>token) {
                auto split=token.find('='); auto k=token.substr(0,split),v=token.substr(split+1);
                if(k=="kind") c.kind=Disk::InputKind(std::stoul(v));
                else if(k=="bytes") c.bytes=std::stoull(v);
                else if(k=="name") c.name=v;
                else if(k=="sha256") {
                    if(v.size()!=64) throw std::runtime_error("Bad SHA256");
                    for(size_t j=0;j<32;++j) c.sha256[j]=uint8_t(std::stoul(v.substr(2*j,2),nullptr,16));
                }
            }
            f.inputs.push_back(c);
        }
        Disk::Digest key; auto result=Disk::MakeKey(f,key);
        if(!result || expected.empty() || Disk::Hex(key)!=expected) throw std::runtime_error("Incomplete/incorrect dependency manifest");
        auto a=Fixture(); a.units=f.units;
        // Deliberately new authored direction, not a claimed stock source.
        a.emitters[0].frame.right=Vec3(0,1,0);
        a.emitters[0].frame.up=Vec3(0.8f,0,-0.6f);
        a.emitters[0].frame.forward=Vec3(-0.6f,0,-0.8f);
        for(const auto& input:f.inputs) if(input.kind==Disk::InputKind::Bsp) {
            a.assessment.map=input.name; a.assessment.bspSha256=Disk::Hex(input.sha256);
        }
        Diagnostic d; d.code="C01_AUTHORED_TEST_ONLY";
        d.message="100000 lux manually authored validation sun; NOT recovered original-stock lighting; no diffuse baseline exclusion proof";
        a.diagnostics.push_back(d);
        result=Disk::Cache(argv[1]).Store(f,a);
        if(!result) throw std::runtime_error(result.message);
        std::cout<<"C01 manually authored physical fixture key="<<expected<<"; NOT stock recovery\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
