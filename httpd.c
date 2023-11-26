/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg)
{
    int client = (intptr_t)arg; //套接字描述符
    char buf[1024];   //字符缓冲区，把request中的字符都存入这里边
    size_t numchars;
    char method[255];  //请求方法
    char url[255];    // 请求中的URL 
    char path[512];   // 真正的资源路径
    size_t i, j;
    struct stat st;  //描述文件状态  https://blog.csdn.net/lijian2017/article/details/87878810
    int cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
    char *query_string = NULL;

    //获取http请求的第一行字符串，如'GET /check.cgi HTTP/1'
    numchars = get_line(client, buf, sizeof(buf)); // 返回值是buf缓冲区中已被填入字符的最高位，通俗讲就是buf中被字符填到哪个位置了

    // 截出来method
    i = 0; j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';  //字符串结束符

    // 只接受GET和POST请求，其它类型的请求调用uniplemented函数
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) //strcasecmp忽略大小写比较字符串，相同返回0。注意：一直比较直到碰到结束符'\0'
    {
        unimplemented(client);
        return;
    }

    // 所有的POST请求都是带请求体参数的，需要使用cgi来处理参数
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    // 截出来请求中的URL，如'GET /check.cgi HTTP/1'中的'/check.cgi'
    i = 0;
    while (ISspace(buf[j]) && (j < numchars)) //处理连续的空格
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) //截出来URL
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    // GET请求也可以在URL中携带参数，此时也需要cgi文件处理参数，故需要特别讨论一下
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0'; //非常重要的步骤，假如原来是url='/color.cgi?color:'red'',此时找到'?'后，把这一位改成字符串结束符，相当于url='/color.cgi'
            query_string++;
        }
    }


    sprintf(path, "htdocs%s", url); //字符串格式化，假如url="/index.html",则path="htdocs/index.html"
    if (path[strlen(path) - 1] == '/') //在请求中，如果path结尾是'/'比如'/document/'或者是'/',默认的请求页面是该目录下的index.html
        strcat(path, "index.html");


    if (stat(path, &st) == -1) {   //将参数path所指的文件状态, 复制到参数st所指的结构中，返回值为-1表示这个文件不存在
        while ((numchars > 0) && strcmp("\n", buf))     /* 一直读 & 丢弃头部信息 */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        // 不理解这一步的操作是在干什么!!
        if ((st.st_mode & S_IFMT) == S_IFDIR)  //检查文件的类型是否是目录
            strcat(path, "/index.html");       //感觉有个小bug，万一这个目录下没有index.html文件呢？这里应该需要再判断一下吧

        // 在这个项目中，请求既可以指向html页面，也可以指向cgi(可以通过cgi渲染html页面)，因此这里需要根据文件的执行性
        // 判断这个请求的指向是不可执行的html页面，还是可执行的cgi文件
        // 如果就是html页面，只需要执行serve_file函数进行简单的页面内容的返回
        // 如果是cgi文件，那就要执行execute_cgi函数，通过这个函数执行cgi来生成html页面。
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    ) //如果是一个可执行文件的话，说明这个文件是cgi文件，
            cgi = 1;     //
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];
    fgets(buf, sizeof(buf), resource); //从指定的流 stream 读取一行，并把它存储在 str 所指向的字符串内。当读取 (n-1) 个字符时，或者读取到换行符时，或者到达文件末尾时，它会停止
    while (!feof(resource)) //测试给定流 stream 的文件结束标识符,
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2]; 
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;


    buf[0] = 'A'; buf[1] = '\0'; 
    if (strcasecmp(method, "GET") == 0)  //get的话，是没有请求体的，读取整个HTTP请求，并丢弃
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0)   //如果是POST的话，需要从请求头中读出Contemt-Length字段，方便等会儿读请求体中的内容，
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';    //字符串结束符
            if (strcasecmp(buf, "Content-Length:") == 0)  //比较前15个字符
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
    }
    // pipe()函数的参数是一个包含2个文件描述符的数组，cgi_output[0]:读管道，cgi_output[1]:写管道
    if (pipe(cgi_output) < 0) { //https://developer.aliyun.com/article/516018
        cannot_execute(client);
        return;
    } 
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 ) {  //https://www.cnblogs.com/outsider0606/p/16559584.html
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 响应行
    send(client, buf, strlen(buf), 0);
    

    printf("PID=%d", pid);
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT); //将子进程--写管道的文件的输出重定向为标准输出
        dup2(cgi_input[0], STDIN);   //将子进程--读管道的文件的输入重定向为标准输入
         //关闭子进程的两个无用端口
        close(cgi_output[0]);  
        close(cgi_input[1]);
        // 设置环境变量，这些环境变量都是为了给 cgi 脚本调用
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        } 

        execl(path, NULL); //执行参数path字符串所代表的路径文件，path是要执行的文件路径
        exit(0);
    } else {    /* parent */   // https://www.cnblogs.com/LHNing/archive/2012/03/12/2391438.html
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {   //读取请求体中的数据，写入
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);  //套接字、缓冲区、缓冲区大小、0，char类型是一个字节，故第三个参数是1
        //返回值是copy的字节数
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) 
        {
            if (c == '\r') //如果是回车符，需要判断一下下一个字符是不是换行符
            {
                n = recv(sock, &c, 1, MSG_PEEK);   // 函数从套接字 sock 中接收数据，但是不移除接收到的数据，而是通过 MSG_PEEK 标志来查看当前可用的数据。
                /* DEBUG printf("%02X\n", c); */
                // 下一个字符是换行符，那就再把换行符读出来，等会就可以直接退出函数
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0); //此时c=='\n'
                // 下一个字符不是换行符，那也满足是“回车符”的条件，依旧不保存字符，退出函数
                else
                    c = '\n';
            }
            //保存到缓冲区中
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);

    // 打印这个SERVER_STRING
    /*
    for(int i=0;i<strlen(buf);i++)
        printf("%c",buf[i]);
    printf("\n");
    */

    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client) //经典404
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL; //指向文件的指针
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");  //在这里解决了122行的疑问，判断文件是否存在是在这里判断的
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;   //套接字描述符
    int on = 1;
    struct sockaddr_in name;  //ip+port

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)  //除了post和get的请求方法不被支持
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;    //服务器运行在4000端口
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(&client_sock); */
        if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}
