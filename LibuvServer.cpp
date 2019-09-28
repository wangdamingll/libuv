#include "pch.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sstream>
#include <uv.h>
using namespace std;

uv_async_t async;
uv_rwlock_t rwlock;
uv_loop_t *listen_loop;
struct sockaddr_in addr;

typedef struct {
	uv_write_t req;
	uv_buf_t buf;
} write_req_t;

typedef struct {
	int thread_index;
	uv_loop_t* client_loop;
} thread_info_t;

int fds[2] = { 0 };

#ifndef WIN32
#define MULTI_THREAD_PIP_TEST
#endif 

#define MULTI_THREAD_SERVER

#define NUM (2)
uv_loop_t* client_loop[NUM] = { 0 };

#define DEFAULT_PORT 7000
#define DEFAULT_BACKLOG 128

void init_uv_loop(int index) {
	uv_rwlock_wrlock(&rwlock);
	client_loop[index] = (uv_loop_t*)malloc(sizeof(uv_loop_t));
	uv_rwlock_wrunlock(&rwlock);
}

uv_loop_t* get_uv_loop(int index) {
	uv_rwlock_rdlock(&rwlock);
	uv_loop_t* loop = client_loop[index];
	uv_rwlock_rdunlock(&rwlock);
	return loop;
}

void free_uv_loop(int index) {
	uv_rwlock_wrlock(&rwlock);
	client_loop[index] = NULL;
	uv_rwlock_wrunlock(&rwlock);
}

void free_write_req(uv_write_t *req) {
	write_req_t *wr = (write_req_t*)req;
	free(wr->buf.base);
	free(wr);
}

void on_pip_write(uv_write_t *req, int status) {
	if (status) {
		fprintf(stderr, "Write error %s\n", uv_strerror(status));
	}
	free_write_req(req);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
	memset(buf->base, 0, buf->len);
}

void on_close(uv_handle_t* handle) {
	free(handle);
}

void on_echo_write(uv_write_t *req, int status) {
	if (status) {
		fprintf(stderr, "Write error %s\n", uv_strerror(status));
	}
	free_write_req(req);
}

void on_echo_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
	std::cout << "on_echo_read::thread_id=[" << uv_thread_self() << "]" << std::endl;
	if (nread > 0) {
		std::cout << "on_echo_read::buf=[" << buf->base << "]" << std::endl;
		write_req_t *req = (write_req_t*)malloc(sizeof(write_req_t));
		req->buf = uv_buf_init(buf->base, nread);
		uv_write((uv_write_t*)req, client, &req->buf, 1, on_echo_write);
		return;
	}
	if (nread < 0) {
		if (nread != UV_EOF) {
			fprintf(stderr, "Read error %s\n", uv_err_name(nread));
		}else{
			fprintf(stderr, "Connect close:: %s\n", uv_err_name(nread));
		}
		uv_close((uv_handle_t*)client, on_close);
	}
	free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
	std::cout << "on_new_connection::thread_id=[" << uv_thread_self() << "]" << std::endl;
	if (status < 0) {
		fprintf(stderr, "New connection error %s\n", uv_strerror(status));
		return;
	}

	uv_tcp_t *client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));

#ifdef MULTI_THREAD_SERVER
	srand((unsigned int)time(NULL));
	uv_loop_t* client_loop = get_uv_loop(rand() % NUM);
	if (client_loop == NULL) {
		fprintf(stderr, "get_uv_loop is null");
		return;
	}
	uv_tcp_init(client_loop, client);
#else
	uv_tcp_init(server->loop, client);
#endif
	if (uv_accept(server, (uv_stream_t*)client) == 0) {
		uv_read_start((uv_stream_t*)client, alloc_buffer, on_echo_read);
	}else {
		uv_close((uv_handle_t*)client, on_close);
	}
}

void on_pip_read(uv_stream_t *pip, ssize_t nread, const uv_buf_t *buf) {	
	if (nread > 0) {
		std::cout << "on_pip_read::buf=[" << buf->base << "]" << std::endl;
		//pop from task list and deal it

	}else if (nread < 0) {
		if (nread == UV_EOF) {
			std::cout << "on_pip_read::UV_EOF" << std::endl;
			uv_close((uv_handle_t *)&pip, on_close);
		}
	}
	// OK to free buffer as write_data copies it.
	if (buf->base)
		free(buf->base);
}


void client_thread(void *arg) {
	std::cout << "client_thread::client_thread_id=[" << uv_thread_self() << "]" << std::endl;
	thread_info_t* thread_info = (thread_info_t *)arg;
	uv_async_init(thread_info->client_loop, &async, NULL);

	uv_pipe_t* pip_read = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
	uv_pipe_init(thread_info->client_loop, pip_read, 0);
	uv_pipe_open(pip_read, fds[0]);
	uv_read_start((uv_stream_t*)pip_read, alloc_buffer, on_pip_read);
	
	uv_run(thread_info->client_loop, UV_RUN_DEFAULT);
	uv_loop_close(thread_info->client_loop);
	free(thread_info->client_loop);
	free_uv_loop(thread_info->thread_index);
	free(thread_info);
}

void timer_fun_cb(uv_timer_t* timer_req) {
	//add task list and pip write one char size to all client thread or client thread loop of socket registed in.
	//map<thread_id,list<task*> >

	write_req_t *write_req = (write_req_t*)malloc(sizeof(write_req_t));
	char* msg = (char*)malloc(2);
	memset(msg, 0, 2);
	write_req->buf = uv_buf_init(msg, 2);
	memcpy(write_req->buf.base, "1", 2);
	uv_write((uv_write_t*)write_req, (uv_stream_t*)timer_req->data, &write_req->buf, 1, on_pip_write);
}

void init_pip(){
#ifndef WIN32
	//linux create anonymous pip 
	if (pipe(fds)) {
		std::cout << "init_pip::pipe(fds)::error=["<<strerror(errno) << "]" << std::endl;
		return ;
	}

	int flagsR = fcntl(fds[0], F_GETFL, 0);
	if (flagsR == -1){
		std::cout << "init_pip::pipe(fds)::flagsR = fcntl(fds[0], F_GETFL, 0)::error=["<<strerror(errno) << "]" << std::endl;
		return ;
	}
	flagsR |= O_NONBLOCK;
	int retR = fcntl(fds[0], F_SETFL, flagsR);
	if (retR == -1) {
		std::cout << "init_pip::pipe(fds)::flagsR = fcntl(fds[0], F_SETFL, flagsR)::error=["<<strerror(errno) << "]" << std::endl;
		return ;
	}

	int flagsW= fcntl(fds[1], F_GETFL, 0);
	if (flagsW == -1) {
		std::cout << "init_pip::pipe(fds)::flagsR = fcntl(fds[1], F_GETFL, 0))::error=["<<strerror(errno) << "]" << std::endl;
		return ;
	}
	flagsW |= O_NONBLOCK;
	int retW = fcntl(fds[1], F_SETFL, flagsW);
	if (retW == -1) {
		std::cout << "init_pip::pipe(fds)::flagsR = fcntl(fds[1], F_SETFL, flagsW)::error=["<<strerror(errno) << "]" << std::endl;
		return ;
	}
#else 
	//windows create anonymous pip 
#endif // !WIN32
}

int main() {
	std::cout << "main::thread_id=[" << uv_thread_self() << "]" << std::endl;

#ifdef MULTI_THREAD_PIP_TEST
	//cread anonymous pip and notify client thread deal task list
	init_pip();
#endif

#ifdef MULTI_THREAD_SERVER
	uv_rwlock_init(&rwlock);
	uv_thread_t client_thread_id[NUM];
	for (int i = 0; i < NUM; i++) {
		init_uv_loop(i);
		uv_loop_t* loop = get_uv_loop(i);
		uv_loop_init(loop);
		thread_info_t* thread_info = (thread_info_t*)malloc(sizeof(thread_info_t));
		thread_info->client_loop = loop;
		thread_info->thread_index = i;
		uv_thread_create(&client_thread_id[i], client_thread, thread_info);
		#ifdef WIN32
			Sleep(1000);
		#else
			sleep(1);
		#endif
	}
#endif
	
	listen_loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
	uv_loop_init(listen_loop);

#ifdef MULTI_THREAD_PIP_TEST
	uv_pipe_t* pip_write = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));;
	uv_pipe_init(listen_loop, pip_write, 0);
	uv_pipe_open(pip_write, fds[1]);

	uv_timer_t* timer_req = (uv_timer_t*)malloc(sizeof(uv_timer_t));
	uv_timer_init(listen_loop, timer_req);
	timer_req->data = (void*)pip_write;
	uv_timer_start(timer_req, timer_fun_cb, 2000, 2000);
#endif

	uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(listen_loop, server);
	uv_ip4_addr("0.0.0.0", DEFAULT_PORT, &addr);
	uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);
	int r = uv_listen((uv_stream_t*)server, DEFAULT_BACKLOG, on_new_connection);
	if (r) {
		fprintf(stderr, "Listen error %s\n", uv_strerror(r));
		return 1;
	}

	uv_run(listen_loop, UV_RUN_DEFAULT);

#ifdef MULTI_THREAD_PIP_TEST
	uv_close((uv_handle_t*)(pip_write), on_close);
	uv_close((uv_handle_t*)(timer_req), on_close);
#endif

	uv_loop_close(listen_loop);
	free(listen_loop);

#ifdef MULTI_THREAD_SERVER
	for (int i = 0; i < NUM; i++) {
		uv_thread_join(&client_thread_id[i]);
	}
	uv_rwlock_destroy(&rwlock);
#endif
	return 0;
}
