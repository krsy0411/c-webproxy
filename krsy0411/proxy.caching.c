#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_OBJ_NUM ((int)(MAX_CACHE_SIZE / MAX_OBJECT_SIZE))

typedef struct cache_entry_t
{
  char uri[MAXLINE];
  char data[MAX_OBJECT_SIZE];
  int size;
  unsigned long timestamp; // 캐시 사용 시간
} cache_entry_t;

typedef struct cache_t
{
  cache_entry_t entries[MAX_OBJ_NUM];
  int count;
  unsigned long time; // 캐시가 사용된 총 시간(= 캐시가 사용된 횟수)
  pthread_mutex_t lock;
} cache_t;

void doit(int fd);
int parse_uri(char* uri, char* hostname, char* path, int* port);
void makeHttpHeader(char* http_header, char* hostname, char* path, int port, rio_t* client_rio);
void* thread(void* connection_fd_ptr);
// 캐시 함수
void cache_init(cache_t *cache);
int cache_find(cache_t *cache, char *uri, char *data, int *size);
void cache_insert(cache_t *cache, char *uri, char *data, int size);
void cache_evict(cache_t *cache);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
cache_t cache;

int main(int argc, char **argv)
{
  int listen_fd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t client_len;
  struct sockaddr_storage client_addr;
  pthread_t tid;

  // 인자 개수가 알맞게 안 들어왔으면, 에러를 출력하고 종료
  if(argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 캐시 초기화
  cache_init(&cache);

  listen_fd = Open_listenfd(argv[1]);
  while(1)
  {
    client_len = sizeof(client_addr);
    int* connection_fd = Malloc(sizeof(int)); // 클라이언트와 연결된 소켓의 파일 디스크립터를 저장할 포인터 : 매 스레드마다 독립적인 메모리 공간 사용

    if(connection_fd == NULL)
    {
      fprintf(stderr, "Error: Unable to allocate memory for connection_fd\n");
      continue;
    }

    *connection_fd = Accept(listen_fd, (SA *)(&client_addr), &client_len);
    Getnameinfo((SA *)(&client_addr), client_len, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);

    Pthread_create(&tid, NULL, thread, connection_fd);
    // doit(connection_fd);
    // Close(connection_fd);
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

  // 캐시 버퍼 & 크기
  char cache_data_buffer[MAX_OBJECT_SIZE];
  int cache_data_size = 0;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /* 캐시에서 먼저 찾기 */
  if(cache_find(&cache, uri, cache_data_buffer, &cache_data_size))
  {
    // 캐시 히트
    Rio_writen(fd, cache_data_buffer, cache_data_size);
    return;
  }
  
  /* 캐시 미스 -> 서버에 요청 전달해서 응답 받아오기 */
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
  Rio_writen(server_fd, http_header, strlen(http_header)); // 재구성한 요청 헤더를 서버로 전송

  size_t temp = rio_readlineb(&server_rio, buf, MAXLINE);
  /* 서버 응답을 클라이언트에 전송하고 캐시에 저장 */
  while(temp != 0)
  {
    Rio_writen(fd, buf, temp);

    // 캐시 버퍼에 응답 저장
    if(cache_data_size + temp <= MAX_OBJECT_SIZE)
    {
      memcpy(cache_data_buffer + cache_data_size, buf, temp);
      cache_data_size += temp;
    }

    temp = rio_readlineb(&server_rio, buf, MAXLINE);
  }

  /* 캐시 저장 */
  if(cache_data_size <= MAX_OBJECT_SIZE)
  {
    cache_insert(&cache, uri, cache_data_buffer, cache_data_size);
  }

  /* 연결 종료 */
  Close(server_fd);
}

/* URI를 파싱해 호스트명, 경로, 포트 번호를 추출하고 대입 */
int parse_uri(char* uri, char* hostname, char* path, int* port)
{
  // default 설정 : 80 포트, 루트 경로 사용
  *port = 80;
  strcpy(path, "/");

  char* hostname_idx = strstr(uri, "//");
  if(hostname_idx != NULL)
  {
    hostname_idx = hostname_idx + 2;
  }
  else
  {
    hostname_idx = uri;
  }

  char* port_idx = strstr(hostname_idx, ":");
  char* path_idx = strstr(hostname_idx, "/");
  
  // 포트 번호가 있는 경우
  if(port_idx != NULL && (path_idx == NULL || port_idx < path_idx))
  {
    // hostname 추출
    *port_idx = '\0';
    strcpy(hostname, hostname_idx);
    *port_idx = ':';
    
    // 포트 번호 추출
    port_idx++;
    char* port_end_idx = strstr(port_idx, "/");
    // 포트 번호 뒤에 "/"가 있는 경우
    if(port_end_idx != NULL)
    {
      *port_end_idx = '\0';
      *port = atoi(port_idx);
      *port_end_idx = '/';
      strcpy(path, port_end_idx);
    }
    else
    {
      // 포트 번호는 있지만 경로가 없는 경우
      *port = atoi(port_idx);
      strcpy(path, "/");
    }
  }
  // 포트 번호가 없는 경우
  else
  {
    // 경로는 있는 경우
    if(path_idx != NULL)
    {
      // hostname 추출
      *path_idx = '\0';
      strcpy(hostname, hostname_idx);
      *path_idx = '/';
      strcpy(path, path_idx);
    }
    else
    {
      // 경로도 없는 경우
      strcpy(hostname, hostname_idx);
      strcpy(path, "/");
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
    if(!(strncasecmp(buf, "Connection", strlen("Connection"))) || !(strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))) || !(strncasecmp(buf, "User-Agent", strlen("User-Agent"))))
    {
      strcat(other_header, buf);
    }
  }

  // Host 헤더가 없는 경우 기본값 설정
  if(!strlen(host_header))
  {
    snprintf(host_header, sizeof(host_header), "Host: %s\r\n", hostname);
  }
  
  // 마지막에 헤더 조립
  snprintf(http_header, MAXLINE, "%s%s%s%s%s%s%s", request_header, host_header, "Connection: close\r\n", "Proxy-Connection: close\r\n", user_agent_hdr, other_header, "\r\n");

  return;
}

/* 스레드 함수 */
void* thread(void* connection_fd_ptr)
{
  int connection_fd = (*(int *)connection_fd_ptr);
  
  Pthread_detach(pthread_self()); // 스레드 분리 -> 자신의 메모리 자원들이 종료 후 반환될 수 있도록
  Free(connection_fd_ptr); // 동적 할당된 메모리 해제
  doit(connection_fd);
  Close(connection_fd);
  return NULL;
}

// ---------------------------------------------------------------------------------------------------------
/* 캐싱 프록시 함수들 */
void cache_init(cache_t *cache)
{
  cache->count = 0;
  cache->time = 0;
  pthread_mutex_init(&(cache->lock), NULL);
}

int cache_find(cache_t *cache, char *uri, char *data, int *size)
{
  pthread_mutex_lock(&(cache->lock));

  for(int i=0; i < cache->count; i++)
  {
    // 캐시 히트
    if(strcmp(cache->entries[i].uri, uri) == 0)
    {
      // 캐시된 데이터를 복사하고 대입
      memcpy(data, cache->entries[i].data, cache->entries[i].size);
      *size = cache->entries[i].size;
      cache->time += 1;
      cache->entries[i].timestamp = cache->time; // 캐시 사용 시간 업데이트(LRU 갱신)

      pthread_mutex_unlock(&(cache->lock));
      return 1;
    }
  }

  pthread_mutex_unlock(&(cache->lock));
  return 0; // 캐시 미스
}

void cache_insert(cache_t *cache, char *uri, char *data, int size)
{
  pthread_mutex_lock(&(cache->lock));

  // 데이터(객체) 사이즈가 너무 크면 삽입 안 하고 종료
  if(size > MAX_OBJECT_SIZE)
  {
    pthread_mutex_unlock(&(cache->lock));
    return;
  }

  // 최대 데이터 객체 개수보다 많으면 evict(축출) 수행
  if(cache->count >= MAX_OBJ_NUM)
  {
    cache_evict(cache);
  }

  // 새로운 엔트리 추가
  strcpy(cache->entries[cache->count].uri, uri);
  memcpy(cache->entries[cache->count].data, data, size);
  cache->entries[cache->count].size = size;
  cache->time += 1;
  cache->entries[cache->count].timestamp = cache->time;
  cache->count += 1;

  pthread_mutex_unlock(&(cache->lock));
  return;
}

void cache_evict(cache_t *cache)
{
  // LRU(Least Recently Used) 알고리즘(= 가장 오랫동안 참조되지 않은 부분을 교체하는 알고리즘)에 따라 엔트리 제거
  int lru = 0;

  // 제거할 엔트리 찾아내기(순차탐색)
  for(int i=1; i < cache->count; i++)
  {
    if(cache->entries[i].timestamp < cache->entries[lru].timestamp)
    {
      lru = i;
    }
  }

  // 엔트리 제거 및 앞으로 당기기
  for(int i = lru; i < cache->count - 1; i++)
  {
    cache->entries[i] = cache->entries[i + 1];
  }

  cache->count -= 1;
}