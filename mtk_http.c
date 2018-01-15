#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include "mtk_camera.h"

#define HTTP_PORT	8899
#define SERFD_NUM	3 /*wireless/wire/127*/
#define BUFFER_SIZE 4096


enum{
	HTTP_RUN = 0,
	HTTP_STOP = 1,
};
#define STD_HEADER "Connection: close\r\n" \
    "Server: MJPG-Streamer/0.2\r\n" \
    "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
    "Pragma: no-cache\r\n" \
    "Expires: Mon, 3 Jan 2000 12:34:56 GMT\r\n"

    
	/* the boundary is used for the M-JPEG stream, it separates the multipart stream of pictures */
#define BOUNDARY "boundarydonotcross"

typedef struct _httpCont{
	int port;
	uint8_t runState;
	pthread_t threadHttp;
	int servFd[SERFD_NUM];
}httpContext;

httpContext httpCon;

void send_error(int fd, int which, char *message)
{
	char buffer[BUFFER_SIZE] = {0};

	if(which == 401) {
		sprintf(buffer, "HTTP/1.0 401 Unauthorized\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"WWW-Authenticate: Basic realm=\"MJPG-Streamer\"\r\n" \
			"\r\n" \
			"401: Not Authenticated!\r\n" \
			"%s", message);
	} else if(which == 404) {
		sprintf(buffer, "HTTP/1.0 404 Not Found\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"\r\n" \
			"404: Not Found!\r\n" \
			"%s", message);
	} else if(which == 500) {
		sprintf(buffer, "HTTP/1.0 500 Internal Server Error\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"\r\n" \
			"500: Internal Server Error!\r\n" \
			"%s", message);
	} else if(which == 400) {
		sprintf(buffer, "HTTP/1.0 400 Bad Request\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"\r\n" \
			"400: Not Found!\r\n" \
			"%s", message);
	} else if (which == 403) {
		sprintf(buffer, "HTTP/1.0 403 Forbidden\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"\r\n" \
			"403: Forbidden!\r\n" \
			"%s", message);
	} else {
		sprintf(buffer, "HTTP/1.0 501 Not Implemented\r\n" \
			"Content-type: text/plain\r\n" \
			STD_HEADER \
			"\r\n" \
			"501: Not Implemented!\r\n" \
			"%s", message);
	}
	write(fd, buffer, strlen(buffer));
}

void *clientThread(void *arg)
{
	int cfd = *((int*)arg);
	char buf[BUFFER_SIZE];
	char response[BUFFER_SIZE];
	char HttpCommand[16];
	char HttpUrl[4096] = {0};
	char *p;
	char *framePtr = NULL;
	uint32_t memSize = 0;
	int ret, i;
	struct timeval tm;

	DBG("Client FD:%d\n", cfd);

	if(cfd <= 0){
		return NULL;
	}
	memset(buf, 0, sizeof(buf));
	ret = recv(cfd, buf, sizeof(buf)-1, 0); //recv size must be buf_size-1, if not strlen may be incorrect 20150623
	if( ret == -1 || ret == 0){
		DBG("recv error or 0!\n");/* sometimes error here */
		close(cfd);
		return NULL;
	}
	if(strstr(buf, "\r\n\r\n") == NULL){
		DBG("Request To Big.\n");
		send_error(cfd, 500, "Request To Big");
		close(cfd);
		return NULL;
	}
	/*
	 * process http header, get http command, 
	 * http url, http version
	 */
	memset(HttpCommand, 0, sizeof(HttpCommand));
	memset(HttpUrl, 0, sizeof(HttpUrl));
	p = buf;
	for(i = 0; i<15 && *p != ' ' && *p != '\r'; i++)
		HttpCommand[i] = *(p++);
	HttpCommand[i] = '\0';
	DBG("HttpCommand=%s\n", HttpCommand);
	while(*p==' ')
		p++;
	if(strncmp(p, "http://", 7) == 0){
		p = p+7;
		while(*p!='/')
			p++;
	}
	for(i = 0; i<4095 && *p != ' ' && *p != '\r'; i++)
		HttpUrl[i] = *(p++);
	HttpUrl[i] = '\0';
	DBG("HttpUrl=%s\n", HttpUrl);

	if(strstr(HttpUrl, "?streaming") == NULL){
		DBG("Bad Request\n");
		send_error(cfd, 400, "Bad Request");
		close(cfd);
		return NULL;
	}
	memset(response, 0, sizeof(response));
    sprintf(response, "HTTP/1.0 200 OK\r\n" \
            "Access-Control-Allow-Origin: *\r\n" \
            STD_HEADER \
            "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n" \
            "\r\n" \
            "--" BOUNDARY "\r\n");
	write(cfd, response, strlen(response));

	while(httpCon.runState == HTTP_RUN){
		char *tmpFrame = NULL;
		uint32_t frameSize = 0;
		char frameType[32] = {0};
		
		mtkGetVideoLock();
		mtkGetVideoFrame(&tmpFrame, &frameSize, frameType, &tm);
		if(frameSize > memSize){
			framePtr = realloc(framePtr, frameSize+512);
			if(framePtr == NULL){
				DBG("Http Calloc Memory Failed\n");
				mtkGetVideoUnLock();
				break;
			}
			memSize = frameSize+512;
		}
		if(frameSize){
			memcpy(framePtr, tmpFrame, frameSize);
		}
		mtkGetVideoUnLock();
		if(!frameSize){
			continue;
		}
		memset(response, 0, sizeof(response));
		sprintf(response, "Content-Type: image/%s\r\n" \
			"Content-Length: %d\r\n" \
			"X-Timestamp: %d.%06d\r\n" \
			"\r\n", frameType, frameSize, (int)tm.tv_sec, (int)tm.tv_usec);
        if(write(cfd, response, strlen(response)) < 0){
			DBG("Write Failed1:%s\n", strerror(errno));
			break;
		}
        if(write(cfd, framePtr, frameSize) < 0){
			DBG("Write Failed2:%s\n", strerror(errno));
			break;
		}
		DBG("[%ld]%s: Frame Size %d\n", time(NULL), frameType, frameSize);		
        sprintf(response, "\r\n--" BOUNDARY "\r\n");
        if(write(cfd, response, strlen(response)) < 0){
			DBG("Write Failed3:%s\n", strerror(errno));
			break;
		}		
	}

	close(cfd);
	if(framePtr){
		free(framePtr);
	}
	return NULL;	
}

void *httpThread(void *arg)
{
	httpContext *ptrHttp = (httpContext *)arg;
	pthread_t threadCli;
	struct addrinfo *aip, *aip2;
	struct addrinfo hints;
	struct sockaddr_storage client_addr;
	socklen_t addr_len = sizeof(struct sockaddr_storage);
	fd_set selectfds;
    char name[NI_MAXHOST];
	int i, on, ret;
	struct timeval tv;
    int max_fds = 0, clienfd;
	
	for(i = 0; i < SERFD_NUM; i++){
		ptrHttp->servFd[i] = -1;
	}
	bzero(&hints, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(name, sizeof(name), "%d", ptrHttp->port);
	if(getaddrinfo(NULL, name, &hints, &aip) != 0){
		DBG("Getaddrinfo Failed:%s\n", strerror(errno));
		return NULL;
	}
    for(i=0, aip2 = aip; aip2 != NULL; aip2 = aip2->ai_next) {
		ptrHttp->servFd[i] = socket(aip2->ai_family, aip2->ai_socktype, 0);
		if(ptrHttp->servFd[i] < 0){
			continue;
		}
		on = 1;
		if(setsockopt(ptrHttp->servFd[i], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
			DBG("SO_REUSEADDR Failed:%s\n", strerror(errno));
		}
        on = 1;
		if(aip2->ai_family == AF_INET6 && setsockopt(ptrHttp->servFd[i], IPPROTO_IPV6, IPV6_V6ONLY,
						(const void *)&on , sizeof(on)) < 0) {
			DBG("setsockopt(IPV6_V6ONLY) Failed:%s\n", strerror(errno));
		}
		if(bind(ptrHttp->servFd[i], aip2->ai_addr, aip2->ai_addrlen) < 0){			
			DBG("Bind Failed:%s\n", strerror(errno));
			ptrHttp->servFd[i] = -1;
			continue;
		}
		if(listen(ptrHttp->servFd[i], 10) < 0) {
			DBG("Listen Failed:%s\n", strerror(errno));
			ptrHttp->servFd[i] = -1;
		}else{
			i++;
			if(i >= SERFD_NUM) {
				DBG("Full Http..\n");
				i--;
				break;
			}
		}
    }
	freeaddrinfo(aip);
	if(i < 1){
		DBG("Http Server Thread Start Faild\n");
		return NULL;
	}

	while(ptrHttp->runState == HTTP_RUN){
		FD_ZERO(&selectfds);
		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;		
		max_fds = 0;
		for(i = 0; i < SERFD_NUM; i++) {
			if(ptrHttp->servFd[i] != -1){
				FD_SET(ptrHttp->servFd[i], &selectfds);
				if(ptrHttp->servFd[i] > max_fds){
					max_fds = ptrHttp->servFd[i];
				}
			}
		}
		ret = select(max_fds + 1, &selectfds, NULL, NULL, &tv);
		if(ret == -1){
			if(errno == EINTR){
				continue;
			}
			DBG("Select Error:%s\n", strerror(errno));
			goto HQUIT;
		}else if(ret == 0){
			continue;
		}
		
        for(i = 0; i < max_fds + 1; i++) {
            if(ptrHttp->servFd[i] != -1 && FD_ISSET(ptrHttp->servFd[i], &selectfds)){
				clienfd = accept(ptrHttp->servFd[i], (struct sockaddr *)&client_addr, &addr_len);
				if(getnameinfo((struct sockaddr *)&client_addr, addr_len, name, sizeof(name), NULL, 0, NI_NUMERICHOST) == 0){
					DBG("serving client: %s\n", name);
				}
				if(pthread_create(&threadCli, NULL, &clientThread, (void *)&clienfd) != 0) {
					DBG("Start Client Thread Failed\n");
					close(clienfd);
					continue;
				}
				pthread_detach(threadCli);
            }
        }
		
	}

HQUIT:
	for(i = 0; i < SERFD_NUM; i++) {
		if(ptrHttp->servFd[i] != -1){
			close(ptrHttp->servFd[i]);
		}
	}
	return NULL;
}

int mktHttpInit(int port)
{
	memset(&httpCon, 0, sizeof(httpCon));

	httpCon.port = (port == 0?HTTP_PORT:port);
	DBG("Http Port:%d\n", httpCon.port);
    pthread_create(&(httpCon.threadHttp), NULL, httpThread, (void*)&httpCon);

	return 0;
}

int mktHttpUnInit(void)
{
	httpCon.runState = HTTP_STOP;
	mtkBroadcastVideoLock();
	if(httpCon.threadHttp){
		pthread_join(httpCon.threadHttp, NULL);
	}

	return 0;
}

