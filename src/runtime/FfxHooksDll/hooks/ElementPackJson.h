#pragma once
// Jarvis-HOOK: startup-only reader. Manifest numbers have exact integer units;
// no parser, allocation or file access belongs in a battle callback.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace FfxHooks::ElementalDominion::Json {
enum class Type : std::uint8_t { Null, Boolean, Integer, String, Array, Object };
enum class Error : std::uint8_t { None, Syntax, Number, Unicode, DuplicateKey, Capacity };
struct Problem { Error code=Error::None;std::size_t offset=0; };
struct Limits {
    std::size_t bytes=1024*1024,nodes=32768,depth=16,stringBytes=1024,members=64,arrayItems=4096;
};
struct Value {
    Type type=Type::Null;
    bool boolean=false;
    std::int64_t integer=0;
    std::string text,key;
    std::vector<Value> children;
    const Value* Find(std::string_view name) const noexcept {
        if(type!=Type::Object)return nullptr;
        for(const auto& child:children)if(child.key==name)return &child;
        return nullptr;
    }
};
class Parser {
public:
    Parser(std::string_view input,const Limits& limits,Problem& problem) noexcept
        : input_(input),limits_(limits),problem_(problem) {}
    bool Run(Value& output){
        if(input_.size()>limits_.bytes)return Fail(Error::Capacity);
        Whitespace();if(!ReadValue(output,0))return false;
        Whitespace();return position_==input_.size()||Fail(Error::Syntax);
    }
private:
    bool Fail(Error code) noexcept {
        if(problem_.code==Error::None){problem_.code=code;problem_.offset=position_;}
        return false;
    }
    void Whitespace() noexcept {
        while(position_<input_.size()){
            const char ch=input_[position_];
            if(ch!=' '&&ch!='\t'&&ch!='\n'&&ch!='\r')break;
            ++position_;
        }
    }
    bool Take(char token) noexcept {
        if(position_==input_.size()||input_[position_]!=token)return false;
        ++position_;return true;
    }
    bool Literal(std::string_view token) noexcept {
        if(input_.substr(position_,token.size())!=token)return Fail(Error::Syntax);
        position_+=token.size();return true;
    }
    static bool Digit(char ch) noexcept {return ch>='0'&&ch<='9';}
    bool Integer(std::int64_t& output) noexcept {
        const bool negative=Take('-');
        if(position_==input_.size()||!Digit(input_[position_]))return Fail(Error::Number);
        if(input_[position_]=='0'&&position_+1<input_.size()&&Digit(input_[position_+1]))return Fail(Error::Number);
        const std::uint64_t maximum=negative?(std::uint64_t{1}<<63):
            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
        std::uint64_t magnitude=0;
        while(position_<input_.size()&&Digit(input_[position_])){
            const unsigned digit=static_cast<unsigned>(input_[position_]-'0');
            if(magnitude>(maximum-digit)/10)return Fail(Error::Number);
            magnitude=magnitude*10+digit;++position_;
        }
        if(position_<input_.size()&&(input_[position_]=='.'||input_[position_]=='e'||input_[position_]=='E'))return Fail(Error::Number);
        output=negative?(magnitude==(std::uint64_t{1}<<63)?
            (std::numeric_limits<std::int64_t>::min)():-static_cast<std::int64_t>(magnitude)):
            static_cast<std::int64_t>(magnitude);
        return true;
    }
    bool Hex16(std::uint32_t& value) noexcept {
        value=0;
        for(unsigned i=0;i<4;++i){
            if(position_==input_.size())return Fail(Error::Unicode);
            const char ch=input_[position_++];unsigned digit=0;
            if(ch>='0'&&ch<='9')digit=static_cast<unsigned>(ch-'0');
            else if(ch>='a'&&ch<='f')digit=static_cast<unsigned>(ch-'a'+10);
            else if(ch>='A'&&ch<='F')digit=static_cast<unsigned>(ch-'A'+10);
            else return Fail(Error::Unicode);
            value=(value<<4)|digit;
        }
        return true;
    }
    bool EscapedScalar(std::string& output){
        std::uint32_t scalar=0;if(!Hex16(scalar))return false;
        if(scalar>=0xD800&&scalar<=0xDBFF){
            if(!Take('\\')||!Take('u'))return Fail(Error::Unicode);
            std::uint32_t low=0;if(!Hex16(low))return false;
            if(low<0xDC00||low>0xDFFF)return Fail(Error::Unicode);
            scalar=0x10000+((scalar-0xD800)<<10)+(low-0xDC00);
        }else if(scalar>=0xDC00&&scalar<=0xDFFF)return Fail(Error::Unicode);
        if(scalar<0x80)output.push_back(static_cast<char>(scalar));
        else if(scalar<0x800){
            output.push_back(static_cast<char>(0xC0|(scalar>>6)));
            output.push_back(static_cast<char>(0x80|(scalar&63)));
        }else if(scalar<0x10000){
            output.push_back(static_cast<char>(0xE0|(scalar>>12)));
            output.push_back(static_cast<char>(0x80|((scalar>>6)&63)));
            output.push_back(static_cast<char>(0x80|(scalar&63)));
        }else{
            output.push_back(static_cast<char>(0xF0|(scalar>>18)));
            output.push_back(static_cast<char>(0x80|((scalar>>12)&63)));
            output.push_back(static_cast<char>(0x80|((scalar>>6)&63)));
            output.push_back(static_cast<char>(0x80|(scalar&63)));
        }
        return true;
    }
    bool LiteralScalar(std::string& output){
        const std::size_t start=position_;
        const auto first=static_cast<unsigned char>(input_[position_++]);
        unsigned continuation=0;std::uint32_t scalar=0,minimum=0;
        if(first>=0xC2&&first<=0xDF){continuation=1;scalar=first&31u;minimum=0x80;}
        else if(first>=0xE0&&first<=0xEF){continuation=2;scalar=first&15u;minimum=0x800;}
        else if(first>=0xF0&&first<=0xF4){continuation=3;scalar=first&7u;minimum=0x10000;}
        else return Fail(Error::Unicode);
        for(unsigned i=0;i<continuation;++i){
            if(position_==input_.size())return Fail(Error::Unicode);
            const auto ch=static_cast<unsigned char>(input_[position_++]);
            if((ch&0xC0u)!=0x80u)return Fail(Error::Unicode);
            scalar=(scalar<<6)|(ch&63u);
        }
        if(scalar<minimum||scalar>0x10FFFF||(scalar>=0xD800&&scalar<=0xDFFF))return Fail(Error::Unicode);
        output.append(input_.data()+start,position_-start);return true;
    }
    bool String(std::string& output){
        if(!Take('"'))return Fail(Error::Syntax);
        while(position_<input_.size()){
            const auto ch=static_cast<unsigned char>(input_[position_]);
            if(ch=='"'){++position_;return true;}
            if(ch<0x20)return Fail(Error::Syntax);
            if(ch=='\\'){
                ++position_;if(position_==input_.size())return Fail(Error::Syntax);
                const char escape=input_[position_++];
                switch(escape){
                case '"':case '\\':case '/':output.push_back(escape);break;
                case 'b':output.push_back('\b');break;
                case 'f':output.push_back('\f');break;
                case 'n':output.push_back('\n');break;
                case 'r':output.push_back('\r');break;
                case 't':output.push_back('\t');break;
                case 'u':if(!EscapedScalar(output))return false;break;
                default:return Fail(Error::Syntax);
                }
            }else if(ch<0x80){output.push_back(static_cast<char>(ch));++position_;}
            else if(!LiteralScalar(output))return false;
            if(output.size()>limits_.stringBytes)return Fail(Error::Capacity);
        }
        return Fail(Error::Syntax);
    }
    bool Container(Value& value,std::size_t depth,bool object){
        if(depth>=limits_.depth)return Fail(Error::Capacity);
        ++position_;value.type=object?Type::Object:Type::Array;
        const char end=object?'}':']';Whitespace();if(Take(end))return true;
        for(;;){
            if(value.children.size()>=(object?limits_.members:limits_.arrayItems))return Fail(Error::Capacity);
            Value child{};
            if(object){
                if(!String(child.key))return false;
                for(const auto& prior:value.children)if(prior.key==child.key)return Fail(Error::DuplicateKey);
                Whitespace();if(!Take(':'))return Fail(Error::Syntax);Whitespace();
            }
            if(!ReadValue(child,depth+1))return false;
            value.children.push_back(std::move(child));Whitespace();
            if(Take(end))return true;
            if(!Take(','))return Fail(Error::Syntax);
            Whitespace();
        }
    }
    bool ReadValue(Value& value,std::size_t depth){
        if(nodes_>=limits_.nodes)return Fail(Error::Capacity);
        ++nodes_;if(position_==input_.size())return Fail(Error::Syntax);
        switch(input_[position_]){
        case '{':return Container(value,depth,true);
        case '[':return Container(value,depth,false);
        case '"':value.type=Type::String;return String(value.text);
        case 't':value.type=Type::Boolean;value.boolean=true;return Literal("true");
        case 'f':value.type=Type::Boolean;return Literal("false");
        case 'n':return Literal("null");
        default:value.type=Type::Integer;return Integer(value.integer);
        }
    }
    std::string_view input_;
    const Limits& limits_;
    Problem& problem_;
    std::size_t position_=0,nodes_=0;
};
inline bool Parse(std::string_view input,Value& output,Problem& problem,const Limits& limits={}) noexcept {
    problem={};
    try{
        Value candidate{};Parser parser(input,limits,problem);
        if(!parser.Run(candidate))return false;
        output=std::move(candidate);return true;
    }catch(const std::bad_alloc&){problem.code=Error::Capacity;return false;}
     catch(const std::length_error&){problem.code=Error::Capacity;return false;}
}
} // namespace FfxHooks::ElementalDominion::Json
