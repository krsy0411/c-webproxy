#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
int parse_uri(char* uri, char* hostname, char* path, int* port);
void makeHttpHeader(char* http_header, char* hostname, char* path, int port, rio_t* client_rio);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listen_fd, connection_fd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t client_len;
  struct sockaddr_storage client_addr;

  // 인자 개수가 알맞게 안 들어왔으면, 에러를 출력하고 종료
  if(argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listen_fd = Open_listenfd(argv[1]);
  while(1)
  {
    client_len = sizeof(client_addr);
    connection_fd = Accept(listen_fd, (SA *)(&client_addr), &client_len);
    Getnameinfo((SA *)(&client_addr), client_len, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);

    doit(connection_fd);
    Close(connection_fd);
  }

  // printf("%s", user_agent_hdr);
  return 0;
}

/* fd(= connect_fd) : 클라이언트와 연결된 소켓의 파일 디스크립터 */
void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], http_header[MAXLINE];
  int port; // 서버의 포트 번호
  char port_ch[10]; // port를 문자열로 저장한 변수
  rio_t rio; // 클라이언트와의 통신을 위한 I/O 구조체
  rio_t server_rio; // 서버와의 통신을 위한 I/O 구조체
  int server_fd; // 프록시가 웹 서버와 연결할 때 사용하는 소켓의 파일 디스크립터

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  parse_uri(uri, hostname, path, &port);
  makeHttpHeader(http_header, hostname, path, port, &rio);
  sprintf(port_ch, "%d", port); // port를 문자열로 변환해 저장

  /* 서버와 연결 후, 재구성한 HTTP 헤더를 서버에 전송 */
  server_fd = Open_clientfd(hostname, port_ch);
  if(server_fd < 0)
  {
    fprintf(stderr, "Error: Unable to connect to server\n");
    return;
  }

  Rio_readinitb(&server_rio, server_fd);
  Rio_writen(server_fd, http_header, strlen(http_header));

  /* 클라이언트가 보냈던 원문도 서버에 전송하고 close */
  size_t temp = rio_readlineb(&server_rio, buf, MAXLINE);
  while(temp != 0)
  {
    Rio_writen(fd, buf, temp);
    temp = rio_readlineb(&server_rio, buf, MAXLINE);
  }

  /* 연결 종료 */
  Close(server_fd);
}

/* URI를 파싱해 호스트명, 경로, 포트 번호를 추출하고 대입 */
int parse_uri(char* uri, char* hostname, char* path, int* port)
{
  // default 설정 : 80 포트 사용
  *port = 80;

  char* hostname_idx = strstr(uri, "//");
  if(hostname_idx != NULL)
  {
    hostname_idx = hostname_idx + 2;
  }
  else
  {
    hostname_idx = uri;
  }

  char* path_idx = strstr(hostname_idx, ":");
  // ":"가 있는 경우 : "\0"으로 변환하고 hostname, path, port를 추출하고 설정
  if(path_idx != NULL)
  {
    *path_idx = '\0';
    sscanf(hostname_idx, "%s", hostname);
    sscanf(path_idx + 1, "%d%s", port, path);
  }
  // ":"가 없는 경우 : "/"(경로)를 찾아 설정
  else
  {
    path_idx = strstr(hostname_idx, "/");

    if(path_idx != NULL)
    {
      *path_idx = '\0';
      sscanf(hostname_idx, "%s", hostname); // 호스트 이름 설정
      *path_idx = '/';
      sscanf(path_idx, "%s", path); // 기본 경로로 설정
    }
    else
    {
      // ":"와 "/" 모두 없는 경우 : hostname만 설정
      sscanf(hostname_idx, "%s", hostname);
    }
  }

  return 0;
}

/* 프록시에서 웹 서버로 전달할 HTTP 헤더를 생성(재구성) */
void makeHttpHeader(char* http_header, char* hostname, char* path, int port, rio_t* client_rio)
{
  char buf[MAXLINE], request_header[MAXLINE], other_header[MAXLINE], host_header[MAXLINE];

  // 요청 라인 생성
  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);

  // 클라이언트 헤더를 파싱하고 복사 
  while(rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    // 빈 텍스트 라인 : 종료
    if(strcmp(buf, "\r\n") == 0)
    {
      break;
    }

    // Host 헤더는 host_header에 복사
    if(!(strncasecmp(buf, "Host", strlen("Host"))))
    {
      strcpy(host_header, buf);
      continue;
    }

    // Connection, Proxy-Connection, User-Agent 헤더는 other_header에 복사
    if(!(strncasecmp(buf, "Connection", strlen("Connection"))) && !(strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))) && !(strncasecmp(buf, "User-Agent", strlen("User-Agent"))))
    {
      strcat(other_header, buf);
    }
  }

  // Host 헤더가 없는 경우 기본값 설정
  if(strlen(host_header) == 0)
  {
    sprintf(host_header, "Host: %s\r\n", hostname);
  }
  
  // 마지막에 헤더 조립
  sprintf(http_header, "%s%s%s%s%s%s%s", request_header, host_header, "Connection: close\r\n", "Proxy-Connection: close\r\n", user_agent_hdr, other_header, "\r\n");

  return;
}