#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int sockfd;
    int len;
    struct sockaddr_in address; //address是网络通信的地址
    int result;
    char ch = 'A';

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;  //sin_family在哪里定义的？
    address.sin_addr.s_addr = inet_addr("127.0.0.1"); //inet_addr将ip地址转换成32位整形，但是找不到在哪里实现的
    address.sin_port = htons(9734);
    len = sizeof(address);
    result = connect(sockfd, (struct sockaddr *)&address, len);

    if (result == -1) //连接失败
    {
        perror("oops: client1");
        exit(1);
    }
    write(sockfd, &ch, 1);  //向套接字写入1个字符，传给服务器
    read(sockfd, &ch, 1);
    printf("char from server = %c\n", ch);
    close(sockfd);
    exit(0);
}
