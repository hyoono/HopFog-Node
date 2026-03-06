#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>

/// Set up the web server with mobile API endpoints.
void setupWebServer(AsyncWebServer& server);

/// Register all API endpoint handlers.
void registerApiHandlers(AsyncWebServer& server);

#endif // WEB_SERVER_H
