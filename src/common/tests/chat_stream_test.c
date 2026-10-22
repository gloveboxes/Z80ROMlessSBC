#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../chat_stream.h"

static z80_chat_stream_t stream;
static char output[20000], wire[24000];
static size_t output_length, allowance;
static const char headers[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\n";
static const char token[] =
    "data: {\"choices\":[{\"delta\":{\"content\":\"Hi\\n\\t\\u0041"
    "\\u2014\\ud83d\\ude00 \xe2\x80\x9cQ\xe2\x80\x9d\"}}]}\r\n\r\n";
static const char done[] = "data: [DONE]\n\n";

static bool emit(uint8_t byte) {
  if (!allowance)
    return false;
  --allowance;
  assert(output_length + 1 < sizeof(output));
  output[output_length++] = (char)byte;
  output[output_length] = '\0';
  return true;
}

static void reset(void) {
  z80_chat_stream_init(&stream);
  output_length = 0;
  output[0] = '\0';
  allowance = sizeof(output) - 1;
}

static void feed(const char *text, size_t fragment) {
  size_t length = strlen(text), position = 0;
  while (position < length && !stream.done && !stream.error) {
    size_t count = length - position;
    if (count > fragment) count = fragment;
    size_t used = z80_chat_stream_feed(&stream, (const uint8_t *)text + position, count, emit);
    assert(used || !allowance);
    position += used;
    if (!allowance)
      break;
  }
}

static void rejected(const char *text) {
  reset();
  feed(text, 1);
  assert(stream.error && !stream.done);
}

int main(void) {
  for (size_t fragment = 1; fragment < 40; ++fragment) {
    reset();
    snprintf(wire, sizeof(wire), "%sContent-Length: %zu\r\n\r\n%s%s",
             headers, strlen(token) + strlen(done), token, done);
    feed(wire, fragment);
    assert(stream.done && !stream.error);
    assert(strcmp(output, "Hi\n\tA-? \"Q\"") == 0);
    reset();
    snprintf(wire, sizeof(wire),
        "%sTransfer-Encoding: chunked\r\n\r\n%zx;foo=bar\r\n%s\r\n%zx\r\n%s\r\n0\r\n\r\n",
        headers, strlen(token), token, strlen(done), done);
    feed(wire, fragment);
    assert(stream.done && !stream.error && strcmp(output, "Hi\n\tA-? \"Q\"") == 0);
  }
  reset();
  snprintf(wire, sizeof(wire), "%s\r\n%s%s", headers, token, done);
  allowance = 2;
  size_t used = z80_chat_stream_feed(&stream, (const uint8_t *)wire, strlen(wire), emit);
  assert(output_length == 2 && used < strlen(wire) && !stream.done);
  allowance = sizeof(output) - 1;
  z80_chat_stream_feed(&stream, (const uint8_t *)wire + used, strlen(wire) - used, emit);
  assert(stream.done && strcmp(output, "Hi\n\tA-? \"Q\"") == 0);

  reset();
  snprintf(wire, sizeof(wire), "%s\r\n"
      ": keepalive\n\n"
      "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"hidden content\","
      "\"tool_calls\":[{\"function\":{\"arguments\":\"\\\"content\\\":\\\"wrong\\\"\"}}],"
      "\"content\":\"right\"}},{\"delta\":{\"content\":\"wrong\"}}]}\n\n"
      "data: {\"choices\":[],\"usage\":{\"total_tokens\":12}}\n\n%s", headers, done);
  feed(wire, 7);
  assert(stream.done && !stream.error && strcmp(output, "right") == 0);
  rejected("HTTP/1.1 401 Unauthorized\r\n\r\n");
  rejected("HTTP/1.1 429 Too Many Requests\r\n\r\n");
  rejected("HTTP/1.1 500 Error\r\n\r\n");
  rejected("HTTP/1.1 20 OK\r\n\r\n");
  rejected("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{}");
  rejected("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Encoding: gzip\r\n\r\n");
  snprintf(wire, sizeof(wire), "%sContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n", headers);
  rejected(wire);
  snprintf(wire, sizeof(wire), "%sContent-Length: 1\r\n\r\nx", headers);
  rejected(wire);
  snprintf(wire, sizeof(wire), "%sTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", headers);
  rejected(wire);
  snprintf(wire, sizeof(wire), "%sTransfer-Encoding: chunked\r\n\r\nZ\r\n", headers);
  rejected(wire);
  snprintf(wire, sizeof(wire), "%sTransfer-Encoding: chunked\r\n\r\n1\r\nx!\n", headers);
  rejected(wire);
  const char *invalid[] = {
    "{", "{\"error\":{\"message\":\"do not echo provider secrets\"}}",
    "{\"choices\":[{\"delta\":{\"content\":\"bad\\uZZZZ\"}}]}",
    "{\"choices\":[{\"delta\":{\"content\":\"bad\\ud800\"}}]}",
    "{\"choices\":[{\"delta\":{\"content\":\"bad\\q\"}}]}",
    "{\"choices\":[{\"delta\":{\"content\":\"bad\xc0\x80\"}}]}",
    "{\"choices\":[{\"delta\":{\"content\":\"a\",\"content\":\"b\"}}]}",
    "{\"choices\":[{\"delta\":{\"content\":\"bad\"}}]} trailing",
    "{\"number\":01}", "{\"number\":1e}", "{\"key\":true,}",
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    snprintf(wire, sizeof(wire), "%s\r\ndata: %s\n\n", headers, invalid[i]);
    rejected(wire);
    assert(output_length == 0);
  }
  for (unsigned nesting = 15; nesting <= 16; ++nesting) {
    reset();
    size_t position = (size_t)snprintf(wire, sizeof(wire), "%s\r\ndata: {\"meta\":", headers);
    for (unsigned i = 0; i < nesting; ++i) wire[position++] = '[';
    wire[position++] = '0';
    for (unsigned i = 0; i < nesting; ++i) wire[position++] = ']';
    snprintf(wire + position, sizeof(wire) - position, "}\n\n%s", done);
    feed(wire, 1);
    assert(nesting == 15 ? stream.done && !stream.error : stream.error && !stream.done);
  }
  reset();
  feed(headers, 1);
  feed("\r\n", 1);
  wire[0] = ':';
  memset(wire + 1, 'x', Z80_CHAT_LINE_MAX - 2);
  wire[Z80_CHAT_LINE_MAX - 1] = '\n';
  wire[Z80_CHAT_LINE_MAX] = '\0';
  feed(wire, 1);
  feed(done, 1);
  assert(stream.done && !stream.error);
  reset();
  feed(headers, 1);
  feed("\r\n", 1);
  memset(wire, 'x', Z80_CHAT_LINE_MAX);
  wire[Z80_CHAT_LINE_MAX] = '\0';
  feed(wire, 1);
  assert(stream.error);
  reset();
  feed("HTTP/1.1 200 OK\r\nX: ", 1);
  feed(wire, 1);
  assert(stream.error);
  puts("PASS: fragmented HTTP/chunks/SSE, bounded backpressure, strict JSON and CP/M text");
}
