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
void serve_static(int fd, char *filename, int filesize, char *method); // 숙제문제 11.11 매개변수 method추가
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); // 숙제문제 11.11 매개변수 method추가
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  Signal(SIGPIPE, SIG_IGN);   // csapp의 Signal 래퍼 사용
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  //듣기 소켓 오픈
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    // echo(connfd);
    Close(connfd); // line:netp:tiny:close
  }
}

void echo(int connfd){
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        if (strcmp(buf, "\r\n") == 0) break;
        Rio_writen(connfd, buf, n);
    }
} // 숙제문제 11.6

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  // 요청 라인 읽고 분석
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  
  // GET 요청이 아닌 다른 요청 입력시 에러 반환 (tiny서버는 GET메소드만 지원함)
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) { // 숙제문제 11.11 method HEAD 추가
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // GET 요청으로부터 URI 파싱
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  // 정적 컨텐츠 제공
  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // regular file인지, 읽기 권한이 있는지 여부 체크
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 숙제문제 11.11 매개변수 method추가
  }
  else { // 동적 컨텐츠 제공
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // regular file인지, 실행 권한이 있는지 여부 체크
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // 숙제문제 11.11 매개변수 method추가
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  // HTTP 응답 본체 빌드
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp) { // 요청 헤더 읽고 무시
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) { // 헤더 종료용 빈 텍스트 줄 체크
    Rio_readlineb(rp, buf, MAXLINE); 
    printf("%s", buf);
  } 
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) { // 정적 컨텐츠
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if(uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else { // 동적 컨텐츠
    // ptr = index(uri, '?');
    ptr = strchr(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) { // 숙제문제 11.11 매개변수 method추가
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];


  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK \r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "GET") == 0) { // 숙제문제 11.11 method가 GET일 경우에만 body출력
    srcfd = Open(filename, O_RDONLY, 0);
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    srcp = (char *)malloc(filesize); // 숙제문제 11.9 mmap > malloc + rio_readn
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    // Nummap(srcp, filesize);
    free(srcp);
  }
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif")) strcpy (filetype, "image/gif");
  else if (strstr(filename, ".png")) strcpy (filetype, "image/png");
  else if (strstr(filename, ".jpg")) strcpy (filetype, "image/jpeg");
  else if (strstr(filename, ".mpg") || strstr(filename, ".mp4") 
          || strstr(filename, ".mpeg")) strcpy (filetype, "video/mpeg"); // 숙제문제 11.7 MPG 비디오 파일 처리
  else strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  // HTTP 응답 static과 일관성 유지
  sprintf(buf, "%sContent-type: text/html\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n\r\n", buf);
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1); // 숙제문제 11.11 HEAD일 때, 환경변수 변경 (/cig-bin/adder.c)
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}