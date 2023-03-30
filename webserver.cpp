#include "webserver.h"

Webserver::Webserver() {
    users = new http_conn[MAX_FD];
    //root文件夹路径
    char server_path[200];

    // 将当前工作目录的绝对路径复制到server_path中区
    getcwd(server_path, 200);
    char root[6] = "/root";

    // 从堆中获取对应字节的内存空间，并返回该内存的首地址给 m_root
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);

    // 将server_path的内容复制到m_root中
    strcpy(m_root, server_path);
    
    // 定时器
    users_timer = new client_data[MAX_FD];
}

// Webserver析构函数
Webserver::~Webserver() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void Webserver::init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, 
        int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void Webserver::trig_mode()
{
    // LT + LT  LT 监听模式和 LT 连接模式是最简单的网络服务器工作模式。
    // 在 LT 监听模式下，服务器在接收到连接请求时立即响应，并等待数据的到来。
    // 在 LT 连接模式下，服务器每次只处理一个请求，直到该请求处理完毕后才处理下一个请求。
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET LT 监听模式和 ET 连接模式可以提高服务器的响应速度。
    // 在 LT 监听模式下，服务器仍然立即响应连接请求。
    // 在 ET 连接模式下，服务器等待数据的到来，但只要有数据到来，服务器就会立即处理该数据。
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT ET 监听模式和 LT 连接模式可以提高服务器的效率。
    // 在 ET 监听模式下，服务器等待连接请求，但只要有连接请求到来，服务器就会立即响应。
    // 在 LT 连接模式下，服务器每次只处理一个请求，直到该请求处理完毕后才处理下一个请求。
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET ET 监听模式和 ET 连接模式是最高效的网络服务器工作模式。
    // 在 ET 监听模式下，服务器等待连接请求，但只要有连接请求到来，服务器就会立即响应。
    // 在 ET 连接模式下，服务器等待数据的到来，但只要有数据到来，服务器就会立即处理该数据。
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void Webserver::log_write() {
    if (m_close_log == 0) {
        // 初始化日志
        if (m_log_write == 1) {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        } else {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

void Webserver::sql_pool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库
    users->initmysql_result(m_connPool);
}

void Webserver::thread_pool() {
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void Webserver::eventListen() {
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 若为负数则断言失败
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (m_OPT_LINGER == 0) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else if (m_OPT_LINGER == 1) {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    // bzero函数将指定内存块，前n个字节全部设为0
    bzero(&address, sizeof(address));
    // sin_family表示地址家族，AF_INET AF表示Address Family地址家族的意思，INET是internet的缩写 整体表示的是IPv4地址
    address.sin_family = AF_INET;
    // htonl（h：主机字节顺序，to：转换为，n：网络，l:unsigned long无符号长整形）作用是将一个32位数从主机字节顺序转换为网络字节顺序 【32位】
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    // htons（s：signed long 有符号长整形）作用是将一个16位数的主机字节顺序转换为网络字节书序【16位】
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    // 将套接字与指定端口相连
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    // listen函数监听该套接字，使得该进程能够接受其它进程的请求，从而成为一个服务器进程；【5表示能够容纳5个用户请求】
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;
    // socketpair函数能够创建一个套接字进行进程间通信
    // 第一个参数 domin 表示协议族
    // 第二个参数 type 表示协议 两个选项：SOCK_STREAM（表示TCP协议）或SOCK_DGRAM（表示UDP协议）
    // 第三个参数 protocol 表示类型，只能为0
    // 第四个参数 sv[2] 表示套接字柄对
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    // 对文件描述符设置非阻塞
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}




