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
    strcat(m_root, root);
    
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

void Webserver::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void Webserver::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void Webserver::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool Webserver::dealclinetdata()
{
    // 客户端地址结构体
    struct sockaddr_in client_address;
    // 客户端地址结构体长度
    socklen_t client_addrlength = sizeof(client_address);
    // 如果是 LT 模式
    if (0 == m_LISTENTrigmode)
    {
        // 接受客户端连接
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            // 如果接受连接出错，记录错误并返回 false
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            // 如果连接数超过最大值，发送错误信息给客户端并记录错误，并返回 false
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 给该连接创建一个定时器
        timer(connfd, client_address);
    }// 如果是 ET 模式
    else
    {
        while (1)
        {
            // 接受客户端连接
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                // 如果接受连接出错，记录错误并跳出循环
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                // 如果连接数超过最大值，发送错误信息给客户端并记录错误，并跳出循环
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            // 给该连接创建一个定时器
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool Webserver::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    // 接受管道读取的信息
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        // 如果接受信息出错，返回 false
        return false;
    }
    else if (ret == 0)
    {
        // 如果接受信息为空，返回 false
        return false;
    }
    else
    {
        // 遍历接受到的信息
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void Webserver::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void Webserver::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void Webserver::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}


