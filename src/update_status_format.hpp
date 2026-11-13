#pragma once
#include <string>
#include <algorithm>
namespace qolupdatemenu {
inline bool validDate(const std::wstring& s){
    if(s.size()!=16||s[4]!=L'-'||s[7]!=L'-'||s[10]!=L' '||s[13]!=L':')return false;
    for(unsigned i=0;i<16;++i)if(i!=4&&i!=7&&i!=10&&i!=13&&(s[i]<L'0'||s[i]>L'9'))return false;
    const auto number=[&](unsigned i){return (s[i]-L'0')*10+s[i+1]-L'0';};
    return number(5)>=1&&number(5)<=12&&number(8)>=1&&number(8)<=31&&number(11)<=23&&number(14)<=59;
}
struct Receipt {std::string state="idle",sha,build;std::wstring updated;bool valid=false;};
inline bool knownState(const std::string& s){
    for(const char* value:{"idle","checking","downloading","staged","applying","up_to_date","failed","updated","no_release","cancelled"})if(s==value)return true;
    return false;
}
inline Receipt parse(const std::string& source){
    Receipt result;if(source.size()>4096)return result;
    for(unsigned char c:source)if(c==0||c>127)return result;
    std::string section;std::size_t begin=0;bool stateSeen=false;
    while(begin<source.size()){
        const auto end=source.find('\n',begin);auto line=source.substr(begin,end==std::string::npos?end:end-begin);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(!line.empty()&&line.front()=='[')section=line;
        const auto split=line.find('=');
        if(split!=std::string::npos){const auto key=line.substr(0,split),value=line.substr(split+1);
            if(section=="[Update]"&&key=="State"){result.state=value;stateSeen=true;}
            if(section=="[Installed]"){
                if(key=="Sha")result.sha=value;
                if(key=="Build")result.build=value;
                if(key=="UpdatedLocal")result.updated.assign(value.begin(),value.end());
            }
        }
        if(end==std::string::npos)break;
        begin=end+1;
    }
    bool sha=result.sha.empty()||result.sha.size()==40;
    for(char c:result.sha)sha=sha&&((c>='0'&&c<='9')||(c>='a'&&c<='f'));
    result.valid=stateSeen&&knownState(result.state)&&sha&&(result.updated.empty()||validDate(result.updated));return result;
}
inline std::wstring format(const std::wstring& version,const std::wstring& build,const std::string& state,const std::wstring& installed){
    std::wstring status;
    if(state=="checking")status=L"Checking...";
    else if(state=="downloading")status=L"Downloading/staging";
    else if(state=="staged")status=L"Staged - exit to apply";
    else if(state=="applying")status=L"Applying update";
    else if(state=="failed")status=L"Update failed - retry";
    else if(state=="no_release")status=L"No published update";
    else if(state=="cancelled")status=L"Update cancelled";
    else if(state=="up_to_date")status=L"Up to date";
    else status=L"Updates off";
    if(validDate(installed)){
        if(state=="up_to_date"){auto shortDate=installed.substr(5);shortDate[2]=L'/';status+=L"; Updated "+shortDate;}
        else if(state=="updated"||state=="idle"||state.empty())status=L"Updated "+installed;
    }
    std::wstring base=L"QoL "+version+L" "+build;
    if(base.size()+3+status.size()>59)base=L"QoL "+build;
    auto result=base+L" | "+status;if(result.size()>59)result.resize(59);return result;
}
inline std::string pendingState(std::string state,bool request,bool requestFailed,unsigned requestAge,unsigned statusAge){
    if(requestFailed||(request&&requestAge>120))return "failed";
    if(request)return "checking";
    if((state=="checking"||state=="downloading"||state=="applying")&&statusAge>300)return "failed";
    return state;
}
}
