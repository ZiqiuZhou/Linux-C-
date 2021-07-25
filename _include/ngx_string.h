
#include <cstdio>
#include <string>

//一些和字符串处理相关的函数，准备放这里

//截取字符串首尾部空格
void Trim(std::string& str)
{
    if( !str.empty() )
    {
        str.erase(0,str.find_first_not_of(" "));
        str.erase(str.find_last_not_of(" ") + 1);
    }
	return;   
}