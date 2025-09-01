/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char* method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char* method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 11.11 : GET, HEAD 메서드만 지원하도록 변경
  if(!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0))
  {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  // 요청 헤더 읽기
  read_requesthdrs(&rio);

  // GET 요청으로부터 URI 파싱
  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not Found", "Tiny couldn't find this file");
    return;
  }

  // 정적 컨텐츠 제공(웹 서버)
  if(is_static)
  {
    // 일반 파일이 아니거나 읽기 권한이 없는 경우
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }

    // 정적 컨텐츠 제공
    serve_static(fd, filename, sbuf.st_size, method);
  }
  // 동적 컨텐츠 제공(웹 애플리케이션 서버)
  else
  {
    // 일반 파일이 아니거나 실행 권한이 없는 경우
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't execute the CGI program");
      return;
    }

    // 동적 컨텐츠 제공
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  
  // HTTP 응답 body 빌드
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, shortmsg, longmsg);
  sprintf(body, "%s<hr><em>Tiny Web Server</em>\r\n", body);

  // HTTP 응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Length: %d\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, "\r\n", 4);
  Rio_writen(fd, body, strlen(body));
}

// 요청 헤더를 읽어 단순히 출력만 하고 파싱은 하지 않는 함수
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  // strcmp : 두 문자열을 비교할 때 사용 : 같으면 0 반환
  while(strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }

  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char* ptr;

  // 정적 컨텐츠
  if(!strstr(uri, "cgi-bin"))
  {
    // 정적 컨텐츠의 경우엔 굳이 cgiargs를 설정할 필요가 없음
    strcpy(cgiargs, ""); // strcpy : 문자열 복사 함수
    strcpy(filename, ".");
    strcat(filename, uri);

    // 디폴트 파일(루트 경로) 설정
    if(uri[strlen(uri) - 1] == '/')
    {
      // 이 경우 파일을 home.html로 설정
      strcat(filename, "home.html");
    }

    return 1;
  }
  // 동적 컨텐츠
  else
  {
    // uri에서 CGI 인자 추출
    ptr = strchr(uri, "?");

    // CGI 인자가 있는 경우
    if(ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';  // uri에서 쿼리 스트링 제거
    }
    else
    {
      strcpy(cgiargs, "");
    }

    strcpy(filename, ".");
    strcat(filename, uri);  // 이제 uri는 /cgi-bin/adder만 포함

    return 0;
  }
}

void serve_static(int fd, char* filename, int filesize, char* method)
{
  int src_fd;
  char* srcp, filetype[MAXLINE], buf[MAXBUF];

  // 클라이언트에게 응답 헤더 전송
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);
  // CGI 프로그램 입장에서 표준 출력(stdout)에 데이터를 쓰면, 웹 서버가 그 출력을 받아서 클라이언트에게 전달
  Rio_writen(fd, buf, strlen(buf)); // 웹 서버가 클라이언트 소켓(fd)에 데이터를 쓰는 부분
  printf("Response headers:\n");
  printf("%s", buf);

  //  11.11 : GET 메서드에 대해서만 응답 본문 전송
  if(strcasecmp(method, "GET") == 0)
  {
    // 클라이언트에게 응답 본문(바디) 전송
    src_fd = Open(filename, O_RDONLY, 0); // 파일을 읽기 전용으로 열기 -> 반환(파일 디스크립터)
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, src_fd, 0); // 파일 내용을 메모리에 매핑 -> 반환(파일 데이터가 메모리에 적재된 위치(포인터))

    srcp = (char *)malloc(sizeof(char) * filesize);
    Rio_readn(src_fd, srcp, filesize); // 파일 내용 읽기
    Close(src_fd); // 파일 디스크립터 닫기
    Rio_writen(fd, srcp, filesize); // 웹 서버가 클라이언트 소켓(fd)에 데이터 쓰기
    free(srcp);

    // Munmap(srcp, filesize); // 메모리 매핑 해제 -> 사용이 끝난 메모리 리소스를 OS에 반환
  }
}

void get_filetype(char* filename, char* filetype)
{
  // strstr : 문자열 내 특정 부분 문자열(패턴)이 처음 나타나는 위치를 찾는 함수

  // MIME 타입
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mpg") || strstr(filename, ".mpeg"))
    strcpy(filetype, "video/mpeg"); // 11.7 : MPG 비디오 파일 처리
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char* filename, char* cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  // HTTP 응답의 첫 부분 전송
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf)); // HTTP 응답의 첫 부분 전송(클라이언트 소켓에 데이터 쓰기)

  // CGI 프로그램 실행 : 부모 프로세스는 계속 서버 역할 & 자식 프로세스는 클라이언트 소켓을 표준 출력으로 바꾸고 CGI 프로그램을 실행해 결과를 클라이언트에게 직접 전송
  // 조건문 : Fork() 함수 실행시, 부모 프로세스는 자식 프로세스의 PID를 반환받고 자식 프로세스는 0을 반환 -> 즉, 자식 프로세스에서만 실행되도록 하기
  if(Fork() == 0)
  {
    // cgi-bin/adder.c에 넘겨주기 위한 환경변수 설정
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);

    Dup2(fd, STDOUT_FILENO); // 표준 출력을 클라이언트 소켓으로 리다이렉션 -> CGI 프로그램이 표준 출력으로 쓰는 모든것은 클라이언트로 바로 감(부모프로세스의 간섭 없이)
    Execve(filename, emptylist, environ); // CGI 프로그램 실행
  }

  Wait(NULL); // 부모 프로세스가 자식 프로세스가 종료될떄까지 대기하는 함수
}