#include "http_conn.h"

int http_conn::m_epollfd = -1;   
int http_conn::m_user_count = 0;   

// define some http status msg
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";


// 网站的根目录
const char* doc_root = "/home/zpvisen/TinyWebServer/webserver/resources";

// 设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    if (!one_shot) {
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中移除要监听的文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，充值socket上EPOLLONESHOT时间，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化状态为请求首行
    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);

}

// 关闭连接
void http_conn::close_conn() {
    if (m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;     // 关闭一个连接，客户总数量减一
    }
}

bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    // 读到的字节
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            return false;
        } else if ( bytes_read == 0) {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read; 
    }
    printf("读到了数据：%s\n", m_read_buf);
    return true;
}

// 主状态机
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS  line_status = LINE_OK;
    // 请求结果
    HTTP_CODE ret = NO_REQUEST;
    
    char * text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) 
    || ((line_status = parse_line()) == LINE_OK)) {
        // 解析到一行完整的数据，或解析到了请求体，也是完整的数据

        // 获取一行数据
        text = get_line();

        m_start_line = m_checked_idx;
        printf("获取到一行HTTP数据: %s\n", text);
        
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    return do_request();
                }
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
}


http_conn::HTTP_CODE http_conn::parse_request_line(char * text) {
    // GET /index.heml HTTP/1.1
    m_url = strpbrk(text, " \t");

    // GET\0/index.heml HTTP/1.1
    *m_url++ = '\0';
    // m_url为 /index.heml HTTP/1.1
    char * method = text;

    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    // m_url为 /index.heml\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 有的不是index.html而是http://192.168.1.1:10000/index.html
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7; // 跳过http://
        m_url = strchr(m_url, '/'); // /index.heml
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;     // 主状态机检查状态变成检查请求头
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char * text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字段消息体
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection", 11) == 0) {
        // 处理Connection头部字段 Connection:keep-alive
        text += 11;
        text += strspn(text, " \t");
        if ( strcasecmp(text, "keep-alive" ) == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    } else {
        printf("哦！未知的头部字段%s \n", text);
    }
    return NO_REQUEST;
}

// 我们并没有真正解析HTTP请求的消息体，只是判断它是否完整读入了
http_conn::HTTP_CODE http_conn::parse_content(char * text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


// 解析一行
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;

    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } 
        return LINE_OPEN;
    }
}

// 当得到一个完整、正确的HTTP请求时，我们分析目标文件得到属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，且告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 以只读的方式打开文件
    int fd = open(m_real_file, O_RDONLY);

    // 创建内存映射
    m_file_address = (char*) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}


bool http_conn::write() {
    printf("一次性写完数据\n");
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);

    printf("parse request, create response");

    // 生成响应

}