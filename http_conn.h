#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <map>
#include "locker.h"
#include "lst_timer.h"
#include "sql_connection_pool.h"
#include "log.h"

class http_conn
{
public:
    static int m_epollfd;                      // 所有的socket上的事件都被注册到同一个epoll对象中
    static int m_user_count;                   // 统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
    static const int FILENAME_LEN = 200;        // 文件名称的最大长度

    http_conn() {}
    ~http_conn() {}

    // HTTP的请求方法，但我们只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE:当前正在分析请求行
    CHECK_STATE_HEADER:当前正在分析头部字段
    CHECK_STATE_CONTENT:当前正在解析请求体*/
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    /*服务器处理HTTP请求的可能结果，报文解析的结果
    NO_REQUEST          ：请求不完整，需要继续读取客户数据
    GET_REQUEST         ：表示获得了一个完成的客户请求
    BAD_REQUEST         ：表示客户请求语法错误
    NO_RESOURCE         ：表示服务器没有资源
    FORBIDDEN_REQUEST   : 表示客户对资源没有足够的访问权限
    FILE_REOUEST        : 文件请求，获取文件成功
    INTERNAL_ERROR      : 表示服务器内部错误
    CLOSED_CONNECTION   : 表示客户端已经关闭连接了*/
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    void process();                                 // 解析客户端请求
    void init(int sockfd, const sockaddr_in &addr, int close_log); // 初始化新接收的连接
    void close_conn();                              // 关闭连接
    bool read();                                    // 非阻塞的读
    bool write();                                   // 非阻塞的写

    // 同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);

private:
    int m_sockfd;                      // 该HTTP连接的socket
    sockaddr_in m_address;             // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_idx;                    // 标识读缓冲区的读入的最后一个字节的下一位置
    char m_write_buf[WRITE_BUFFER_SIZE];    // 写缓冲区
    int m_write_idx;                   // 标识写缓冲区的写入的最后一个字节的下一个位置

    char m_real_file[FILENAME_LEN];     // 存放客户端请求的路径

    int m_checked_idx; // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;  // 当前正在解析的行的起始位置
    char *m_url;       // 请求目标文件的文件名
    char *m_version;   // 协议版本
    METHOD m_method;   // 请求方法
    char *m_host;      // 主机名
    bool m_linger;     // HTTP请求是否要保持连接
    int m_content_length;
    char* m_file_address;       // 客户请求的目标文件被mmap内存映射到内存中的起始位置

    // 用来填充http响应
    void unmap();

    // 目标文件状态
    struct stat m_file_stat;

    // 多内存块
    struct iovec m_iv[2];
    int m_iv_count;

    // 将要发送的字节数
    int bytes_to_send;

    // 是否关闭日志
    int m_close_log;

    // 已经发送的字节数
    int bytes_have_send;

    CHECK_STATE m_check_state; // 主状态机当前所处的状态

    bool process_write(HTTP_CODE ret);

    void init(); // 初始化连接其余的信息

    HTTP_CODE process_read();                 // 解析HTTP请求
    HTTP_CODE parse_request_line(char *text); // 解析请求首行
    HTTP_CODE parse_headers(char *text);      // 解析请求头
    HTTP_CODE parse_content(char *text);      // 解析请求体
    LINE_STATUS parse_line();

    char *get_line() { return m_read_buf + m_start_line; }
    HTTP_CODE do_request();

    bool add_response(const char *format, ...);

    bool add_status_line(int status, const char *title);

    bool add_content(const char *content);

    bool add_content_type();

    void add_headers(int content_length);

    bool add_content_length(int content_length);

    bool add_linger();

    bool add_blank_line();


};

#endif
