#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <string>
#include <vector>
#include<iostream>
using namespace muduo;
using namespace std;

//获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

//构造函数：注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    //用户基本业务管理相关事件处理回调操作
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    //群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    //连接redis服务器
    if (_redis.connect())
    {
        //设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}
//处理登录业务  id posswprd
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string password = js["password"];

    User user = _userModel.query(id);
    if (user.getID() == id && user.getPassword() == password)
    {
        if (user.getState() == "online")
        {
            //该用户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 1;
            response["errmsg"] = "this account is using, inupt another!";
            conn->send(response.dump());
        }
        else
        {
            //登陆成功，记录用户连接信息
            /*
            加锁保护，多线程访问时C++STL容器时都是无法保证线程安全，如果有两个线程同时插入，位置相同时，就会覆盖掉
            创建lock_guard<mutex>对象，在创建对象调用构造函数时就会加锁，出作用域会自动调用析构函数解锁
            _connMutex：互斥锁对象
            */
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            //id用户登录成功后，向redis记录channel(id)
            _redis.subscribe(id);

            //登陆成功，更新用户状态信息 state：offline -> online
            //针对数据库的操作不用考虑线程安全的问题，mysql服务器会自动保证线程安全
            user.setState("online");
            _userModel.updatestate(user);

            //给用户返回一个登录成功的信息
            //每个线程的栈都是独有的，所以局部变量不用考虑线程安全的问题
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getID();
            response["name"] = user.getName();

            //查询该用户是否有离线消息
            vector<string> offlineMessage = _offlineMsgModel.query(id);
            if (!offlineMessage.empty())
            {
                response["offlinemsg"] = offlineMessage;
                //读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            //查询该用户的好友列表信息，并返回好友列表
            vector<User> userFriendlist = _friendModel.query(id);
            if (!userFriendlist.empty())
            {
                vector<string> vec;
                for (User &user : userFriendlist)
                {
                    json js;
                    js["id"] = user.getID();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec.push_back(js.dump());
                }
                response["friends"] = vec;
            }

            //查询用户的群组信息
            vector<Group> groupUserVec = _groupModel.queryGroups(id);
            if (!groupUserVec.empty())
            {
                //group:[{group:[xxx,xxx,xxx,xxx]}]
                vector<string> groupV;
                for (Group &group : groupUserVec)
                {
                    json groupjson;
                    groupjson["id"] = group.getID();
                    groupjson["groupname"] = group.getName();
                    groupjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getID();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    groupjson["users"] = userV;
                    groupV.push_back(groupjson.dump());
                }
                response["groups"] = groupV;
            }
            conn->send(response.dump());
        }
    }
    else
    {
        //该用户不存在，登陆失败
        if (user.getID() == -1)
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "the current user is not registered, please register first!";
            conn->send(response.dump());
        }
        //用户存在但是密码错误，登录失败
        else
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 3;
            response["errmsg"] = "id or password is invalid!";
            conn->send(response.dump());
        }
    }
}
// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    //用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    //更新用户状态信息
    User user(userid, "", "", "offline");
    _userModel.updatestate(user);
}
//处理注册消息 name password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string passsword = js["password"];

    User user;
    user.setName(name);
    user.setPassword(passsword);
    bool state = _userModel.insert(user);
    if (state)
    {
        //注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getID();
        conn->send(response.dump());
    }
    else
    {
        //注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}
//一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    //查询toid是否在本服务器上并且处于在线状态
    int toid = js["toid"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            //用户在线，转发消息  服务器主动推送消息给toid
            it->second->send(js.dump());
            return;
        }
    }

    //toid不在本服务器上时，查询toid是否在其他服务器上并处于在线状态
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    //toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}
//添加好友业务 msgid friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid, friendid);
}
//创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];
    //存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        //存储群组创建人信息
        _groupModel.addGroup(userid, group.getID(), "creator");
    }
}
//加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    //添加userid用户到groupid组中
    _groupModel.addGroup(userid, groupid, "normal");
}
//群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    //获取userid用户在groupid群组中的其他群成员
    vector<int> useridlist = _groupModel.queryGroupUsers(userid, groupid);
    //加锁：保证线程安全
    lock_guard<mutex> lock(_connMutex);
    for (int id : useridlist)
    {
        auto it = _userConnMap.find(id);
        //查询用户是否在本服务器上并且处于在线状态，如果在并且处于在线状态，发送消息
        if (it != _userConnMap.end())
        {
            it->second->send(js.dump());
        }
        //用户不在线，存储离线消息
        else
        {
            //如果不在本服务器上，但是处于在线状态，向用户所在的服务器通道发送消息
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                //用户不在线，存储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

//从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    //存储离线消息
    _offlineMsgModel.insert(userid, msg);
}

//处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    /*
    遍历_userConnMap在线用户通信连接表，查找conn所对应的用户id,获取该id;
    找到后从_userConnMap中删除，然后在数据库中将该id所对应的state置为offline。
    在遍历_userConnMap时，有可能用户登录成功，又向_userConnMap中插入，遍历中会出问题，
    所以要添加保证线程安全的措施。
    */
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                //获取异常退出用户的id
                user.setID(it->first);
                // 从_userConnMap中删除用户的连接信息
                _userConnMap.erase(it);
                break;
            }
        }
    }

    //客户端异常退出，相当于下线，在redis中取消订阅
    _redis.unsubscribe(user.getID());

    //更新数据库中用户的状态信息
    if (user.getID() != -1)
    {
        user.setState("offline");
        _userModel.updatestate(user);
    }
}
//服务器异常：业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}
//获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    //获取消息对应的处理器：此处不能直接用[]运算符来查询，如果不存在这个消息id，[]运算符重载函数直接会插入一个新的消息id
    auto it = _msgHandlerMap.find(msgid);
    //如果msgid没有对应的事件处理回调时，返回一个默认的处理器(任何操作也不做的处理器)，输出错误信息
    if (it == _msgHandlerMap.end())
    {
        //采用lambda表达式
        return [=](const TcpConnectionPtr &conn, json js, Timestamp) {
            LOG_ERROR << "msgid：" << msgid << " can not find handler!";
        };
    }
    //如果msgid有对应的事件处理回调时，返回msgid所对应的事件处理器
    else
    {
        return _msgHandlerMap[msgid];
    }
}