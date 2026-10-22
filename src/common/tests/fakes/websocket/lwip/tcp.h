#ifndef TEST_WS_TCP_H
#define TEST_WS_TCP_H
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "pbuf.h"
using err_t = int;
using u16_t = uint16_t;
constexpr err_t ERR_OK = 0, ERR_MEM = -1, ERR_BUF = -2, ERR_RTE = -4,
                ERR_ABRT = -13, ERR_ARG = -16;
constexpr unsigned TCP_SND_BUF = 4096;
constexpr unsigned TCP_WRITE_FLAG_COPY = 1;
constexpr int IPADDR_TYPE_ANY = 0;
constexpr const void* IP_ADDR_ANY = nullptr;
struct tcp_pcb {
  void* arg = nullptr;
  err_t (*recv)(void*, tcp_pcb*, pbuf*, err_t) = nullptr;
  err_t (*sent)(void*, tcp_pcb*, u16_t) = nullptr;
  err_t (*poll)(void*, tcp_pcb*) = nullptr;
  void (*error)(void*, err_t) = nullptr;
  err_t (*accept)(void*, tcp_pcb*, err_t) = nullptr;
  err_t write_result = ERR_OK, output_result = ERR_OK, close_result = ERR_OK;
  unsigned outputs = 0, recved = 0, available = TCP_SND_BUF, poll_interval = 0;
  bool aborted = false, closed = false, nodelay = false;
  std::vector<std::vector<uint8_t>> writes;
};
inline tcp_pcb test_listener;
inline void tcp_arg(tcp_pcb* p, void* arg) { p->arg = arg; }
inline void tcp_recv(tcp_pcb* p, decltype(p->recv) cb) { p->recv = cb; }
inline void tcp_sent(tcp_pcb* p, decltype(p->sent) cb) { p->sent = cb; }
inline void tcp_err(tcp_pcb* p, decltype(p->error) cb) { p->error = cb; }
inline void tcp_accept(tcp_pcb* p, decltype(p->accept) cb) { p->accept = cb; }
inline void tcp_poll(tcp_pcb* p, decltype(p->poll) cb, unsigned interval) {
  p->poll = cb;
  p->poll_interval = interval;
}
inline tcp_pcb* tcp_new_ip_type(int) { test_listener = {}; return &test_listener; }
inline err_t tcp_bind(tcp_pcb*, const void*, uint16_t) { return ERR_OK; }
inline tcp_pcb* tcp_listen(tcp_pcb* p) { return p; }
inline void tcp_nagle_disable(tcp_pcb* p) { p->nodelay = true; }
inline void tcp_nagle_enable(tcp_pcb* p) { p->nodelay = false; }
inline void tcp_recved(tcp_pcb* p, u16_t bytes) { p->recved += bytes; }
inline err_t tcp_write(tcp_pcb* p, const void* data, u16_t size, unsigned flags) {
  assert(!p->aborted && !p->closed && flags == TCP_WRITE_FLAG_COPY);
  if (p->write_result != ERR_OK)
    return p->write_result;
  if (size > p->available)
    return ERR_MEM;
  p->available -= size;
  auto bytes = static_cast<const uint8_t*>(data);
  p->writes.emplace_back(bytes, bytes + size);
  return ERR_OK;
}
inline err_t tcp_output(tcp_pcb* p) {
  assert(!p->aborted && !p->closed);
  ++p->outputs;
  return p->output_result;
}
inline err_t tcp_close(tcp_pcb* p) {
  if (p->close_result == ERR_OK)
    p->closed = true;
  return p->close_result;
}
inline void tcp_abort(tcp_pcb* p) {
  assert(!p->aborted && !p->closed);
  p->aborted = true;
  if (p->error)
    p->error(p->arg, ERR_ABRT);
}
#endif
