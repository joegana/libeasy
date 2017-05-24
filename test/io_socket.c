#include <stdio.h>
#include <pthread.h>
#include "easy_io.h"

static easy_thread_pool_t *workers;
typedef struct {
	int len;
	unsigned char *data;
} msg_t;

static int on_connect(easy_connection_t *c) {
	char buf[21] = {0};
	easy_inet_addr_to_str(&c->addr, buf, sizeof(buf) - 1);
	printf("connected: %s\n", buf);
	return 0;
}

static int on_disconnect(easy_connection_t *c) {
	char buf[21] = {0};
	easy_inet_addr_to_str(&c->addr, buf, sizeof(buf) - 1);
	printf("disconnected: %s\n", buf);
	return 0;
}

static int on_cleanup(easy_request_t *r, void *apacket) {
	printf("clean up\n");
	return 0;
}

static int on_process(easy_request_t *r) {
	easy_thread_pool_push(workers, r, easy_hash_key((uint64_t)(long)r));
	return EASY_AGAIN;
}

static void *on_decode(easy_message_t *m) {
	msg_t *packet;
	int len = m->input->last - m->input->pos;
	int i = 0, pos = 0;
	printf("len: %d\n", len);
	if ((packet = (msg_t *)easy_pool_alloc(m->pool,
				  len + sizeof(msg_t))) == NULL) {
		m->status = EASY_ERROR;
		return NULL;
	}
	packet->data = (unsigned char *)(packet + 1);
	memcpy(packet->data, m->input->pos, len);
	for (i = 0; i < len; i ++) {
		if (m->input->pos[i] == 0x0a) {
			break;
		}
		if (m->input->pos[i] == 0x0d) continue;
		packet->data[pos ++] = m->input->pos[i];
	}
	packet->len = pos;
	m->input->pos += i + 1;
	return packet;
}

static int on_encode(easy_request_t *r, void *packet) {
	msg_t *msg = (msg_t *)packet;
	easy_buf_t *b = easy_buf_pack(r->ms->pool, msg->data, msg->len);
	easy_request_addbuf(r, b);
	return 0;
}

//异步处理请求
static int async_request_process(easy_request_t *r, void *args) {
	msg_t *msg = (msg_t *)r->ipacket;
	msg_t *out;
	int len = 0;
	char buf[msg->len + 20];
	if (msg->len == 0) {
		return EASY_OK;
	}
	if (msg->len == 4 && strncmp(msg->data, "quit", 4) == 0) {
		//quit
		easy_connection_destroy(r->ms->c);
		strcpy(buf, "bye!\n");
		len = strlen(buf);
	} else {
		sprintf(buf, "recv(%d): ", msg->len);
		char *p = buf + strlen(buf);
		for (int i = 0; i < msg->len; i ++) {
			*(p ++) = msg->data[i];
		}
		*(p ++) = 0x0a;
		len = p - buf;
	}
	out = easy_pool_alloc(r->ms->pool, sizeof(msg_t) + len);
	out->len = len;
	out->data = (unsigned char *)(out + 1);
	memcpy(out + 1, buf, len);
	r->opacket = out;
    return EASY_OK;
}


/*
 * 数据流：<in_data> -> decode -> process -> encode -> <out_data> -> cleanup
 * 本例实现了一个类echo server，输入可打印字符串，输出recv: <string>
 */
int main() {
	easy_log_level = EASY_LOG_ALL;
	easy_io_handler_pt handler = {0};
	handler.on_connect = on_connect;
	handler.on_disconnect = on_disconnect;
	handler.process = on_process;
	handler.cleanup = on_cleanup;
	handler.decode = on_decode;
	handler.encode = on_encode;
	easy_io_create(1);
	workers = easy_request_thread_create(100, async_request_process, NULL);
	easy_io_add_listen("::1", 12345, &handler);
	easy_io_start();
	// 起处理速度统计定时器
	ev_timer stat_watcher;
	easy_io_stat_t iostat;
	easy_io_stat_watcher_start(&stat_watcher, 5.0, &iostat, NULL);
	//wait
	easy_io_wait();
	easy_io_destroy();
	return 0;
}
