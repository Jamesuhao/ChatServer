#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <functional>
#include <string>
#include <iostream>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;
//初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    //注册连接、断开回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    //注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    //设置数量
    _server.setThreadNum(4);
}

//启动服务
void ChatServer::start()
{
    _server.start();
}
//上报连接相关信息的回调函数：连接创建、连接断开
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    //客户端断开连接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}
//上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    //从缓冲区获取数据返回buf
    string buf = buffer->retrieveAllAsString();
    //数据的反序列化
    json js = json::parse(buf);
    /*
    完全解耦网络模块的代码和业务模块的代码：
    业务处理模块中，在聊天服务器业务类构造函数的实现中将msgid所对应的业务处理器插入到业务处理map表中，提供一个获取业务处理器handler的方法，
    网络模块代码中直接通过聊天服务器业务类的类对象调用其获取消息对应处理器的方法，通过msgid获取相对应的事件处理器，回调事件处理器就可。
    好处：业务处理模块无论如何修改，网络模块都不用修改。
    网络模块只是通过msgid在map表中获取业务处理模块所插入的事件处理器，然后回调事件处理器而已；
    而msgid所对应什么样的事件处理器则是由业务处理模块进行处理了。
    */
    //通过js["msgid"] 获取 =》业务处理器handler =》 调用事件处理器hanler传入conn js time这三个参数即可
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>()); //js["msgid"].get<int>()：将获取到的数字转换为int
    //回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}