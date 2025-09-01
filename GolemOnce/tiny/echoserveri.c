#include "csapp.h"

void echo(int connfd);

int main(int argc, char **argv){
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; //Enough space for any address (128 bytes) 모든 형태의 소켓 주소를 저장하기에 충분히 큼
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if(argc != 2){ // 인자로 port 하나만 받는다(메인함수 1개 + 추가인자)
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = Open_listenfd(argv[1]); // bind+listen, 지정 port에 TCP 서버 소켓 준비
    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // Accept가 블로킹돼있다가 연결이 오면 connfd 돌려줌
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0); // getnameinfo이 구조체를 호스트, 서비스 문자열로 반환
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        echo(connfd);
        Close(connfd); // 
    }
    exit(0);
}