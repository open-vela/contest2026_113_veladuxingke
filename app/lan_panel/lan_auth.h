/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/lan_auth.h
 ****************************************************************************/

#ifndef __LAN_AUTH_H
#define __LAN_AUTH_H

#include <stddef.h>

int lan_auth_load(char *token, size_t size);
int lan_auth_check(const char *token, size_t length);

#endif /* __LAN_AUTH_H */
