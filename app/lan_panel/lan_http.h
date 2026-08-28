/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/lan_http.h
 ****************************************************************************/

#ifndef __LAN_HTTP_H
#define __LAN_HTTP_H

#include <stdbool.h>
#include <stddef.h>

struct lan_http_request_s
{
  char method[8];
  char path[256];
  char authorization[96];
  char body[2048];
  size_t body_length;
};

int lan_http_read_request(int fd, struct lan_http_request_s *request);
int lan_http_send_response(int fd, int status, const char *type,
                           const char *body, size_t length);
int lan_http_send_file(int fd, const char *path, const char *type);
void lan_http_close(int fd);

#endif /* __LAN_HTTP_H */
