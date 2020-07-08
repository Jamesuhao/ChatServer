#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

//记录当前系统登录的用户信息
User g_currentUser;
//记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登录成功用户的基本信息
void showCurrentUserData();

//接收线程
void readTaskHandler(int clientfd);
//获取系统事件(聊天信息需要添加时间信息)
string getCurrentTime();
//控制主菜单页面程序
bool isMainMenuRunning = false;
//主聊天页面程序
void mainMenu(int);

//聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char *argv[])
{
    //检测输入参数个数，启动客户端时，需要输入IP地址和port端口号
    if (argc < 3)
    {
        cerr << "command invalid! example：./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    //解析通过命令行参数传递的IP地址和port端口号
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    //创建client需要连接的server信息和IP地址+port端口号，即就是创建用于通信的套接字
    /*
    int socket(int domain,int type,int protocol);
    domain：地址域(IPV4/IPV6)
    type：套接字类型(流式套接字SOCK_STREAM/数据报套接字SOCK_DGRAM)
    protocol：使用的协议，0(表示套接字类型下默认的协议)(tcp协议：IPPROTO_TCP/udp：协议IPPROTO_UDP)
    返回值：成功返回套接字的操作句柄，失败返回-1
    AF_INET：IPV4协议
    SOCK_STREAM：流式套接字
    IPPROTO_TCP：TCP协议
    */
    int clientfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    //填写client需要连接的server信息、ip地址和port端口号，即填充需要绑定的地址信息结构
    /*
    uint16_t htons(uint16_t)：将2个字节的主机字节序数据转换为网络字节序数据
    uint32_t inet_addr(char* ip)：将点分十进制字符串IP地址转换成网络字节序的IP地址
    */
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    /*
    int connect(int sockfd,int sockaddr* addr,socklen_t len);
    sockfd：套接字操作句柄
    addr：服务端地址信息结构(sockaddr_in)
    len：地址信息长度
    返回值：成功返回0，失败返回-1
    */
    //client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        //显示首页菜单：登录、注册、退出功能
        cout << "=====================" << endl;
        cout << "      1. login       " << endl;
        cout << "      2. register    " << endl;
        cout << "      3. quit        " << endl;
        cout << "=====================" << endl;
        cout << "choice：";
        int choice = 0;
        cin >> choice;
        cin.get(); //读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: //login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid：";
            cin >> id;
            cin.get(); //读掉缓冲区残留的回车
            cout << "userpassword：";
            cin.getline(pwd, 50);

            //组织请求json串
            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            /*
            ssize_t send(int sockfd,char* data,int len,int flag);
            sockfd：套接字操作句柄
            data：要发送数据的首地址
            len：要发生数据的长度
            flag：选项参数，默认为0(表示当前操作是阻塞操作)，MSG_DONTWAIT(表示当前操作是非阻塞操作，当socket发送缓冲区满了，立即报错返回)
            返回值：发送成功返回实际发生数据长度，失败返回-1；若连接断开触发异常，进程退出
            */
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (-1 == len) //登陆请求失败
            {
                cerr << "send login message error" << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(clientfd, buffer, 1024, 0);
                if (-1 == len) //接收登录响应失败
                {
                    cerr << "recv login response error" << endl;
                }
                else //接收登录响应成功
                {
                    json responsejs = json::parse(buffer);
                    if (0 != responsejs["errno"].get<int>()) //登陆失败
                    {
                        cerr << responsejs["errmsg"] << endl;
                    }
                    else //登陆成功
                    {
                        //记录当前用户的id和name
                        g_currentUser.setID(responsejs["id"].get<int>());
                        g_currentUser.setName(responsejs["name"]);

                        //记录当前用户的好友列表信息
                        //contains(key):查询json数据串中有没有key这个键
                        if (responsejs.contains("friends"))
                        {
                            //初始化
                            g_currentUserFriendList.clear();

                            vector<string> vec = responsejs["friends"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                User user;
                                user.setID(js["id"].get<int>());
                                user.setName(js["name"]);
                                user.setState(js["state"]);
                                g_currentUserFriendList.push_back(user);
                            }
                        }

                        //记录当前用户的群组列表信息
                        if (responsejs.contains("groups"))
                        {
                            //初始化
                            g_currentUserGroupList.clear();

                            vector<string> vec1 = responsejs["groups"];
                            for (string &groupstr : vec1)
                            {
                                json groupjs = json::parse(groupstr);
                                Group group;
                                group.setID(groupjs["id"].get<int>());
                                group.setName(groupjs["groupname"]);
                                group.setDesc(groupjs["groupdesc"]);

                                vector<string> vec2 = groupjs["users"];
                                for (string &userstr : vec2)
                                {
                                    GroupUser user;
                                    json js = json::parse(userstr);
                                    user.setID(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setRole(js["role"]);
                                    user.setState(js["state"]);
                                    group.getUsers().push_back(user);
                                }
                                g_currentUserGroupList.push_back(group);
                            }
                        }

                        //显示当前登录用户的基本信息
                        showCurrentUserData();

                        //显示当前用户的离线消息 个人聊天信息或者群组消息
                        if (responsejs.contains("offlinemsg"))
                        {
                            vector<string> vec = responsejs["offlinemsg"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                // time + [id] + name ="said: " + xxx
                                //一对一离线聊天
                                if (ONE_CHAT_MSG == js["msgid"].get<int>())
                                {
                                    cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                                         << " said：" << js["msg"].get<string>() << endl;
                                }
                                //群聊
                                else
                                {
                                    cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                                         << " said：" << js["msg"].get<string>() << endl;
                                }
                            }
                        }

                        //登录成功，启动接收线程接收数据，该线程只启动一次
                        static int readthreadnumber = 0;
                        if (0 == readthreadnumber)
                        {
                            std::thread readTask(readTaskHandler, clientfd); //pthread_create
                            readTask.detach();                               //pthread_detach
                            readthreadnumber++;
                        }

                        //进入聊天主菜单页面
                        isMainMenuRunning = true;
                        mainMenu(clientfd);
                    }
                }
            }
        }
        break;
        case 2: //register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword：";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (-1 == len)
            {
                cerr << "send register message error:" << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                /*
                ssize_t recv(int sockfd,char* buf,int len,int flag);
                sockfd：套接字操作句柄
                buf：用户接收缓冲区的首地址
                len：用户想要读取的数据长度(不能超过缓冲区的大小)
                flag：默认是0(阻塞操作), MSG_DONTWAIT(非阻塞)
                返回值：成功返回实际读取的数据长度；连接断开返回0，读取失败返回-1
                */
                len = recv(clientfd, buffer, 1024, 0);
                if (-1 == len)
                {
                    cerr << "recv register response error" << endl;
                }
                else
                {
                    json responsejs = json::parse(buffer);
                    if (0 != responsejs["errno"].get<int>()) //注册失败
                    {
                        cerr << name << " is already exist,register error!" << endl;
                    }
                    else //注册成功
                    {
                        cout << name << " register success,userid is " << responsejs["id"] << ", do not forget it!" << endl;
                    }
                }
            }
        }
        break;
        case 3: //quit业务
            close(clientfd);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }
    return 0;
}
//接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }

        //接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int messageType = js["msgid"].get<int>();
        if (ONE_CHAT_MSG == messageType)
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said：" << js["msg"].get<string>() << endl;
            continue;
        }
        if (GROUP_CHAT_MSG == messageType)
        {
            cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
    }
}
//显示当前登陆成功用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id：" << g_currentUser.getID() << " name：" << g_currentUser.getName() << endl;
    cout << "----------------------frined list----------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getID() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------Group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getID() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getID() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// 系统支持的客户端命令处理的回调函数声明
// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" conmmand handler
void loginout(int, string);

// 系统支持的客户端命令及其所对应的功能、命令格式列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式：help"},
    {"chat", "一对一聊天，格式：chat:friendid:message"},
    {"addfriend", "添加好友，格式：addfriend:friendid"},
    {"creategroup", "创建群组，格式：creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式：addgroup:groupid"},
    {"groupchat", "群聊，格式：groupchat:groupid:message"},
    {"loginout", "注销，格式：loginout"}};

// 注册系统支持的客户端命令处理的回调函数
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

//主聊天页面程序
void mainMenu(int clientfd)
{
    help();
    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer); //存储用户输入
        string command;            //存储命令
        int index = commandbuf.find(":");
        if (-1 == index)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, index);
        }
        auto it = commandHandlerMap.find(command);
        //输入的命令系统不支持
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }
        //输入的命令系统支持，调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能需要修改该函数
        it->second(clientfd, commandbuf.substr(index + 1, commandbuf.size() - index));
    }
}

// "help" command handler  命令格式：help
void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &mp : commandMap)
    {
        cout << mp.first << ":" << mp.second << endl;
    }
    cout << endl;
}

// "chat" command handler 命令格式：chat:friendid:message
void chat(int clientfd, string str)
{
    int index = str.find(":");
    if (-1 == index)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getID();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send chat msg error->" << buffer << endl;
    }
}

// "addfriend" command handler  命令格式：addfriend:friendid
void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());

    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getID();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error ->" << buffer << endl;
    }
}

// "creategroup" command handler 命令格式：creategroup:groupname:groupdesc
void creategroup(int clientfd, string str)
{
    int index = str.find(":");
    if (-1 == index)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }
    string groupname = str.substr(0, index);
    string groupdesc = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getID();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send creategroup msg error ->" << buffer << endl;
    }
}

// "addgroup" command handler 命令格式：addgroup:groupid
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getID();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error ->" << buffer << endl;
    }
}

// "groupchat" command handler 命令格式：groupchat:groupid:message
void groupchat(int clientfd, string str)
{
    int index = str.find(":");
    if (-1 == index)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getID();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send groupchat msg error ->" << buffer << endl;
    }
}

// "loginout" command handler 命令格式：loginout
void loginout(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getID();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send loginout msg error ->" << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}

// 获取系统时间(聊天信息需要添加时间信息)
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}