#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cdecode.h"
 
#define URL_SIZE 100
#define MAX_EMAIL_SIZE 65535
#define MAX_BODY_SIZE 100000

struct memory {
   char *response;
   size_t size;
};
// Header/body callback function 
static size_t mail_data(void *data, size_t size, size_t nmemb, void *userp) {
   size_t realsize = size * nmemb;
   struct memory *mem = (struct memory *)userp;
 
   char *ptr = realloc(mem->response, mem->size + realsize + 1);
   if(ptr == NULL)
     return 0; 
 
   mem->response = ptr;
   memcpy(&(mem->response[mem->size]), data, realsize);
   mem->size += realsize;
   mem->response[mem->size] = 0;
 
   return realsize;
}
// Get UIDs in mailbox folder
void get_UIDs(char *response, char *data) {
    char *loc = strstr(response, "SEARCH");
    loc += 7;
    loc[strcspn(loc, "\r\n")] = 0;
    strcpy(data, loc);
}
// Get header of email
void get_headers(char *response, char *data) {
    char *loc = strstr(response, "Date:");
    char *loc2 = strstr(response, "To:");
    loc2[strcspn(loc2, "\r\n")] = 0;
    strcpy(data, loc);
}
// Decode body
void decode(char *input, char *data) {
	char* output = (char*)malloc(MAX_BODY_SIZE);
	char* c = output;
	int cnt = 0;
	base64_decodestate s;
	base64_init_decodestate(&s);
	cnt = base64_decode_block(input, strlen(input), c, &s);
	c += cnt;
	*c = 0;
  strcpy(data, output);
  free(output);
}
// Get body of email
void get_body(char *response, char *data) {
    char *loc = strstr(response, "FETCH (UID");
    loc += strcspn(loc, "\n") + 1;
    char *loc2 = strstr(response, "==)");
    *loc2 = '\0';
    decode(loc, data);
}
// Fetch mail
int request(char *username, char *password, char *folder, int opt, int uid, char *data) {
  CURL *curl;
  char server[] = "imaps://imap.gmail.com:993";
  char url[URL_SIZE];
  char request[URL_SIZE];
  CURLcode res = CURLE_OK;
  struct memory headers = {0};
  struct memory body = {0};
 
  curl = curl_easy_init();
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    snprintf(url, URL_SIZE, "%s%s", server, folder);
    curl_easy_setopt(curl, CURLOPT_URL, url);

    switch(opt) {
        case 0:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "UID SEARCH ALL");
            break;
        case 1:
            snprintf(request, URL_SIZE, "UID FETCH %d BODY[HEADER.FIELDS (DATE SUBJECT TO FROM)]", uid);
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request);
            break;
        case 2:
            snprintf(request, URL_SIZE, "UID FETCH %d BODY[1]", uid);
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request);
            break;
        default: break;
    }
 
    #ifdef SKIP_PEER_VERIFICATION
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    #endif
 
    #ifdef SKIP_HOSTNAME_VERIFICATION
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    #endif

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mail_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&body);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&headers);

    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
 
    res = curl_easy_perform(curl);
 
    if(res != CURLE_OK) {
        curl_easy_cleanup(curl);
        return 0;
    }

    // printf("Headers:%s\n\n", headers.response ? headers.response : "");
    // printf("Body:%s\n\n", body.response ? body.response : "");

    switch(opt) {
        case 0:
            get_UIDs(body.response, data);
            break;
        case 1:
            get_headers(headers.response, data);
            break;
        case 2:
            get_body(headers.response, data);
            break;
        default: break;
    }

    curl_easy_cleanup(curl);
  }
  return 1;
}
 
char payload_text[MAX_EMAIL_SIZE];
 
struct upload_status {
  size_t bytes_read;
};
 
static size_t payload_source(char *ptr, size_t size, size_t nmemb, void *userp) {
  struct upload_status *upload_ctx = (struct upload_status *)userp;
  const char *data;
  size_t room = size * nmemb;
 
  if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
    return 0;
  }
 
  data = &payload_text[upload_ctx->bytes_read];
 
  if(*data) {
    size_t len = strlen(data);
    if(room < len)
      len = room;
    memcpy(ptr, data, len);
    upload_ctx->bytes_read += len;
 
    return len;
  }
 
  return 0;
}
// Send mail 
int send_mail(char *username, char *password, char *subject, char *to, char *body) {
  CURL *curl;
  char server[] = "smtp://smtp.gmail.com:587/";
  struct curl_slist *recipients = NULL;
  struct upload_status upload_ctx = {0};
  CURLcode res = CURLE_OK;

  curl = curl_easy_init();
  if(curl) {
    snprintf(payload_text, MAX_EMAIL_SIZE, "To: %s\r\n"
                                                "From: %s\r\n"
                                                "Subject: %s\r\n"
                                                "\r\n" 
                                                "%s\r\n", to, username, subject, body);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    
    curl_easy_setopt(curl, CURLOPT_URL, server);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username);
    recipients = curl_slist_append(recipients, to);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
 
    res = curl_easy_perform(curl);
 
    if(res != CURLE_OK) {
      curl_slist_free_all(recipients);
      curl_easy_cleanup(curl);
      return 0;
    }
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
  }
   return 1;
}