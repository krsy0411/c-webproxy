#include "csapp.h"

int main(int argc, char **argv){
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio; // 버퍼링된 안전 입출력 상태를 담는 구조체... ? 8kb??

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host><port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = argv[2];

    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while (Fgets(buf, MAXLINE, stdin) != NULL){
        Rio_writen(clientfd, buf, strlen(buf));     // 서버로 한 줄 전송
        Rio_readlineb(&rio, buf, MAXLINE);          // 서버가 돌려준 줄 읽기
        Fputs(buf, stdout);                         // 출력
    }
    Close(clientfd);
    exit(0);
}