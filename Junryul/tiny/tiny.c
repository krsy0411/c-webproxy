/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);                                                                  // 요청 처리 함수
void read_requesthdrs(rio_t *rp);                                                   // 요청 헤더 읽기 함수
int parse_uri(char *uri, char *filename, char *cgiargs);                            // URI 파싱 함수
void serve_static(int fd, char *filename, int filesize);                            // 정적 컨텐츠 제공 함수
void get_filetype(char *filename, char *filetype);                                  // 파일 타입 결정 함수
void serve_dynamic(int fd, char *filename, char *cgiargs);                          // 동적 컨텐츠 제공 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 클라이언트 오류 응답 함수
void sigchld_handler(int sig); // 과제 11.8 시그널 핸들러

int main(int argc, char **argv)
{
  int listenfd, connfd;                  // 연결을 위한 파일 디스크립터
  char hostname[MAXLINE], port[MAXLINE]; // 클라이언트의 호스트 이름과 포트 번호
  socklen_t clientlen;                   // 클라이언트 주소의 길이
  struct sockaddr_storage clientaddr;    // 클라이언트 주소 구조체

  signal(SIGCHLD, sigchld_handler); // 과제 11.8 시그널 핸들러

  // 인자가 부족한 경우
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // 포트 번호를 출력
    exit(1);
  }

  // 포트 번호를 사용하여 수신 대기 소켓 생성
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);                                                 // 클라이언트 주소의 길이를 설정
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                       // 클라이언트 연결 수락
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트의 호스트 이름과 포트 번호 가져오기
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);                                                                   // 요청 처리
    Close(connfd);
  }
}

// 요청 처리 함수
void doit(int fd)
{
  int is_static;                                                      // 정적 컨텐츠 여부
  struct stat sbuf;                                                   // 파일 상태 정보를 담는 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청 라인과 헤더를 저장하는 버퍼
  char filename[MAXLINE], cgiargs[MAXLINE];                           // 파일 이름과 CGI 인자를 저장하는 버퍼
  rio_t rio;

  Rio_readinitb(&rio, fd);                       // RIO 버퍼 초기화
  Rio_readlineb(&rio, buf, MAXLINE);             // 요청 라인 읽기
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 요청 라인 파싱
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // URI 파싱
  is_static = parse_uri(uri, filename, cgiargs);
  // 파일 상태 확인
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not Found", "Tiny couldn't find this file");
    return;
  }

  // 정적 컨텐츠인지 동적 컨텐츠인지 확인
  if (is_static)
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    { // 파일 접근 권한 확인
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // 정적 컨텐츠 제공
  }
  else
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // 동적 컨텐츠 제공
  }
}

// 요청 헤더 읽기 함수
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE); // 요청 헤더의 첫 번째 라인 읽기
  while (strcmp(buf, "\r\n"))
  {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE); // 다음 요청 헤더 라인 읽기
  }
  return;
}

// URI 파싱 함수
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // URI에서 CGI 인자 추출
  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");   // CGI 인자가 없는 경우 빈 문자열로 초기화
    strcpy(filename, "."); // 기본 파일 이름 설정
    strcat(filename, uri); // URI를 파일 이름에 추가
    if (uri[strlen(uri) - 1] == '/')
    {
      strcat(filename, "home.html"); // 디렉토리 요청 시 기본 파일 설정
    }
    return 1;
  }
  else
  {                        // 동적 컨텐츠
    ptr = index(uri, '?'); // URI에서 CGI 인자 추출
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1); // CGI 인자 복사
      *ptr = '\0';              // URI에서 CGI 인자 분리
    }
    else
    {
      strcpy(cgiargs, "");
    }
    strcpy(filename, "."); // 기본 파일 이름 설정
    strcat(filename, uri); // URI를 파일 이름에 추가
    return 0;
  }
}

// 정적 컨텐츠 제공 함수
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);                          // 파일 유형 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));                          // 응답 헤더 전송
  printf("Response headers:\n");
  printf("%s", buf);

  // 과제 11.9 malloc 이용 파일 전송
  srcfd = Open(filename, O_RDONLY, 0);                        // 원본 파일 열기
  srcp = (char*)malloc(filesize);                             // 파일 크기만큼 메모리 할당
  Rio_readn(srcfd, srcp, filesize);                           // 파일 내용을 메모리에 읽기
  Close(srcfd);                                               // 원본 파일 닫기
  Rio_writen(fd, srcp, filesize);                             // 응답 본문 전송
  free(srcp);
}

// 파일 유형 결정 함수
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))   // HTML 파일인 경우 (filename에 .html 포함)
    strcpy(filetype, "text/html"); // HTML MIME 타입 (filetype에 복사)
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4"); // 과제 11.7
  else
    strcpy(filetype, "text/plain");
}

// 과제 11.8 시그널 핸들러
void sigchld_handler(int sig) {
    int olderrno = errno;
    int status;
    pid_t pid;

    // 종료된 자식 프로세스 수거
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("Child %d terminated\n", pid);
    }

    errno = olderrno;
}

// 동적 컨텐츠 제공 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));            // 응답 헤더 전송
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));            // 서버 정보 전송

  if (Fork() == 0)
  {
    setenv("QUERY_STRING", cgiargs, 1);   // CGI 인자를 환경 변수로 설정
    Dup2(fd, STDOUT_FILENO);              // 표준 출력을 클라이언트로 리다이렉트
    Execve(filename, emptylist, environ); // CGI 프로그램 실행
    exit(0);
  }
}

// 클라이언트 오류 응답 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
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