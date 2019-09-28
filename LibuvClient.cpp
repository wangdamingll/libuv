// LibuvClient.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uv.h>
#include <string>
#include <sstream>
using namespace std;

#define DEFAULT_PORT 7000
#define DEFAULT_BACKLOG 128

typedef struct {
	uv_write_t req;
	uv_buf_t buf;
} write_req_t;

typedef struct {
	uv_timer_t* timer_req;
} uv_close_req_t;

void free_write_req(uv_write_t *req) {
	write_req_t *wr = (write_req_t*)req;
	free(wr->buf.base);
	free(wr);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
	memset(buf->base, 0, buf->len);
}

void on_close(uv_handle_t* handle) {
	free(handle);
}

void echo_write(uv_write_t *req, int status) {
	if (status) {
		fprintf(stderr, "Write error %s\n", uv_strerror(status));
	}
	free_write_req(req);
}

void echo_read(uv_stream_t *req, ssize_t nread, const uv_buf_t *buf) {
	if (nread > 0) {
		std::cout << "read::msg=["<< buf->base  <<"]"<< std::endl;
		free(buf->base);
		srand((unsigned int)time(NULL));
		if (rand() % 10 == 0) {
			uv_close_req_t* uv_close_rq = (uv_close_req_t*)req->data;
			uv_close((uv_handle_t*)(uv_close_rq->timer_req), on_close);
			free(uv_close_rq);
			uv_close((uv_handle_t*)req, on_close);
		}
		return;
	}
	if (nread < 0) {
		if (nread != UV_EOF) {
			fprintf(stderr, "Read error %s\n", uv_err_name(nread));
		}
		else {
			fprintf(stderr, "Connect close:: %s\n", uv_err_name(nread));
		}
		free(buf->base);
		uv_close_req_t* uv_close_rq = (uv_close_req_t*)req->data;
		uv_close((uv_handle_t*)(uv_close_rq->timer_req), on_close);
		free(uv_close_rq);
		uv_close((uv_handle_t*)req, on_close);
		return;
	}

	free(buf->base);
}

void timer_fun_cb (uv_timer_t* timer_req) {
	write_req_t *write_req = (write_req_t*)malloc(sizeof(write_req_t));
	stringstream iss;
	iss << uv_thread_self() << ":hello";
	std::cout << "send::msg=[" << iss.str().c_str()<<"]" << std::endl;
	char* msg = (char*)malloc(iss.str().length()+1);
	memset(msg, 0, iss.str().length() + 1);
	memcpy(msg, iss.str().c_str(), iss.str().length());
	write_req->buf = uv_buf_init(msg, iss.str().length());
	uv_write((uv_write_t*)write_req, (uv_stream_t*)timer_req->data, &write_req->buf, 1, echo_write);
	uv_read_start((uv_stream_t*)timer_req->data, alloc_buffer, echo_read);
}

void on_connect(uv_connect_t* req, int status) {
	if (status < 0) {
		fprintf(stderr, "New connection error %s\n", uv_strerror(status));
		return;
	}

	uv_timer_t* timer_req = (uv_timer_t*)malloc(sizeof(uv_timer_t));
	timer_req->data = (void*)req->handle;

	uv_close_req_t* uv_close_rq = (uv_close_req_t*)malloc(sizeof(uv_close_req_t));
	uv_close_rq->timer_req = timer_req;
	req->handle->data = (void*)uv_close_rq;
	uv_timer_init((uv_loop_t *)req->data, timer_req);
	uv_timer_start(timer_req, timer_fun_cb, 2000, 100);
}


void client_thread(void *arg) {
	int i = *(int*)arg;
	std::cout << "client_thread::client_thread_id=[" << uv_thread_self() << "] i=["<<i<<"]" << std::endl;

	uv_loop_t *client_loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
	uv_loop_init(client_loop);

	uv_tcp_t* client_socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(client_loop, client_socket);
	uv_connect_t* connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));
	connect->data = (void*)client_loop;
	struct sockaddr_in dest;
	uv_ip4_addr("localhost", DEFAULT_PORT, &dest);
	uv_tcp_connect(connect, client_socket, (const struct sockaddr*)&dest, on_connect);

	uv_run(client_loop, UV_RUN_DEFAULT);
	uv_loop_close(client_loop);
	free(client_loop);
	free(connect);
}

#define  NUM (2)
int main() {
	std::cout << "main::thread_id=[" << uv_thread_self() << "]" << std::endl;
	uv_thread_t client_thread_id[NUM];
	for (int i = 0; i < NUM; i++) {
		uv_thread_create(&client_thread_id[i], client_thread, &i);
		Sleep(1000);
	}

	for (int i = 0; i < NUM; i++) {
		uv_thread_join(&client_thread_id[i]);
	}
	return 0;
}
