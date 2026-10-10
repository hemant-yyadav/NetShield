/*
  proxy_parse.c -- a HTTP Request Parsing Library.
  COS 461  
*/

#include "proxy_parse.h"

#define DEFAULT_NHDRS 8
#define MAX_REQ_LEN 65535
#define MIN_REQ_LEN 4

static const char *root_abs_path = "/";

/* private function declartions */
int ParsedRequest_printRequestLine(struct ParsedRequest *pr, 
				   char * buf, size_t buflen,
				   size_t *tmp);
size_t ParsedRequest_requestLineLen(struct ParsedRequest *pr);

/*
 * debug() prints out debugging info if DEBUG is set to 1
 *
 * parameter format: same as printf 
 *
 */
void debug(const char * format, ...) {
     va_list args;
     if (DEBUG) {
	  va_start(args, format);
	  vfprintf(stderr, format, args);
	  va_end(args);
     }
}


/*
 *  ParsedHeader Public Methods
 */

/* Set a header with key and value */
int ParsedHeader_set(struct ParsedRequest *pr, 
		     const char * key, const char * value)
{
     struct ParsedHeader *ph;

    //  Remove existing header (if duplicate)
     ParsedHeader_remove (pr, key);

     //pr->headers is a dynamic array of ParsedHeader structs.
//If the array is full, we double its capacity (*2) using realloc.

     if (pr->headerslen <= pr->headersused+1) {
	  pr->headerslen = pr->headerslen * 2;
	  pr->headers = 
	       (struct ParsedHeader *)realloc(pr->headers, 
		pr->headerslen * sizeof(struct ParsedHeader));
	  if (!pr->headers)
	       return -1;
     }

     //Select a slot for the new header

     ph = pr->headers + pr->headersused;
     pr->headersused += 1;

     // Allocate memory for the key string. adn copy key into it 
     
     ph->key = (char *)malloc(strlen(key)+1);
     memcpy(ph->key, key, strlen(key));
     ph->key[strlen(key)] = '\0';

     ph->value = (char *)malloc(strlen(value)+1);
     memcpy(ph->value, value, strlen(value));
     ph->value[strlen(value)] = '\0';
     //store length 

     ph->keylen = strlen(key)+1;
     ph->valuelen = strlen(value)+1;
     return 0;
}


/* get the parsedHeader with the specified key or NULL */
// ParsedHeader_get is used to look up a header in the request by its key
struct ParsedHeader* ParsedHeader_get(struct ParsedRequest *pr, 
				      const char * key)
{

     size_t i = 0;
     struct ParsedHeader * tmp; //will point to each header in turn.
     while(pr->headersused > i) //Loop through all stored headers, headersused tells us how many headers are currently stored.
     {
	  tmp = pr->headers + i; //pr->headers is the base of the array of ParsedHeader structs.
	  if(tmp->key && key && strcmp(tmp->key, key) == 0) //Make sure neither tmp->key nor key is NULL.
// Use strcmp to compare the stored key with the requested key.
// If they match → return this header.
	  {
	       return tmp;
	  }
	  i++;
     }
     return NULL;
}

/* remove the specified key from parsedHeader */
int ParsedHeader_remove(struct ParsedRequest *pr, const char *key)
{
     struct ParsedHeader *tmp;
     tmp = ParsedHeader_get(pr, key);
     if(tmp == NULL)
	  return -1;

     free(tmp->key);
     free(tmp->value);
     tmp->key = NULL;
     return 0;
}


/* modify the header with given key, giving it a new value
 * return 1 on success and 0 if no such header found
 * 
int ParsedHeader_modify(struct ParsedRequest *pr, const char * key, 
			const char *newValue)
{
     struct ParsedHeader *tmp;
     tmp = ParsedHeader_get(pr, key);
     if(tmp != NULL)
     {
	  if(tmp->valuelen < strlen(newValue)+1)
	  {
	       tmp->valuelen = strlen(newValue)+1;
	       tmp->value = (char *) realloc(tmp->value, 
					     tmp->valuelen * sizeof(char));
	  } 
	  strcpy(tmp->value, newValue);
	  return 1;
     }
     return 0;
}
*/

/*
  ParsedHeader Private Methods
*/

void ParsedHeader_create(struct ParsedRequest *pr)
{
     pr->headers = 
     (struct ParsedHeader *)malloc(sizeof(struct ParsedHeader)*DEFAULT_NHDRS);
     pr->headerslen = DEFAULT_NHDRS;
     pr->headersused = 0;
} 


size_t ParsedHeader_lineLen(struct ParsedHeader * ph)
{
     if(ph->key != NULL)
     {
	  return strlen(ph->key)+strlen(ph->value)+4;
     }
     return 0; 
}

size_t ParsedHeader_headersLen(struct ParsedRequest *pr) 
{
     if (!pr || !pr->buf)
	  return 0;

     size_t i = 0;
     int len = 0;
     while(pr->headersused > i)
     {
	  len += ParsedHeader_lineLen(pr->headers + i);
	  i++;
     }
     len += 2;
     return len;
}

int ParsedHeader_printHeaders(struct ParsedRequest * pr, char * buf, 
			      size_t len)
{
     char * current = buf;
     struct ParsedHeader * ph;
     size_t i = 0;

     if(len < ParsedHeader_headersLen(pr))
     {
	  debug("buffer for printing headers too small\n");
	  return -1;
     }
  
     while(pr->headersused > i)
     {
	  ph = pr->headers+i;
	  if (ph->key) {
	       memcpy(current, ph->key, strlen(ph->key));
	       memcpy(current+strlen(ph->key), ": ", 2);
	       memcpy(current+strlen(ph->key) +2 , ph->value, 
		      strlen(ph->value));
	       memcpy(current+strlen(ph->key) +2+strlen(ph->value) , 
		      "\r\n", 2);
	       current += strlen(ph->key)+strlen(ph->value)+4;
	  }
	  i++;
     }
     memcpy(current, "\r\n",2);
     return 0;
}


void ParsedHeader_destroyOne(struct ParsedHeader * ph)
{
     if(ph->key != NULL)
     {
	  free(ph->key);
	  ph->key = NULL;
	  free(ph->value);
	  ph->value = NULL;
	  ph->keylen = 0;
	  ph->valuelen = 0;
     }
}

void ParsedHeader_destroy(struct ParsedRequest * pr)
{
     size_t i = 0;
     while(pr->headersused > i)
     {
	  ParsedHeader_destroyOne(pr->headers + i);
	  i++;
     }
     pr->headersused = 0;

     free(pr->headers);
     pr->headerslen = 0;
}


int ParsedHeader_parse(struct ParsedRequest * pr, char * line)
/*
pr is whoel request like 
GET http://www.google.com/search?q=cat HTTP/1.1\r\n
Host: www.google.com\r\n
User-Agent: Safari/537.36\r\n
Accept: text/html\r\n
\r\n

line is a particular header line ex: "User-Agent: Safari/537.36\r\n"



*/ 

{
     char * key;
     char * value;
     char * index1;
     char * index2;

     index1 = index(line, ':'); //find the colon 
     if(index1 == NULL)
     {
	  debug("No colon found\n");
	  return -1;
     }

     //extract the key 
     key = (char *)malloc((index1-line+1)*sizeof(char));
     memcpy(key, line, index1-line);
     key[index1-line]='\0';

     // Move past ": " (colon + space)

     index1 += 2;

     //find end 
     index2 = strstr(index1, "\r\n");
     // extract value 
     value = (char *) malloc((index2-index1+1)*sizeof(char));
     memcpy(value, index1, (index2-index1));
     value[index2-index1] = '\0';

    //  Stores this key/value pair

     ParsedHeader_set(pr, key, value);
     free(key);
     free(value);
     return 0;
}

/*
  ParsedRequest Public Methods
*/

void ParsedRequest_destroy(struct ParsedRequest *pr)
{
     if(pr->buf != NULL)
     {
	  free(pr->buf);
     } 
     if (pr->path != NULL) {
	  free(pr->path);
     }
     if(pr->headerslen > 0)
     {
	  ParsedHeader_destroy(pr);
     }
     free(pr);
}

struct ParsedRequest* ParsedRequest_create()
{
     struct ParsedRequest *pr;
     pr = (struct ParsedRequest *)malloc(sizeof(struct ParsedRequest));
     if (pr != NULL)
     {
	  ParsedHeader_create(pr);
	  pr->buf = NULL;
	  pr->method = NULL;
	  pr->protocol = NULL;
	  pr->host = NULL;
	  pr->path = NULL;
	  pr->version = NULL;
	  pr->buf = NULL;
	  pr->buflen = 0;
     }
     return pr;
}

/* 
   Recreate the entire buffer from a parsed request object.
   buf must be allocated
*/
int ParsedRequest_unparse(struct ParsedRequest *pr, char *buf, 
			  size_t buflen)
{
     if (!pr || !pr->buf)
	  return -1;

     size_t tmp;
     if (ParsedRequest_printRequestLine(pr, buf, buflen, &tmp) < 0)
	  return -1;
     if (ParsedHeader_printHeaders(pr, buf+tmp, buflen-tmp) < 0)
	  return -1;
     return 0;
}

/* 
   Recreate the headers from a parsed request object.
   buf must be allocated
*/
int ParsedRequest_unparse_headers(struct ParsedRequest *pr, char *buf, 
				  size_t buflen)
{
     if (!pr || !pr->buf)
	  return -1;

     if (ParsedHeader_printHeaders(pr, buf, buflen) < 0)
	  return -1;
     return 0;
}




/* Size of the headers if unparsed into a string */
size_t ParsedRequest_totalLen(struct ParsedRequest *pr)
{
     if (!pr || !pr->buf)
	  return 0;
     return ParsedRequest_requestLineLen(pr)+ParsedHeader_headersLen(pr);
}


/* 
   Parse request buffer
 
   Parameters: 
   parse: ptr to a newly created ParsedRequest object
   buf: ptr to the buffer containing the request (need not be NUL terminated)
   and the trailing \r\n\r\n
   buflen: length of the buffer including the trailing \r\n\r\n
   
   Return values:
   -1: failure
   0: success
*/


/*
    GET http://www.google.com:80/search?q=chatgpt HTTP/1.1\r\n
    Host: www.google.com\r\n
    User-Agent: Safari/537.36\r\n
    Accept: 
   
    this whole text is sitting in buf, and buflen = its length.

*/

int 
ParsedRequest_parse(struct ParsedRequest * parse, const char *buf, 
		    int buflen)
{
     char *full_addr;
     char *saveptr;
     char *index;
     char *currentHeader;

     if (parse->buf != NULL) { //Ensures parser isn’t being reused incorrectly.
	  debug("parse object already assigned to a request\n");
	  return -1;
     }
   
     if (buflen < MIN_REQ_LEN || buflen > MAX_REQ_LEN) { //Rejects too-small or too-large requests.
	  debug("invalid buflen %d", buflen);
	  return -1;
     }
   
     /* Create NUL terminated tmp buffer */
    //  Make a copy of the buffer
     char *tmp_buf = (char *)malloc(buflen + 1); /* including NUL */
     memcpy(tmp_buf, buf, buflen);
     tmp_buf[buflen] = '\0';
   
     /*
        Verify headers section exists
        Looks for the \r\n\r\n sequence, which marks end of headers.
        If missing, request is invalid.

     */
     index = strstr(tmp_buf, "\r\n\r\n");
     if (index == NULL) {
	  debug("invalid request line, no end of header\n");
	  free(tmp_buf);
	  return -1;
     }
   
     /* Copy request line into parse->buf */
     // Extract the request line (first line only)
//      Copies only the first line ("GET http://www.google.com:80/search?q=chatgpt HTTP/1.1")
// Stores in parse->buf
     index = strstr(tmp_buf, "\r\n");
     if (parse->buf == NULL) {
	  parse->buf = (char *) malloc((index-tmp_buf)+1);
	  parse->buflen = (index-tmp_buf)+1;
     }
     memcpy(parse->buf, tmp_buf, index-tmp_buf);
     parse->buf[index-tmp_buf] = '\0';


     /* Parse request line */
     //Tokenize the request line
     parse->method = strtok_r(parse->buf, " ", &saveptr); //GET method 
     if (parse->method == NULL) { 
	  debug( "invalid request line, no whitespace\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }

     if (strcmp (parse->method, "GET")) {
	  debug( "invalid request line, method not 'GET': %s\n", 
		 parse->method);
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }

     full_addr = strtok_r(NULL, " ", &saveptr);
     // "http://www.google.com:80/search?q=cat"

     if (full_addr == NULL) {
	  debug( "invalid request line, no full address\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }

     parse->version = full_addr + strlen(full_addr) + 1;
     // "HTTP/1.1"



     if (parse->version == NULL) {
	  debug( "invalid request line, missing version\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }

     if (strncmp (parse->version, "HTTP/", 5)) {
	  debug( "invalid request line, unsupported version %s\n", 
		 parse->version);
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }


     // Split protocol, host, port, path from FULLADRESS (http://www.google.com:80/search?q=cat)

     parse->protocol = strtok_r(full_addr, "://", &saveptr); // "http"
     if (parse->protocol == NULL) {
	  debug( "invalid request line, missing host\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }
     
     const char *rem = full_addr + strlen(parse->protocol) + strlen("://");
     size_t abs_uri_len = strlen(rem);

     parse->host = strtok_r(NULL, "/", &saveptr);  //host = "www.google.com:80"
     if (parse->host == NULL) {
	  debug( "invalid request line, missing host\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }
     
     if (strlen(parse->host) == abs_uri_len) { 
	  debug("invalid request line, missing absolute path\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  return -1;
     }

     parse->path = strtok_r(NULL, " ", &saveptr); //path = "search?q=cat"

//      If no path is given (e.g., GET http://google.com HTTP/1.1),
// It assigns a default root path (/).
// Typically root_abs_path = "/".
// So missing path → becomes "/"
     if (parse->path == NULL) {  

                // replace empty abs_path with "/"
	  int rlen = strlen(root_abs_path);
	  parse->path = (char *)malloc(rlen + 1);
	  strncpy(parse->path, root_abs_path, rlen + 1);
     } 
//      Here, root_abs_path = "/".
// So if the path already starts with / (like "/search?q=cat"), then strncmp(...) == 0.
// But wait — this code is rejecting paths starting with double slash "//...".
// ⚠️ This is a safety check: HTTP spec says paths cannot start with two slashes (could be ambiguous in proxies).
// So if path = "//evil.com", the request is invalid.
     else if (strncmp(parse->path, root_abs_path, strlen(root_abs_path)) == 0) {
	  debug("invalid request line, path cannot begin "
		"with two slash characters\n");
	  free(tmp_buf);
	  free(parse->buf);
	  parse->buf = NULL;
	  parse->path = NULL;
	  return -1;
     }

      else {
	  // copy parse->path, prefix with a slash
	  char *tmp_path = parse->path;
	  int rlen = strlen(root_abs_path);  // for /
	  int plen = strlen(parse->path);
	  parse->path = (char *)malloc(rlen + plen + 1);
	  strncpy(parse->path, root_abs_path, rlen);
	  strncpy(parse->path + rlen, tmp_path, plen + 1);
     }


     // 7. Separate host and port from host;  host = "www.google.com:80"

     parse->host = strtok_r(parse->host, ":", &saveptr);
     parse->port = strtok_r(NULL, "/", &saveptr);

     if (parse->host == NULL) {
	  debug( "invalid request line, missing host\n");
	  free(tmp_buf);
	  free(parse->buf);
	  free(parse->path);
	  parse->buf = NULL;
	  parse->path = NULL;
	  return -1;
     }

     if (parse->port != NULL) {
	  int port = strtol (parse->port, (char **)NULL, 10);
	  if (port == 0 && errno == EINVAL) {
	       debug("invalid request line, bad port: %s\n", parse->port);
	       free(tmp_buf);
	       free(parse->buf);
	       free(parse->path);
	       parse->buf = NULL;
	       parse->path = NULL;
	       return -1;
	  }
     }

   
     /* Parse headers */

//      iterates over each header line until blank line (\r\n\r\n)
// Calls ParsedHeader_parse for each line → stores "key: value" pairs.
     int ret = 0;
     currentHeader = strstr(tmp_buf, "\r\n")+2;
     while (currentHeader[0] != '\0' && 
	    !(currentHeader[0] == '\r' && currentHeader[1] == '\n')) {
	  
	  //debug("line %s %s", parse->version, currentHeader);

	  if (ParsedHeader_parse(parse, currentHeader)) {
	       ret = -1;
	       break;
	  }

	  currentHeader = strstr(currentHeader, "\r\n");
	  if (currentHeader == NULL || strlen (currentHeader) < 2)
	       break;

	  currentHeader += 2;
     }
     free(tmp_buf);
     return ret;
}

/* 
   ParsedRequest Private Methods
*/

size_t ParsedRequest_requestLineLen(struct ParsedRequest *pr)
{
     if (!pr || !pr->buf)
	  return 0;

     size_t len =  
	  strlen(pr->method) + 1 + strlen(pr->protocol) + 3 + 
	  strlen(pr->host) + 1 + strlen(pr->version) + 2;
     if(pr->port != NULL)
     {
	  len += strlen(pr->port)+1;
     }
     /* path is at least a slash */
     len += strlen(pr->path);
     return len;
}

int ParsedRequest_printRequestLine(struct ParsedRequest *pr, 
				   char * buf, size_t buflen,
				   size_t *tmp)
{
     char * current = buf;

     if(buflen <  ParsedRequest_requestLineLen(pr))
     {
	  debug("not enough memory for first line\n");
	  return -1; 
     }
     memcpy(current, pr->method, strlen(pr->method));
     current += strlen(pr->method);
     current[0]  = ' ';
     current += 1;

     memcpy(current, pr->protocol, strlen(pr->protocol));
     current += strlen(pr->protocol);
     memcpy(current, "://", 3);
     current += 3;
     memcpy(current, pr->host, strlen(pr->host));
     current += strlen(pr->host);
     if(pr->port != NULL)
     {
	  current[0] = ':';
	  current += 1;
	  memcpy(current, pr->port, strlen(pr->port));
	  current += strlen(pr->port);
     }
     /* path is at least a slash */
     memcpy(current, pr->path, strlen(pr->path));
     current += strlen(pr->path);

     current[0] = ' ';
     current += 1;

     memcpy(current, pr->version, strlen(pr->version));
     current += strlen(pr->version);
     memcpy(current, "\r\n", 2);
     current +=2;
     *tmp = current-buf;
     return 0;
}
