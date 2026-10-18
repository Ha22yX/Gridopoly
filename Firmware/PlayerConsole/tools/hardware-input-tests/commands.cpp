#include "hardware_input.cpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <deque>
#include <string>
#include <vector>
struct FakeSerial{std::deque<char>rx;std::vector<std::string>lines;int capacity=256;int available(){return static_cast<int>(rx.size());}int read(){char c=rx.front();rx.pop_front();return c;}int availableForWrite(){return capacity;}size_t write(const uint8_t*p,size_t n){lines.emplace_back(reinterpret_cast<const char*>(p),n);return n;}void printf(const char*format,...){char line[512];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);if(n>0)lines.emplace_back(line,static_cast<size_t>(n));}}Serial;
unsigned batteryStarts=0,batteryStops=0;uint32_t batteryStartAt=0;bool batteryAvailable=true;
bool batteryHardwareTraceStart(uint32_t ms){++batteryStarts;batteryStartAt=ms;return batteryAvailable;}void batteryHardwareTraceStop(){++batteryStops;}
void touchTraceStart(uint32_t){}void touchTraceStop(uint32_t){}
#pragma warning(push)
// Host fixture combines two production translation units to inspect private counters.
#pragma warning(disable:4459)
#include "extracted-command-path.h"
#pragma warning(pop)
unsigned checks=0,failed=0;void check(bool ok,const char*label){++checks;failed+=!ok;std::printf("[%s] %s\n",ok?"PASS":"FAIL",label);}
void command(const char*s,uint32_t now=1000){for(const char*p=s;*p;++p)Serial.rx.push_back(*p);do{serviceInputTrace(now);}while(!Serial.rx.empty());}
int main(){fakeGpio=(1U<<6U)|(1U<<5U);hardwareInputBegin();
command("BATTERY TRACE ON\r\n");check(batteryStarts==1&&batteryStartAt==1000,"actual parser accepts CRLF BATTERY TRACE ON at current uptime");command("BATTERY TRACE OFF\n");check(batteryStops==1,"actual parser routes BATTERY TRACE OFF only to diagnostic stop");
command("BATTERY TRA");check(batteryStarts==1,"partial command cannot start diagnostics");command("CE ON\n",2000);check(batteryStarts==2&&batteryStartAt==2000,"split command completes once with current timestamp");command("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXBATTERY TRACE ON\n");check(batteryStarts==2,"overflowed line cannot execute a suffix command");batteryAvailable=false;command("BATTERY TRACE ON\n");check(Serial.lines.back().find("enabled=0")!=std::string::npos,"disabled battery service reports rejected trace activation");
inputStatistics.samples=inputStatistics.lastSampleGapUs=inputStatistics.maxSampleGapUs=UINT32_MAX;inputStatistics.decodedSteps=inputStatistics.positiveSteps=inputStatistics.negativeSteps=UINT32_MAX;inputStatistics.invalidTransitions=inputStatistics.inputQueueDropped=diagnosticDropped=inputTraceUsbDropped=UINT32_MAX;
hardwareInputTestEnqueue({InputKind::Rotate,1,500});const auto before=hardwareInputStatistics();command("INPUT STATUS\n",UINT32_MAX);const auto after=hardwareInputStatistics();const auto statusLine=Serial.lines.back();check(statusLine.size()<256&&statusLine.find("pos=4294967295 neg=4294967295")!=std::string::npos&&statusLine.find("queue_drop=4294967295")!=std::string::npos,"maximum-width cumulative signed counters fit one bounded STATUS line");check(!inputTraceEnabled&&after.samples==before.samples&&after.decodedSteps==before.decodedSteps&&after.queueEntries==before.queueEntries,"INPUT STATUS neither enables trace nor clears cumulative statistics or pending input");
command("INPUT TRACE ON\n",2000);check(inputTraceEnabled&&inputTraceDeadlineMs==92000&&hardwareInputStatistics().inputQueueDropped==UINT32_MAX,"TRACE ON retains boot evidence while opening exactly 90 seconds");command("INPUT TRACE OFF\n",2001);check(!inputTraceEnabled&&hardwareInputStatistics().decodedSteps==UINT32_MAX,"TRACE OFF preserves boot counters");command("INPUT TRACE ON\n",3000);serviceInputTrace(93000);check(!inputTraceEnabled,"raw trace ends automatically at its deadline");
Serial.capacity=0;auto lineCount=Serial.lines.size();command("INPUT STATUS\n",94000);check(Serial.lines.size()==lineCount&&inputTraceUsbDropped==UINT32_MAX,"unwritable USB drops STATUS and its loss counter saturates");
Serial.capacity=256;inputTraceEnabled=true;inputTraceDeadlineMs=200000;diagnosticHead=0;diagnosticTail=32;diagnosticCount=32;
for(unsigned i=0;i<32;++i)diagnostics[i]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,3,3,-1,32};
lineCount=Serial.lines.size();serviceInputTrace(95000);check(diagnosticCount==16&&Serial.lines.size()==lineCount+16,"raw trace drains at most 16 records per loop");bool fields=true;for(size_t i=lineCount;i<Serial.lines.size();++i){const auto&line=Serial.lines[i];fields&=line.size()<256&&line.find("gap_us=4294967295 max_gap_us=4294967295 queue_drop=4294967295 tx_drop=4294967295")!=std::string::npos;}check(fields,"raw trace includes timing and real input-drop counters without truncation");
Serial.capacity=0;lineCount=Serial.lines.size();serviceInputTrace(95001);check(diagnosticCount==0&&Serial.lines.size()==lineCount,"raw diagnostics are discarded under USB backpressure without retry backlog");
std::printf("STATUS_MAX_BYTES=%zu\nRESULT checks=%u failed=%u; exact extracted production command path, no device\n",statusLine.size(),checks,failed);return failed?1:0;}
