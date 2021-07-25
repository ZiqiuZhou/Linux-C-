//系统头文件放上边
#include <cstdio>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <vector>
#include <iostream>

//自定义头文件放下边,因为g++中用了-I参数，所以这里用<>也可以
#include "ngx_func.h"     //函数声明
#include "ngx_c_conf.h"   //和配置文件处理相关的类,名字带c_表示和类有关
#include "ngx_string.h"

constexpr std::size_t bufSize = 501;

//静态成员赋值
CConfig *CConfig::m_instance = nullptr;

//析构函数
CConfig::~CConfig() {
    std::vector<CConfItem*>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos) {
        delete (*pos);
        (*pos) = nullptr;
    }
    m_ConfigItemList.clear();
}

FileHandle CConfig::FileReader::openFile(const char*& pconfName) {
    std::FILE* file = fopen(pconfName, "r");
    posixAssert(file != nullptr);
    return FileHandle(file, FileDeleter());
}

void CConfig::FileReader::closeFile(FileHandle& file) {
    std::FILE* rawfile = file.release();

    // Close the file, throw an exception on error.
    int result = fclose(rawfile);
    posixAssert(result == 0);
}

bool CConfig::FileReader::readFile(char* linebuf, std::vector<CConfItem*>& _m_ConfigItemList) {
    //检查文件是否结束 ，没有结束则条件成立

    while (!std::feof(file_.get())) {
        if(fgets(linebuf, 500, file_.get()) == nullptr) { //从文件中读数据，每次读一行，一行最多不要超过500个字符
            continue;
        } else if (linebuf[0] == 0) {
            continue;
        } else if (linebuf[0] == ';' || linebuf[0] == ' ' || linebuf[0] == '#' || linebuf[0] == '\t' || linebuf[0] == '\n') { // 注释行
            continue;
        }

        lblprocstring:
        //后边若有换行，回车，空格等都截取掉
        if(strlen(linebuf) > 0)
        {
            int length = strlen(linebuf) - 1;
            if(linebuf[length] == 10 || linebuf[length] == 13 || linebuf[length] == 32)
            {
                linebuf[length] = 0;
                goto lblprocstring;
            }
        }
        if(linebuf[0] == 0) {
            continue;
        }
        if(linebuf[0]=='[') { //[开头的也不处理
            continue;
        }
        std::string str = linebuf;
        auto pos = str.find('=');
        if (pos < str.size() - 1 && pos!=std::string::npos) {
            CConfItem* confItem = new CConfItem();

            std::string itemName(pos, '\0');
            std::copy(str.begin(), str.begin() + pos, itemName.begin());

            std::string itemContent(str.size() - pos - 1, '\0');
            std::copy(str.begin() + pos + 1, str.end(), itemContent.begin());

            // 截取字符串空格, included in ngx_string.cxx
            Trim(itemName);
            Trim(itemContent);

            confItem->ItemName = std::move(itemName);
            confItem->ItemContent = std::move(itemContent);

            _m_ConfigItemList.emplace_back(confItem);
        }
        str.clear();
    }
    return true;
}

//装载配置文件
bool CConfig::Load(const char *pconfName) {
    // open file
    auto file_reader = FileReader::open(pconfName);

    //每一行配置文件读出来都放这里
    char linebuf[bufSize]; //每行配置都不要太长，保持<500字符内，防止出现问题

    //走到这里，文件打开成功
    std::vector<CConfItem*> _m_ConfigItemList;
    bool result = file_reader->readFile(linebuf, _m_ConfigItemList);
    posixAssert(result);

    m_ConfigItemList = std::move(_m_ConfigItemList);

    return true;
}

//根据ItemName获取配置信息字符串，不修改不用互斥
const char *CConfig::GetString(const char *p_itemname)
{
    std::vector<CConfItem*>::iterator pos;
    for(pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        if(strcasecmp( (*pos)->ItemName.c_str(),p_itemname) == 0)
            return (*pos)->ItemContent.c_str();
    }//end for
    return nullptr;
}
//根据ItemName获取数字类型配置信息，不修改不用互斥
int CConfig::GetIntDefault(const char *p_itemname,const int def)
{
    std::vector<CConfItem*>::iterator pos;
    for(pos = m_ConfigItemList.begin(); pos !=m_ConfigItemList.end(); ++pos)
    {
        if(strcasecmp( (*pos)->ItemName.c_str(),p_itemname) == 0)
            return std::stoi((*pos)->ItemContent);
    }//end for
    return def;
}