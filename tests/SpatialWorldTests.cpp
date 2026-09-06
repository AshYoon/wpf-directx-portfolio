#include "SpatialWorld.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

using scene::SpatialWorld;
using scene::QueryResult;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Same(const SpatialWorld& world, float x, float y, float radius) {
    QueryResult linear, grid;
    world.LinearQuery(x,y,radius,linear);
    world.GridQuery(x,y,radius,grid);
    std::sort(grid.hits.begin(),grid.hits.end());
    Require(linear.hits == grid.hits, "Grid and linear results differ");
    Require(std::adjacent_find(grid.hits.begin(),grid.hits.end()) == grid.hits.end(), "Duplicate hit");
    Require(grid.candidates.size() <= linear.candidates.size(), "Too many candidates");
}
void Correctness() {
    SpatialWorld world;
    world.SetParticles({});
    Same(world, 50,50,0);
    world.SetParticles({{0,0,0,0},{100,100,0,0},{5,5,0,0},{10,5,0,0},{5,10,0,0},{5,5,0,0}});
    QueryResult result;
    world.GridQuery(5,5,5,result);
    Require(result.hits.size() == 4, "Inclusive circle or cell boundary failed");
    world.GridQuery(5,5,0,result);
    Require(result.hits.size() == 2, "Coincident zero-radius query failed");
    for (float x : {0.0f,5.0f,50.0f,100.0f})
        for (float y : {0.0f,5.0f,50.0f,100.0f})
            for (float r : {0.0f,5.0f,12.0f,50.0f,200.0f}) Same(world,x,y,r);
    std::mt19937 random(2026);
    std::uniform_real_distribution<float> position(0,100), radius(0,50);
    for (bool clustered : {false,true}) {
        for (uint32_t count : {10u,100u,1000u,10000u}) {
            world.Reset(count,42,clustered);
            for (int i=0;i<100;++i) {
                world.Move(0.05f);
                world.Rebuild();
                Same(world,position(random),position(random),radius(random));
            }
            Same(world,50,50,200);
        }
    }
    SpatialWorld other;
    world.Reset(1000,42,false); other.Reset(1000,42,false);
    for (size_t i=0;i<world.Particles().size();++i)
        Require(world.Particles()[i].x == other.Particles()[i].x &&
            world.Particles()[i].vy == other.Particles()[i].vy, "Seed reset is not deterministic");
    world.SetParticles({{99.99f,0.01f,5,-5}});
    world.Move(0.05f); world.Rebuild();
    Require(world.Particles()[0].x <= 100 && world.Particles()[0].y >= 0, "Boundary reflection failed");
    Same(world,100,0,1);
    bool rejected=false;
    try { world.GridQuery(std::numeric_limits<float>::quiet_NaN(),50,5,result); }
    catch (const std::invalid_argument&) { rejected=true; }
    Require(rejected,"NaN query accepted");
    rejected=false;
    try { world.Reset(10001,42,false); }
    catch (const std::invalid_argument&) { rejected=true; }
    Require(rejected,"Oversized world accepted");
    std::cout << "PASS: randomized query comparisons, inclusive boundaries, duplicates, reset, movement and input validation\n";
}
template<class F> double Measure(F&& work) {
    const auto start=std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void Benchmark() {
    // Median of 15 batches. 64 different queries on the SAME stationary snapshot.
    // Both methods reuse their output storage; first batch warms capacities.
    std::cout << "distribution,objects,queries,linear_batch_ms,grid_batch_ms,rebuild_ms,linear_checks,grid_checks\n";
    for (bool clustered : {false,true}) for (uint32_t count : {10u,100u,1000u,10000u}) {
        SpatialWorld world; world.Reset(count,42,clustered);
        QueryResult linear,grid;
        linear.hits.reserve(count); linear.candidates.reserve(count);
        grid.hits.reserve(count); grid.candidates.reserve(count);
        std::vector<double> lt,gt,bt;
        uint64_t linearChecks=0,gridChecks=0,checksum=0;
        for(int batch=-1;batch<15;++batch) {
            auto rebuild=Measure([&]{world.Rebuild();});
            uint64_t lc=0,gc=0;
            auto run=[&](bool useGrid) {
                for(int q=0;q<64;++q) {
                    float x=10.0f+(q%8)*11.0f, y=10.0f+(q/8)*11.0f;
                    auto& result=useGrid?grid:linear;
                    if(useGrid) world.GridQuery(x,y,12,result);
                    else world.LinearQuery(x,y,12,result);
                    checksum += result.hits.size();
                    (useGrid?gc:lc) += result.candidates.size();
                }
            };
            double l=0,g=0;
            if(batch%2==0) {l=Measure([&]{run(false);});g=Measure([&]{run(true);});}
            else {g=Measure([&]{run(true);});l=Measure([&]{run(false);});}
            if(batch>=0) {lt.push_back(l);gt.push_back(g);bt.push_back(rebuild);}
            linearChecks=lc;gridChecks=gc;
        }
        std::sort(lt.begin(),lt.end());std::sort(gt.begin(),gt.end());std::sort(bt.begin(),bt.end());
        std::cout << (clustered?"clustered":"uniform") << ',' << count << ",64,"
            << lt[7] << ',' << gt[7] << ',' << bt[7] << ',' << linearChecks << ',' << gridChecks << '\n';
        if(checksum==std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Unreachable checksum");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="--benchmark") Benchmark();
        else Correctness();
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}