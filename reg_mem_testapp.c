/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#undef NDEBUG
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "config.h"
#include "cache.h"
#include "util.h"
#include "protocol_binary.h"

#define TMP_TEMPLATE "/tmp/test_file.XXXXXXX"

static pid_t server_pid;
static in_port_t port;
static int sock;

/**
 * Function to start the server and let it listen on a random port
 *
 * @param port_out where to store the TCP port number the server is
 *                 listening on
 * @param daemon set to true if you want to run the memcached server
 *               as a daemon process
 * @return the pid of the memcached server
 */
static inline void cpuid(void) {
  asm volatile(
      "cpuid\n\t"
      : ::"%rax", "%rbx", "%rcx", "%rdx");
}

static inline unsigned long get_ticks_start(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "cpuid\n\t"
      "rdtsc\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}

static inline unsigned long get_ticks_end(void) {
  unsigned int cycles_high, cycles_low;
  asm volatile(
      "rdtscp\n\t"
      "mov %%edx, %0\n\t"
      "mov %%eax, %1\n\t"
      "cpuid\n\t"
      : "=r"(cycles_high), "=r"(cycles_low)::"%rax", "%rbx", "%rcx", "%rdx");
  return ((unsigned long)cycles_low) | (((unsigned long)cycles_high) << 32);
}
static pid_t start_server(in_port_t *port_out, bool daemon, int timeout) {
    char environment[80];
    snprintf(environment, sizeof(environment),
             "MEMCACHED_PORT_FILENAME=/tmp/ports.%lu", (long)getpid());
    char *filename= environment + strlen("MEMCACHED_PORT_FILENAME=");
    char pid_file[80];
    snprintf(pid_file, sizeof(pid_file), "/tmp/pid.%lu", (long)getpid());

    remove(filename);
    remove(pid_file);

#ifdef __sun
    /* I want to name the corefiles differently so that they don't
       overwrite each other
    */
    char coreadm[128];
    snprintf(coreadm, sizeof(coreadm),
             "coreadm -p core.%%f.%%p %lu", (unsigned long)getpid());
    system(coreadm);
#endif

    pid_t pid = fork();
    assert(pid != -1);

    if (pid == 0) {
        /* Child */
        char *argv[20];
        int arg = 0;
        char tmo[24];
        snprintf(tmo, sizeof(tmo), "%u", timeout);

        putenv(environment);
#ifdef __sun
        putenv("LD_PRELOAD=watchmalloc.so.1");
        putenv("MALLOC_DEBUG=WATCH");
#endif

        if (!daemon) {
            argv[arg++] = "./timedrun";
            argv[arg++] = tmo;
        }
        argv[arg++] = "./memcached";
        argv[arg++] = "-A";
        argv[arg++] = "-p";
        argv[arg++] = "-1";
        argv[arg++] = "-U";
        argv[arg++] = "0";
        /* Handle rpmbuild and the like doing this as root */
        if (getuid() == 0) {
            argv[arg++] = "-u";
            argv[arg++] = "root";
        }
        if (daemon) {
            argv[arg++] = "-d";
            argv[arg++] = "-P";
            argv[arg++] = pid_file;
        }
#ifdef MESSAGE_DEBUG
         argv[arg++] = "-vvv";
#endif
#ifdef HAVE_DROP_PRIVILEGES
        argv[arg++] = "-o";
        argv[arg++] = "relaxed_privileges";
#endif
        argv[arg++] = NULL;
        assert(execv(argv[0], argv) != -1);
    }

    /* Yeah just let us "busy-wait" for the file to be created ;-) */
    while (access(filename, F_OK) == -1) {
        usleep(10);
    }

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open the file containing port numbers: %s\n",
                strerror(errno));
        assert(false);
    }

    *port_out = (in_port_t)-1;
    char buffer[80];
    while ((fgets(buffer, sizeof(buffer), fp)) != NULL) {
        if (strncmp(buffer, "TCP INET: ", 10) == 0) {
            int32_t val;
            assert(safe_strtol(buffer + 10, &val));
            *port_out = (in_port_t)val;
        }
    }
    fclose(fp);
    assert(remove(filename) == 0);

    return pid;
}


static struct addrinfo *lookuphost(const char *hostname, in_port_t port)
{
    struct addrinfo *ai = 0;
    struct addrinfo hints = { .ai_family = AF_UNSPEC,
                              .ai_protocol = IPPROTO_TCP,
                              .ai_socktype = SOCK_STREAM };
    char service[NI_MAXSERV];
    int error;

    (void)snprintf(service, NI_MAXSERV, "%d", port);
    if ((error = getaddrinfo(hostname, service, &hints, &ai)) != 0) {
       if (error != EAI_SYSTEM) {
          fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
       } else {
          perror("getaddrinfo()");
       }
    }

    return ai;
}

static int connect_server(const char *hostname, in_port_t port, bool nonblock)
{
    struct addrinfo *ai = lookuphost(hostname, port);
    int sock = -1;
    if (ai != NULL) {
       if ((sock = socket(ai->ai_family, ai->ai_socktype,
                          ai->ai_protocol)) != -1) {
          if (connect(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
             fprintf(stderr, "Failed to connect socket: %s\n",
                     strerror(errno));
             close(sock);
             sock = -1;
          } else if (nonblock) {
              int flags = fcntl(sock, F_GETFL, 0);
              if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
                  fprintf(stderr, "Failed to enable nonblocking mode: %s\n",
                          strerror(errno));
                  close(sock);
                  sock = -1;
              }
          }
       } else {
          fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
       }

       freeaddrinfo(ai);
    }
    return sock;
}

static void send_ascii_command(const char *buf) {
    off_t offset = 0;
    const char* ptr = buf;
    size_t len = strlen(buf);

    do {
        ssize_t nw = write(sock, ptr + offset, len - offset);
        if (nw == -1) {
            if (errno != EINTR) {
                fprintf(stderr, "Failed to write: %s\n", strerror(errno));
                abort();
            }
        } else {
            offset += nw;
        }
    } while (offset < len);
}

static void start_memcached_server(void) {
    server_pid = start_server(&port, false, 600);
    sock = connect_server("127.0.0.1", port, false);
}

static void shutdown_memcached_server(void) {
    char buffer[1024];

    close(sock);
    sock = connect_server("127.0.0.1", port, false);

    send_ascii_command("shutdown\r\n");
    /* verify that the server closed the connection */
    assert(read(sock, buffer, sizeof(buffer)) == 0);

    close(sock);

    /* We set server_pid to -1 so that we don't later call kill() */
    if (kill(server_pid, 0) == 0) {
        server_pid = -1;
    }

}

static void read_response(char* buff){
  read(sock, buff, 512);
}
static char buff[512];
static char response_buff[512];
#define SLOW
static inline void do_store(char* key, char* value){
  strcpy(buff, "add ");
  strcat(buff, key);
  strcat(buff, " 0 0 24 1\r\n");
  strcat(buff, value);
  strcat(buff, "\r\n");
  send_ascii_command(buff);
#ifdef SLOW
  read_response(response_buff);
#endif
}

static inline void do_touch(char* key){
  strcpy(buff, "touch ");
  strcat(buff, key);
  strcat(buff, " 0 1\r\n");
  send_ascii_command(buff);
#ifdef SLOW
  read_response(response_buff);
#endif
}

static inline void do_get(char* key){
  strcpy(buff, "get ");
  strcat(buff, key);
  strcpy(buff, "\r\n");
  send_ascii_command(buff);
  read_response(response_buff);
}

int main(int argc, char **argv)
{
  memset(response_buff, 0, 512);
  start_memcached_server();
  unsigned long long begin, end, count = 0;
  FILE *f = fopen("../data/test.txt", "r");
  char command[128];
  char key[128];
  char val[128];
  int n;

// begin testing

  fscanf(f, "INSERT %d\n", &n); 
  for(int i = 0; i < n; ++i){ 
    fscanf(f, "%s %s", key, val);
    begin = get_ticks_start();
    do_store(key, val);
    end = get_ticks_end();
    count += (end-begin);
  }
  printf("INSERT takes %fcy\n", ((count*1.0)/n));

  fscanf(f, "%s %d", command, &n);
  if (strcmp(command, "TOUCH") == 0){
    count = 0;
    printf("touched!\n");
    for(int i = 0; i < n; ++i){
      fscanf(f, "%s", key);
      begin = get_ticks_start();
      do_touch(key);
      end = get_ticks_end();
      count += (end-begin);
    }
    printf("TOUCH takes %fcy\n", ((count*1.0)/n));
    fscanf(f, "%s %d", command, &n);
  } else printf("command was %s\n", command);

  if (strcmp(command, "GET") == 0){
    printf("got!\n");
    count = 0;
    for(int i = 0; i < n; ++i){
      fscanf(f, "%s %s", key, val);
      begin = get_ticks_start();
      do_get(key);
      end = get_ticks_end();
      count += end - begin;
    }
    printf("GET takes %fcy\n", (1.0*count)/n);
  } else printf("command was %s\n", command);

// end testing

  shutdown_memcached_server();
}
