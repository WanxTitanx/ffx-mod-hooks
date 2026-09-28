// Private to MonsterRewardsRuntime. File I/O belongs to worker preparation or
// an explicit F8 Save, never a reward callback, frame pump, or loader detach.
RateTable rates{};
bool ratesValid=true,hadRatesFile=false;
std::string ratesBytes;
std::filesystem::path ratesPath;
std::atomic<unsigned> temporarySequence{0};
std::filesystem::path StorePath(){
    const std::string ini=Config::GetLoadedPath();
    if(ini.empty()||ini[0]=='(')return {};
    return std::filesystem::absolute(std::filesystem::path(ini)).parent_path()/L"monster-rewards-v1.tsv";
}
bool ReadStored(std::string& bytes,bool& exists){
    bytes.clear();exists=false;if(ratesPath.empty())return false;
    const DWORD attributes=GetFileAttributesW(ratesPath.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
    exists=true;std::ifstream file(ratesPath,std::ios::binary|std::ios::ate);if(!file)return false;
    const auto length=file.tellg();if(length<0||length>MaximumSettingsBytes)return false;
    bytes.resize(static_cast<std::size_t>(length));file.seekg(0);
    return bytes.empty()||static_cast<bool>(file.read(bytes.data(),static_cast<std::streamsize>(bytes.size())));
}
void LoadRates(){
    ratesPath=StorePath();rates={};ratesValid=ReadStored(ratesBytes,hadRatesFile)&&(!hadRatesFile||ParseSettings(ratesBytes,rates));
    if(ratesValid)for(unsigned i=0;i<rates.size();++i)if(rates[i].ap!=1||rates[i].gil!=1)List(i);
}
bool StoreRates(const RateTable& candidate){
    if(!ratesValid||ratesPath.empty()||StorePath()!=ratesPath)return false;
    std::string serialized,current;bool exists=false;
    if(!SerializeSettings(candidate,serialized)||!ReadStored(current,exists)||exists!=hadRatesFile||current!=ratesBytes)return false;
    const auto filename=L".monster-rewards-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(temporarySequence.fetch_add(1))+L".tmp";
    const auto temporary=ratesPath.parent_path()/filename;
    HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE)return false;
    struct OwnedTemporary {const std::filesystem::path& path;bool owned=true;~OwnedTemporary(){if(owned)DeleteFileW(path.c_str());}} cleanup{temporary};
    DWORD written=0;
    const bool writtenAll=WriteFile(output,serialized.data(),static_cast<DWORD>(serialized.size()),&written,nullptr)&&written==serialized.size()&&FlushFileBuffers(output);
    CloseHandle(output);
    bool replaced=false;
    if(writtenAll&&ReadStored(current,exists)&&exists==hadRatesFile&&current==ratesBytes)
        replaced=MoveFileExW(temporary.c_str(),ratesPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!replaced)return false;
    cleanup.owned=false;
    if(!ReadStored(current,exists)||!exists||current!=serialized){ratesValid=false;admission=false;return false;}
    rates=candidate;ratesBytes=std::move(serialized);hadRatesFile=true;return true;
}
