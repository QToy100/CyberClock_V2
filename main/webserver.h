#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_
#include <esp_http_server.h>



void StartWebServer(httpd_handle_t server = nullptr);
void StopWebServer();

#endif // _WEBSERVER_H_
