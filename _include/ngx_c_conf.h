
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>
#include <memory>      // for unique_ptr<>

#include "ngx_global.h"

struct FileDeleter
{
    void operator ()(std::FILE* file) const noexcept
    {
        // This function is called by the destructor of `std::unique_ptr<>`, so we ignore any errors returned by
        // `std::fclose()`.
        std::fclose(file);
    }
};

using FileHandle = std::unique_ptr<std::FILE, FileDeleter>;

class CConfig {
// 单例设计模式
private:
    CConfig() {}
public:
    ~CConfig();
private:
    static CConfig *m_instance;

public:
    static CConfig* GetInstance() {
        if (m_instance == nullptr) {
            //锁（本来该加锁，但在主线程中率先执行了GetInstance函数，这样就不存在多线程调用该函数导致的需要互斥的问题，因此这里就没有实际加锁）
            if (m_instance == nullptr) {
                m_instance = new CConfig();
                static CGarhuishou cl;
            }
            //放锁
        }
        return m_instance;
    }

    //类中套类，用于释放对象
    struct CGarhuishou {
    public:
        ~CGarhuishou() {
            if (m_instance != nullptr) {
                delete m_instance;
                m_instance = nullptr;
            }
        }
    };

public:
    bool Load(const char *pconfName); // 装载配置文件
    const char* GetString(const char* p_itemname);
    int GetIntDefault(const char* p_itemname, const int def);

    std::vector<CConfItem*> m_ConfigItemList; //存储配置信息的列表
};

// 类中套类，用来读取文件
// file handler to open and read config file

class FileReader {
private:
    // Define a "file handle" as a `unique_ptr<>` with a custom deleter: lifetime management is done as usual, but when
    // the resource is to be freed, `unique_ptr<>` will pass the file pointer to an instance of our deleter rather than
    // using a `delete` expression.
    FileHandle file_;

private:
    FileHandle openFile(const char*& pconfName);
    void closeFile(FileHandle& file);

    FileReader(const char*& pconfName)
            : file_(std::move(openFile(pconfName))) {}

public:
    static FileReader* open(const char*& pconfName) {
        static FileReader instance(pconfName);
        return &instance;
    }

    bool readFile(char* buffer, std::vector<CConfItem*>& _m_ConfigItemList);
    ~FileReader() {
        closeFile(file_);
    }
};
#endif
