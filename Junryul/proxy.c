#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from %s:%s\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }

  // printf("%s", user_agent_hdr);
  return 0;
}

void doit(int fd)
{
  int clientfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char request_buf[MAX_CACHE_SIZE], response_buf[MAX_CACHE_SIZE];
  rio_t rio;
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];

  // 클라이언트 요청 라인 전부 읽고 따로 저장해두기
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 요청 라인 파싱

  if (strcasecmp(method, "GET") != 0)
  {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  // 요청라인 파싱해서 hostname, port, path 얻기
  parse_uri(uri, hostname, port, path);

  /* --  헤더변환내용 -- */
  change_requesthdrs(&rio, method, hostname, port, path, request_buf, sizeof(request_buf));

  // 새롭게 클라이언트 소켓 열기
  clientfd = Open_clientfd(hostname, port);
  if (clientfd < 0)
  {
    fprintf(stderr, "Connection failed\n");
    return;
  }

  // 서버에 요청하고, 응답 전부 클라이언트에 전송
  Rio_writen(clientfd, request_buf, strlen(request_buf)); // 요청 라인 전송
  int n;
  while ((n = Rio_readn(clientfd, response_buf, MAX_CACHE_SIZE)) > 0)
  {
    Rio_writen(fd, response_buf, n);
  }
  Close(clientfd);
}

int parse_uri(char *uri, char *hostname, char *port, char *path)
{
  /* strstr, strchr 이용,
  http://www.google.com:80/path/to/resource
  나왔을 때, 파싱하는 함수 작성.
  hostname: www.google.com
  port: 80
  path: /path/to/resource

  의사코드
  //를 기준으로 나눔
  다시 :를 기준으로 나누고,
  그걸 다시 / 기준으로 나눠서 port와 path에 저장
  */

  // '//' 기준으로 자르기
  char *ptr = strstr(uri, "//");
  if (ptr)
  {
    ptr = ptr + 2;
  }
  else
  {
    ptr = uri;
  }
  strcpy(hostname, ptr);
  // 결과: hostname에 저장되는 값: www.google.com:80/path/to/resource

  // path 설정
  ptr = strchr(hostname, '/');
  if (ptr)
  {
    strcpy(path, ptr);
    *ptr = '\0';
  }
  else
  {
    strcpy(path, "/");
  }

  // port 설정
  ptr = strchr(hostname, ':');
  if (ptr)
  {
    *ptr = '\0';
    strcpy(port, ptr + 1);
  }
  else
  {
    strcpy(port, "80");
  }
  return 0;
}

void change_requesthdrs(rio_t *rp, char *method, char *hostname, char *port, char *path, char *request_buf, size_t size)
{
  /* 헤더 변환 함수
  헤더는 프록시로 들어올 때 GET http://www.google.com:80/path/to/resource HTTP/1.1
  이렇게 들어오고, 서버에 요청할 때는
  GET /path/to/resource HTTP/1.1 이렇게 바꿔줘야 한다.
  바로 아래 따라오는 Host 또한,
  Host: www.google.com:80 이렇게 들어올 확률이 높지만,
  HTTP1.0에서는 이 값을 보내지 않을 수도 있고,
  정확하지 않을 확률 또한 존재하므로, 우리가 가진 Hostname과 port로 지정해줄 함수가 필요
  */

  request_buf[0] = '\0';

  // 헤더 변환해야 되는 부분 미리 지정
  int n = snprintf(request_buf, size,
                   "%s %s HTTP/1.1\r\n"
                   "Host: %s:%s\r\n"
                   "Connection: close\r\n"
                   "Proxy-Connection: close\r\n"
                   "%s\r\n",
                   method, path, hostname, port, user_agent_hdr);

  if (n < 0 || n >= size)
  {
    fprintf(stderr, "Request buffer overflow\n");
    return;
  }

  char buf[MAXLINE]; // 요청 헤더를 읽기 위한 버퍼

  // 나머지 헤더 읽기
  while (Rio_readlineb(rp, buf, MAXLINE) > 0)
  {
    if (!strcmp(buf, "\r\n"))
    {
      break;
    }
    // 따로 지정하는 부분은 무시하게끔
    if (strncasecmp(buf, "Host:", 5) == 0 ||
        strncasecmp(buf, "Connection:", 11) == 0 ||
        strncasecmp(buf, "Proxy-Connection:", 17) == 0 ||
        strncasecmp(buf, "User-Agent:", 11) == 0)
    {
      continue;
    }
    strncat(request_buf, buf, size - strlen(request_buf) - 1);
  }
  strncat(request_buf, "\r\n", size - strlen(request_buf) - 1); // 헤더 끝 표시
}

// 클라이언트 오류 응답 함수
void clienterror(int fd, char *uri, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s</p>\r\n", body, shortmsg, longmsg);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}