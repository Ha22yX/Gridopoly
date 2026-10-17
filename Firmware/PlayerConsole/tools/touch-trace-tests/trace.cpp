#include "touch_trace.h"
#include "Arduino.h"
#include <cstdio>
#include <cstring>
#include <climits>
#include <algorithm>
struct Sink {
    int capacity = 64;
    bool shortWrite = false;
    std::vector<std::string> lines;
    int availableForWrite() { return capacity; }
    size_t write(const uint8_t *p, size_t n) {
        lines.emplace_back(reinterpret_cast<const char *>(p), n);
        return shortWrite ? n - 1 : n;
    }
};
int main() {
    unsigned passed = 0, failed = 0;
    auto check = [&](bool v, const char *s) { std::printf("[%s] %s\n",v?"PASS":"FAIL",s);(v?passed:failed)++; };
    check(sizeof(TouchTraceCore)+sizeof(TouchTraceOutput)<=800 && sizeof(TouchTraceSample)==64,
          "eight fixed 64-byte records plus output/core state fit under 800 bytes");
    {
        TouchTraceCore core; TouchTraceOutput output; Sink sink; TouchTraceSample s;
        core.record(s,0,0); output.tick(sink,core,0);
        check(core.snapshot(0).samples==0 && sink.lines.empty() && !core.token(0),"disabled recorder neither captures nor emits data");
        const auto token=core.start(100);s.atMs=100;s.flags=3;s.result=1;s.rawX=44;s.screenX=44;
        core.record(s,token,UINT32_MAX-9999);s.atMs=110;core.record(s,token,0);
        TouchTraceSample a,b;core.pop(a);core.pop(b);
        check(a.id==1 && b.id==2 && b.gapUs==10000 && b.rawX==44,"actual callback gaps and coherent samples survive microsecond wrap");
        s.atMs=111;s.flags=0;s.result=-1;core.record(s,token,1000);
        s.atMs=112;s.flags=2;s.result=0;core.record(s,token,2000);
        const auto st=core.snapshot(112);
        check(st.errors==1 && st.releases==1,"unknown/error and a genuine zero-touch release have separate counters");
        core.pop(a);core.pop(b);check(a.result==-1 && !(a.flags&1) && b.result==0 && !(b.flags&1),"missing raw coordinates never claim a stale raw point");
        for(unsigned i=0;i<20;++i){s.atMs=120+i;core.record(s,token,120000+i*1000);}
        check(core.snapshot(140).queued==8 && core.snapshot(140).dropped==12,"full diagnostic ring drops only records and counts every overflow");
        const auto next=core.start(150);core.record(s,token,150000);
        check(next!=token && core.snapshot(150).samples==0,"a prior callback cannot contaminate a newly started trace session");
        check(core.token(149)==0 && core.snapshot(150).active,"a clock captured before restart cannot expire the new trace session");
        check(core.token(30149)==next && core.token(30150)==0 && core.snapshot(30150).reason==2,"30-second automatic deadline stops capture even without output service");
        core.start(UINT32_MAX-1000);check(core.token(UINT32_MAX-1)!=0 && core.token(28999)==0,"trace deadline survives millis wrap");
    }
    {
        TouchTraceCore core;TouchTraceOutput out;Sink sink;auto token=core.start(0);
        TouchTraceSample s;s.atMs=1;s.result=1;s.flags=31;s.rawX=100;s.rawY=140;s.screenX=100;s.screenY=140;
        s.originX=78;s.originY=112;s.localX=22;s.localY=28;s.generation=7;s.points=s.drawn=1;s.drawAtMs=1;
        core.record(s,token,1000);
        for(uint32_t t=1;t<20;++t)out.tick(sink,core,t);
        check(sink.lines.size()==7 && sink.lines[0].find("GP_TOUCH READY ")==0 &&
              sink.lines[2].find("raw=100,140 scr=100,140")!=std::string::npos &&
              sink.lines[3].find("loc=22,28 org=78,112")!=std::string::npos,
              "READY precedes six sequence-linked coordinate/timing rows");
        core.stop(20);for(uint32_t t=20;t<30;++t)out.tick(sink,core,t);
        check(sink.lines.back().find("GP_TOUCH END ")==0 && sink.lines.back().find("why=1")!=std::string::npos,
              "manual stop drains records, emits final counters then explicit END");
        const auto count=sink.lines.size();out.tick(sink,core,10000);check(sink.lines.size()==count,"closed trace stays silent");
    }
    {
        TouchTraceCore core;TouchTraceOutput out;Sink sink;auto token=core.start(UINT32_MAX-500);
        TouchTraceSample s;s.atMs=UINT32_MAX-400;s.gapUs=s.readUs=s.generation=s.pendingAgeMs=s.drawUs=s.drawLagMs=s.fallbackUs=s.drawAtMs=UINT32_MAX;
        s.rawX=s.rawY=s.screenX=s.screenY=s.localX=s.localY=s.originX=s.originY=INT16_MIN;
        s.points=s.drawn=s.fallbackCount=UINT16_MAX;s.result=-1;s.flags=255;s.recognizerReady=1;
        core.record(s,token,100);
        for(uint32_t dt=0;dt<1200;++dt)out.tick(sink,core,UINT32_MAX-400+dt);
        bool bounded=true;for(auto &l:sink.lines)bounded &= l.size()<=63 && l.back()=='\n';
        check(bounded && sink.lines.size()>=10,"maximum numeric fields remain whole lines within 63 bytes");
        check(sink.lines[1].find("r=-1")!=std::string::npos,"negative read status is retained without unsigned conversion");
    }
    {
        TouchTraceCore core;TouchTraceOutput out;Sink sink;sink.capacity=0;auto token=core.start(0);
        TouchTraceSample s;s.atMs=1;s.flags=2;core.record(s,token,1000);
        for(uint32_t t=1;t<100;++t)out.tick(sink,core,t);
        check(sink.lines.empty(),"full USB queue triggers no write or partial-line retry");
        sink.capacity=20;for(uint32_t t=100;t<200;++t)out.tick(sink,core,t);
        check(sink.lines.empty(),"short available capacity cannot start a line");
        sink.capacity=64;for(uint32_t t=200;t<210;++t)out.tick(sink,core,t);
        check(!sink.lines.empty() && sink.lines.front().find("READY")!=std::string::npos,"USB recovery begins with READY rather than a line remainder");
        sink.capacity=0;for(uint32_t t=30000;t<33000;t+=100)out.tick(sink,core,t);
        const auto count=sink.lines.size();sink.capacity=64;out.tick(sink,core,34000);
        check(out.closed() && sink.lines.size()==count,"absent USB cannot extend capture or logging indefinitely");
    }
    {
        TouchTraceCore core;TouchTraceOutput out;Sink sink;sink.shortWrite=true;core.start(0);
        out.tick(sink,core,0);out.tick(sink,core,1);
        check(sink.lines.size()==1,"unexpected short transport write is counted and never replayed as a partial line");
    }
    host::mainThread=std::this_thread::get_id();
    touchTraceService(0);check(host::lines.empty() && host::locks==0 && !touchTraceEnabled() && !touchTraceToken(0),
        "actual disabled adapter performs no lock, capture or USB I/O");
    touchTraceStart(100);std::atomic<bool> done{false};
    std::thread producer([&]{for(uint32_t i=0;i<200;++i){TouchTraceSample s;s.atMs=100+i;s.flags=3;s.result=1;
        s.rawX=static_cast<int16_t>(i);s.screenX=static_cast<int16_t>(i+20);
        const auto token=touchTraceToken(s.atMs);touchTraceRecord(s,token,100000+i*1000);}done=true;});
    while(!done)touchTraceService(100);
    producer.join();for(uint32_t t=300;t<1000;++t)touchTraceService(t);
    bool coherent=true;unsigned pairs=0;
    for(auto &l:host::lines){unsigned id=0,flags=0;int rx=0,ry=0,sx=0,sy=0;
        if(std::sscanf(l.c_str(),"GP_T id=%u raw=%d,%d scr=%d,%d f=%u",&id,&rx,&ry,&sx,&sy,&flags)==6){++pairs;coherent &= sx==rx+20;}}
    check(coherent && pairs>0 && host::violations==0,"actual concurrent adapter preserves record ownership and never performs USB I/O in a lock or producer");
    check(touchTraceToken(30100)==0,"actual producer path independently enforces the deadline");
    for(uint32_t t=30100;t<30200;++t)touchTraceService(t);
    check(!host::lines.empty() && host::lines.back().find("GP_TOUCH END")!=std::string::npos,
        "actual main-loop service emits final END after automatic expiry");
    const auto locks=host::locks.load();touchTraceService(31000);check(host::locks==locks,"completed adapter returns to zero-lock idle");
    std::printf("STATE_BYTES core=%zu output=%zu sample=%zu\n",sizeof(TouchTraceCore),sizeof(TouchTraceOutput),sizeof(TouchTraceSample));
    std::printf("RESULT passed=%u failed=%u\n",passed,failed);return failed?1:0;
}
