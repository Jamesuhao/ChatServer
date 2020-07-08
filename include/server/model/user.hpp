#ifndef USER_H
#define USER_H
//这个文件用于设置用户信息
#include <string>
using namespace std;
//User表的ORM对象关系映射类
class User
{
public:
    User(int id = -1, string name = "", string pwd = "", string state = "offline")
    {
        this->id = id;
        this->name = name;
        this->password = password;
        this->state = state;
    }
    void setID(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPassword(string password) { this->password = password; }
    void setState(string state) { this->state = state; }

    int getID() { return this->id; }
    string getName() { return this->name; }
    string getPassword() { return this->password; }
    string getState() { return this->state; }

protected:
    int id;
    string name;
    string password;
    string state;
};
#endif