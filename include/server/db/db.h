#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>
using namespace std;

//数据库操作类
class MySQL
{
public:
    //初始化数据库连接：并不是真正的初始化，只是为对象开辟存储空间
    MySQL();
    //释放数据库连接资源:即释放为对象所开辟的存储空间
    ~MySQL();
    //连接数据库
    bool connect();
    //更新操作
    bool update(string sql);
    //查询操作
    MYSQL_RES *query(string sql);
    //获取当前连接
    MYSQL *getConnection();

private:
    MYSQL *_conn;
};

#endif