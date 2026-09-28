// Jarvis-HOOK: bounded, transactional parsing of an untrusted startup manifest.
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>

#if __has_include("../hooks/ElementPackJson.h")
#include "../hooks/ElementPackJson.h"
namespace {
namespace J=FfxHooks::ElementalDominion::Json;
unsigned checks=0,failures=0;
void Check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
using J::Parse;
}
int main(){
    J::Value value{};J::Problem problem{};
    Check(Parse(R"({"version":1,"enabled":false,"names":["core.fire","custom.wind"],"metadata":null})",value,problem),"complete manifest-shaped JSON parses");
    Check(value.type==J::Type::Object&&value.Find("version")&&value.Find("version")->integer==1,"typed object member lookup");
    Check(value.Find("enabled")&&value.Find("enabled")->type==J::Type::Boolean&&!value.Find("enabled")->boolean,"false does not become an absent or enabled option");
    Check(value.Find("names")&&value.Find("names")->children.size()==2&&value.Find("names")->children[1].text=="custom.wind","arrays preserve stable-key ordering");
    Check(!value.Find("absent")&&value.Find("metadata")->type==J::Type::Null,"missing and explicit null remain distinct");
    for(const auto* valid:{"0","-0","9223372036854775807","-9223372036854775808"})
        Check(Parse(valid,value,problem)&&value.type==J::Type::Integer,"signed integer boundaries are admitted exactly");
    Check(value.integer==(std::numeric_limits<std::int64_t>::min)(),"negative integer endpoint has no overflow");
    const char* invalid[]={""," ","{","[","tru","False","NaN","Infinity","+1","01","-01","--1","-",
        "1.5","1e3","9223372036854775808","-9223372036854775809","18446744073709551616",
        "{} []","[1,]","{\"x\":1,}","{\"x\" 1}","{x:1}","[1 2]","/*comment*/{}",
        "\"unterminated","\"raw\nline\"","\"\\q\"","\"\\u12\"","\"\\uZZZZ\"",
        "\"\\uD800\"","\"\\uDC00\"","\"\\uD800\\u0041\""};
    for(const char* text:invalid){
        J::Value before{};before.type=J::Type::String;before.text="preserved";
        Check(!Parse(text,before,problem)&&problem.code!=J::Error::None,"malformed or non-integer manifest JSON is rejected");
        Check(before.type==J::Type::String&&before.text=="preserved","parse failure cannot partially replace the active document");
    }
    for(const char* text:{R"({"x":1,"x":2})",R"({"x":1,"\u0078":2})",R"({"outer":{"x":1,"x":2}})"})
        Check(!Parse(text,value,problem)&&problem.code==J::Error::DuplicateKey,"duplicate keys are rejected after escape decoding");
    Check(Parse(R"({"label":"Fire \"A\" \\ \u0041","symbol":"\uD83D\uDD25"})",value,problem),"valid escapes and a surrogate pair parse");
    Check(value.Find("label")->text=="Fire \"A\" \\ A"&&value.Find("symbol")->text=="\xF0\x9F\x94\xA5","UTF-8 is encoded without losing scalar identity");
    Check(Parse("\"\xC3\xA9\"",value,problem)&&value.text=="\xC3\xA9","valid literal UTF-8 is preserved");
    for(const std::string& bad:{std::string("\"\xC0\xAF\""),std::string("\"\xED\xA0\x80\""),
        std::string("\"\xF4\x90\x80\x80\""),std::string("\"\xE2\x82\""),std::string("\"\x80\"")})
        Check(!Parse(bad,value,problem)&&problem.code==J::Error::Unicode,"invalid UTF-8 does not enter a binding or display label");
    J::Limits limit{};limit.bytes=3;
    Check(!Parse("true",value,problem,limit)&&problem.code==J::Error::Capacity,"input bytes are bounded before tokenization");
    limit={};limit.depth=3;
    Check(Parse("[[[0]]]",value,problem,limit)&&!Parse("[[[[0]]]]",value,problem,limit),"nested containers obey an exact depth bound");
    limit={};limit.nodes=3;
    Check(Parse("[1,2]",value,problem,limit)&&!Parse("[1,2,3]",value,problem,limit),"node budget includes the root");
    limit={};limit.stringBytes=3;
    Check(Parse("\"abc\"",value,problem,limit)&&!Parse("\"abcd\"",value,problem,limit),"decoded strings have a separate byte budget");
    Check(!Parse(R"({"long":1})",value,problem,limit),"object keys use the same bounded string decoder");
    limit={};limit.members=2;
    Check(Parse(R"({"a":1,"b":2})",value,problem,limit)&&!Parse(R"({"a":1,"b":2,"c":3})",value,problem,limit),"object member fanout is bounded");
    limit={};limit.arrayItems=2;
    Check(Parse("[true,false]",value,problem,limit)&&!Parse("[true,false,null]",value,problem,limit),"array fanout is bounded independently");
    Check(Parse("\r\n\t {\"a\": [1, false, null]} \n",value,problem)&&problem.code==J::Error::None,"a successful retry clears the previous diagnostic");
    std::printf("ELEMENT_PACK_JSON_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production ElementPackJson.h is missing");return 1;}
#endif
