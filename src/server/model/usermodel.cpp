#include "usermodel.hpp"
#include <iostream>
#include "db.h"
using namespace std;

//User表的增加方法
bool UserModel::insert(User &user)
{
    // 1.组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "insert into user(name,password,state) values('%s','%s','%s')",
            user.getName().c_str(), user.getPassword().c_str(), user.getState().c_str());
    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            /*
            mysql_insert_id()函数返回上一步INSERT操作产生的id。
            收集的当前这个数据库连接中发生的最后一次insert操作的结果，而AUTO_INCREMENT数据的唯一性如果上一查询没有产生AUTO_INCREMENT的ID，则mysql_insert_id()返回0。
            mysql_insert_id不会出错,不过条件是必须有一个字段为 auto_increment,而且类型为int。
            */
            //获取插入成功的用户数据所生成的主键id
            user.setID(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}
//根据用户id查询用户信息
User UserModel::query(int id)
{
    // 1.组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                user.setID(atoi(row[0]));
                user.setName(row[1]);
                user.setPassword(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                return user;
            }
        }
    }
    return User();
}

//更新用户的状态信息
bool UserModel::updatestate(User user)
{
    // 1. 组装SQL语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d",
            user.getState().c_str(), user.getID());
    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}
//重置用户的状态信息
void UserModel::resetState()
{
    // 1. 组装SQL语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}