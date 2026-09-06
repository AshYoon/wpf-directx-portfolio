#include <Windows.h>
#include "SceneRenderer.h"
#include <iostream>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct Instance {
    SR_Handle renderer=nullptr;
    HWND window=nullptr;
    ~Instance() {SR_Destroy(renderer); if(window) DestroyWindow(window);}
};
}
int main() {
    try {
        Require(SR_GetApiVersion()==2,"ABI version");
        Require(SR_Create(nullptr)==E_POINTER,"null create output");
        Require(SR_Destroy(nullptr)==S_OK,"null destroy");
        Require(SR_EnqueueQuery(nullptr,0,0)==E_POINTER,"null enqueue handle");
        Require(SR_FlushCommands(nullptr)==E_POINTER,"null flush handle");
        Instance instance;
        Require(SR_Create(&instance.renderer)==S_OK,"create");
        SR_Settings settings{sizeof(SR_Settings),1000,42,0,1,1,1,12};
        Require(SR_Configure(instance.renderer,&settings,0)==E_UNEXPECTED,"configure before initialize");
        Require(SR_EnqueueConfigure(instance.renderer,&settings,0)==E_UNEXPECTED,"enqueue before initialize");
        Require(SR_Initialize(instance.renderer,nullptr)==E_INVALIDARG,"invalid HWND");
        instance.window=CreateWindowExW(0,L"STATIC",L"API Test",WS_OVERLAPPEDWINDOW,
            0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(instance.window!=nullptr,"test HWND");
        Require(SR_Initialize(instance.renderer,instance.window)==S_OK,"initialize after failed initialization");
        Require(SR_Initialize(instance.renderer,instance.window)<0,"double initialize");
        Require(SR_Resize(instance.renderer,640,480)==S_OK,"resize");
        Require(SR_Configure(instance.renderer,&settings,1)==S_OK,"configure");
        auto bad=settings; bad.size=1;
        Require(SR_Configure(instance.renderer,&bad,0)==E_INVALIDARG,"settings size validation");
        bad=settings;bad.count=10001;
        Require(SR_Configure(instance.renderer,&bad,0)==E_INVALIDARG,"count validation");
        Require(SR_Configure(instance.renderer,&settings,2)==E_INVALIDARG,"reset flag validation");
        Require(SR_SetQuery(instance.renderer,std::numeric_limits<float>::quiet_NaN(),0)<0,"NaN query");
        SR_Stats stats{};
        Require(SR_GetStats(instance.renderer,&stats)==E_INVALIDARG,"stats size validation");
        stats.size=sizeof(stats);
        Require(SR_GetStats(instance.renderer,&stats)==S_OK && stats.results_match && stats.count==1000,"initial snapshot");
        SR_Result foreignThread=0;
        std::thread thread([&]{
            SR_Stats other{};other.size=sizeof(other);
            foreignThread=SR_GetStats(instance.renderer,&other);
        });
        thread.join();
        Require(foreignThread==RPC_E_WRONG_THREAD,"thread affinity");

        // 외부 생산자는 장면을 직접 변경하지 않고 큐에 값으로 전달한다.
        SR_Result queued=E_FAIL, direct=0, foreignFlush=0;
        std::thread producer([&] {
            auto submitted=settings; submitted.count=100;
            queued=SR_EnqueueConfigure(instance.renderer,&submitted,1);
            submitted.count=10;
            if (queued==S_OK) queued=SR_EnqueueQuery(instance.renderer,0.75f,0.5f);
            if (queued==S_OK) queued=SR_EnqueueResize(instance.renderer,0,0);
            direct=SR_SetQuery(instance.renderer,0.5f,0.5f);
            foreignFlush=SR_FlushCommands(instance.renderer);
        });
        producer.join();
        Require(queued==S_OK && direct==RPC_E_WRONG_THREAD && foreignFlush==RPC_E_WRONG_THREAD,
            "enqueue thread access contract");
        Require(SR_GetStats(instance.renderer,&stats)==S_OK && stats.count==1000,"enqueue applied on producer");
        Require(SR_Render(instance.renderer)==S_FALSE,"queued minimize was not applied");
        Require(SR_GetStats(instance.renderer,&stats)==S_OK && stats.count==100 && stats.query_x>50,
            "copied configure or FIFO query was not applied");
        Require(SR_EnqueueResize(instance.renderer,640,480)==S_OK,"enqueue restore");
        Require(SR_Render(instance.renderer)>=0,"queued restore while suspended");

        bad=settings; bad.count=10001;
        Require(SR_EnqueueConfigure(instance.renderer,&bad,0)==E_INVALIDARG,"invalid queued settings");
        Require(SR_EnqueueConfigure(instance.renderer,&settings,2)==E_INVALIDARG,"invalid queued reset");
        Require(SR_EnqueueConfigure(instance.renderer,nullptr,0)==E_POINTER,"null queued settings");
        Require(SR_EnqueueQuery(instance.renderer,std::numeric_limits<float>::quiet_NaN(),0)==E_INVALIDARG,
            "invalid queued query");
        Require(SR_EnqueueResize(instance.renderer,16385,1)==E_INVALIDARG,"invalid queued size");
        for (int i=0;i<4096;++i)
            Require(SR_EnqueueQuery(instance.renderer,0.25f,0.25f)==S_OK,"queue filled too early");
        Require(SR_EnqueueQuery(instance.renderer,0,0)==HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
            "queue overload not rejected");
        Require(SR_FlushCommands(instance.renderer)==S_OK,"flush full queue");
        std::atomic<bool> submittedAll{true};
        std::thread producers[2];
        for (auto& worker : producers) worker=std::thread([&] {
            for (int i=0;i<100;++i)
                if (SR_EnqueueQuery(instance.renderer,0.25f,0.75f)!=S_OK) submittedAll=false;
        });
        for (auto& worker : producers) worker.join();
        Require(submittedAll && SR_EnqueueQuery(instance.renderer,0.5f,0.5f)==S_OK,"multiple producers");
        Require(SR_FlushCommands(instance.renderer)==S_OK,"owner flush");
        Require(SR_GetStats(instance.renderer,&stats)==S_OK && stats.query_x==50 && stats.query_y==50,
            "flush did not refresh final query statistics");

        std::vector<uint8_t> image(640*480*4),other(image.size());
        for(uint32_t count : {10000u,10u,1000u}) {
            settings.count=count;
            Require(SR_Configure(instance.renderer,&settings,1)==S_OK,"reset scene");
            Require(SR_SetQuery(instance.renderer,0.25f,0.75f)==S_OK,"query mapping");
            Require(SR_CopyFrame(instance.renderer,image.data(),640,480)==S_OK,"capture directly after configure/query");
            Require(SR_GetStats(instance.renderer,&stats)==S_OK && stats.results_match && stats.count==count,"capture snapshot");
            Require(SR_CopyFrame(instance.renderer,other.data(),640,480)==S_OK && image==other,"capture advanced paused simulation");
        }
        Require(SR_Resize(instance.renderer,0,0)==S_FALSE,"suspend");
        Require(SR_Render(instance.renderer)==S_FALSE,"render while suspended");
        Require(SR_Resize(instance.renderer,640,480)==S_OK,"restore");
        Require(SR_Render(instance.renderer)>=0,"render after restore");
        Require(SR_EnqueueConfigure(instance.renderer,&settings,1)==S_OK,"queue before destroy");
        // 생산자는 이미 join했다. 적용하지 않은 명령은 렌더러 파괴 시 취소된다.
        std::cout<<"PASS: ABI, queue producer access/FIFO/capacity, parallel queries, capture, suspend and shutdown\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
