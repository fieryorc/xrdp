/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   xrdp: A Remote Desktop Protocol server.
   Copyright (C) Jay Sorg 2004

   generic operating system calls

*/

#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <openssl/rc4.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/bn.h>

//#define MEMLEAK

#ifdef _WIN32
static CRITICAL_SECTION g_term_mutex;
#else
static pthread_mutex_t g_term_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int g_term = 0;

#ifdef MEMLEAK
static int g_memsize = 0;
static int g_memid = 0;
struct xrdp_list* g_memlist = 0;
#endif

/*****************************************************************************/
int g_init_system(void)
{
#ifdef _WIN32
  WSADATA w;

  WSAStartup(2, &w);
  InitializeCriticalSection(&g_term_mutex);
#endif
#ifdef MEMLEAK
  g_memlist = xrdp_list_create();
#endif
  return 0;
}

/*****************************************************************************/
int g_exit_system(void)
{
#ifdef _WIN32
  WSACleanup();
  DeleteCriticalSection(&g_term_mutex);
#endif
#ifdef MEMLEAK
  int i;
  struct xrdp_mem* p;

  for (i = 0; i < g_memlist->count; i++)
  {
    p = (struct xrdp_mem*)xrdp_list_get_item(g_memlist, i);
    g_printf("leak size %d id %d\n\r", p->size, p->id);
  }
  g_printf("mem %d\n\r", g_memsize);
  xrdp_list_delete(g_memlist);
  g_memlist = 0;
#endif
  return 0;
}

/*****************************************************************************/
void* g_malloc(int size, int zero)
{
#ifdef MEMLEAK
  char* rv;
  struct xrdp_mem* p;

  rv = (char*)malloc(size + sizeof(struct xrdp_mem));
  if (zero)
    memset(rv, 0, size + sizeof(struct xrdp_mem));
  g_memsize += size;
  p = (struct xrdp_mem*)rv;
  p->size = size;
  p->id = g_memid;
  if (g_memlist != 0)
    xrdp_list_add_item(g_memlist, (int)p);
  g_memid++;
  return rv + sizeof(struct xrdp_mem);
#else
  char* rv;

  rv = (char*)malloc(size);
  if (zero)
    memset(rv, 0, size);
  return rv;
#endif
}

/*****************************************************************************/
void* g_malloc1(int size, int zero)
{
  char* rv;

  rv = (char*)malloc(size);
  if (zero)
    memset(rv, 0, size);
  return rv;
}

/*****************************************************************************/
void g_free(void* ptr)
{
#ifdef MEMLEAK
  struct xrdp_mem* p;
  int i;

  if (ptr != 0)
  {
    p = (struct xrdp_mem*)(((char*)ptr) - sizeof(struct xrdp_mem));
    g_memsize -= p->size;
    i = xrdp_list_index_of(g_memlist, (int)p);
    if (i >= 0)
      xrdp_list_remove_item(g_memlist, i);
    free(p);
  }
#else
  if (ptr != 0)
  {
    free(ptr);
  }
#endif
}

/*****************************************************************************/
void g_free1(void* ptr)
{
  if (ptr != 0)
  {
    free(ptr);
  }
}

/*****************************************************************************/
void g_printf(char* format, ...)
{
  va_list ap;

  va_start(ap, format);
  vfprintf(stdout, format, ap);
  va_end(ap);
}

/*****************************************************************************/
/* produce a hex dump */
void g_hexdump(char* p, int len)
{
  unsigned char* line;
  int i;
  int thisline;
  int offset;

  line = (unsigned char*)p;
  offset = 0;
  while (offset < len)
  {
    printf("%04x ", offset);
    thisline = len - offset;
    if (thisline > 16)
      thisline = 16;
    for (i = 0; i < thisline; i++)
      printf("%02x ", line[i]);
    for (; i < 16; i++)
      printf("   ");
    for (i = 0; i < thisline; i++)
      printf("%c", (line[i] >= 0x20 && line[i] < 0x7f) ? line[i] : '.');
    printf("\n");
    offset += thisline;
    line += thisline;
  }
}

/*****************************************************************************/
void g_memset(void* ptr, int val, int size)
{
  memset(ptr, val, size);
}

/*****************************************************************************/
void g_memcpy(void* d_ptr, const void* s_ptr, int size)
{
  memcpy(d_ptr, s_ptr, size);
}

/*****************************************************************************/
int g_getchar(void)
{
  return getchar();
}

/*****************************************************************************/
int g_tcp_socket(void)
{
  int rv;
  int i;

  i = 1;
  rv = socket(PF_INET, SOCK_STREAM, 0);
  setsockopt(rv, IPPROTO_TCP, TCP_NODELAY, (void*)&i, sizeof(i));
  return rv;
}

/*****************************************************************************/
void g_tcp_close(int sck)
{
#ifdef _WIN32
  closesocket(sck);
#else
  close(sck);
#endif
}

/*****************************************************************************/
int g_tcp_set_non_blocking(int sck)
{
  unsigned long i;

#ifdef _WIN32
  i = 1;
  ioctlsocket(sck, FIONBIO, &i);
#else
  i = fcntl(sck, F_GETFL);
  i = i | O_NONBLOCK;
  fcntl(sck, F_SETFL, i);
#endif
  return 0;
}

/*****************************************************************************/
int g_tcp_bind(int sck, char* port)
{
  struct sockaddr_in s;

  memset(&s, 0, sizeof(struct sockaddr_in));
  s.sin_family = AF_INET;
  s.sin_port = htons(atoi(port));
  s.sin_addr.s_addr = INADDR_ANY;
  return bind(sck, (struct sockaddr*)&s, sizeof(struct sockaddr_in));
}

/*****************************************************************************/
int g_tcp_listen(int sck)
{
  return listen(sck, 2);
}

/*****************************************************************************/
int g_tcp_accept(int sck)
{
  struct sockaddr_in s;
#ifdef _WIN32
  signed int i;
#else
  unsigned int i;
#endif

  i = sizeof(struct sockaddr_in);
  memset(&s, 0, i);
  return accept(sck, (struct sockaddr*)&s, &i);
}

/*****************************************************************************/
int g_tcp_recv(int sck, void* ptr, int len, int flags)
{
  return recv(sck, ptr, len, flags);
}

/*****************************************************************************/
int g_tcp_send(int sck, void* ptr, int len, int flags)
{
  return send(sck, ptr, len, flags);
}

/*****************************************************************************/
int g_tcp_last_error_would_block(int sck)
{
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EWOULDBLOCK;
#endif
}

/*****************************************************************************/
int g_tcp_select(int sck)
{
  fd_set rfds;
  struct timeval time;

  time.tv_sec = 0;
  time.tv_usec = 0;
  FD_ZERO(&rfds);
  FD_SET(((unsigned int)sck), &rfds);
  return select(sck + 1, &rfds, 0, 0, &time);
}

/*****************************************************************************/
int g_is_term(void)
{
  int rv;

#ifdef _WIN32
  EnterCriticalSection(&g_term_mutex);
  rv = g_term;
  LeaveCriticalSection(&g_term_mutex);
#else
  pthread_mutex_lock(&g_term_mutex);
  rv = g_term;
  pthread_mutex_unlock(&g_term_mutex);
#endif
  return rv;
}

/*****************************************************************************/
void g_set_term(int in_val)
{
#ifdef _WIN32
  EnterCriticalSection(&g_term_mutex);
  g_term = in_val;
  LeaveCriticalSection(&g_term_mutex);
#else
  pthread_mutex_lock(&g_term_mutex);
  g_term = in_val;
  pthread_mutex_unlock(&g_term_mutex);
#endif
}

/*****************************************************************************/
void g_sleep(int msecs)
{
#ifdef _WIN32
  Sleep(msecs);
#else
  usleep(msecs * 1000);
#endif
}

/*****************************************************************************/
#ifdef _WIN32
int g_thread_create(unsigned long (__stdcall * start_routine)(void*), void* arg)
{
  DWORD thread;

  return !CreateThread(0, 0, start_routine, arg, 0, &thread);
}
#else
int g_thread_create(void* (* start_routine)(void*), void* arg)
{
  pthread_t thread;

  return pthread_create(&thread, 0, start_routine, arg);
}
#endif

/* rc4 stuff */

/*****************************************************************************/
void* g_rc4_info_create(void)
{
  return g_malloc(sizeof(RC4_KEY), 1);;
}

/*****************************************************************************/
void g_rc4_info_delete(void* rc4_info)
{
  g_free(rc4_info);
}

/*****************************************************************************/
void g_rc4_set_key(void* rc4_info, char* key, int len)
{
  RC4_set_key((RC4_KEY*)rc4_info, len, (unsigned char*)key);
}

/*****************************************************************************/
void g_rc4_crypt(void* rc4_info, char* data, int len)
{
  RC4((RC4_KEY*)rc4_info, len, (unsigned char*)data, (unsigned char*)data);
}

/* sha1 stuff */

/*****************************************************************************/
void* g_sha1_info_create(void)
{
  return g_malloc(sizeof(SHA_CTX), 1);
}

/*****************************************************************************/
void g_sha1_info_delete(void* sha1_info)
{
  g_free(sha1_info);
}

/*****************************************************************************/
void g_sha1_clear(void* sha1_info)
{
  SHA1_Init((SHA_CTX*)sha1_info);
}

/*****************************************************************************/
void g_sha1_transform(void* sha1_info, char* data, int len)
{
  SHA1_Update((SHA_CTX*)sha1_info, data, len);
}

/*****************************************************************************/
void g_sha1_complete(void* sha1_info, char* data)
{
  SHA1_Final((unsigned char*)data, (SHA_CTX*)sha1_info);
}

/* md5 stuff */

/*****************************************************************************/
void* g_md5_info_create(void)
{
  return g_malloc(sizeof(MD5_CTX), 1);
}

/*****************************************************************************/
void g_md5_info_delete(void* md5_info)
{
  g_free(md5_info);
}

/*****************************************************************************/
void g_md5_clear(void* md5_info)
{
  MD5_Init((MD5_CTX*)md5_info);
}

/*****************************************************************************/
void g_md5_transform(void* md5_info, char* data, int len)
{
  MD5_Update((MD5_CTX*)md5_info, data, len);
}

/*****************************************************************************/
void g_md5_complete(void* md5_info, char* data)
{
  MD5_Final((unsigned char*)data, (MD5_CTX*)md5_info);
}

/*****************************************************************************/
int g_mod_exp(char* out, char* in, char* mod, char* exp)
{
  BN_CTX* ctx;
  BIGNUM lmod;
  BIGNUM lexp;
  BIGNUM lin;
  BIGNUM lout;
  int rv;

  ctx = BN_CTX_new();
  BN_init(&lmod);
  BN_init(&lexp);
  BN_init(&lin);
  BN_init(&lout);
  BN_bin2bn((unsigned char*)mod, 64, &lmod);
  BN_bin2bn((unsigned char*)exp, 64, &lexp);
  BN_bin2bn((unsigned char*)in, 64, &lin);
  BN_mod_exp(&lout, &lin, &lexp, &lmod, ctx);
  rv = BN_bn2bin(&lout, (unsigned char*)out);
  BN_free(&lin);
  BN_free(&lout);
  BN_free(&lexp);
  BN_free(&lmod);
  BN_CTX_free(ctx);
  return rv;
}

/*****************************************************************************/
void g_random(char* data, int len)
{
#ifdef _WIN32
  memset(data, 0x44, len);
#else
  int fd;

  memset(data, 0x44, len);
  fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1)
    fd = open("/dev/random", O_RDONLY);
  if (fd != -1)
  {
    read(fd, data, len);
    close(fd);
  }
#endif
}

/*****************************************************************************/
int g_abs(int i)
{
  return abs(i);
}

/*****************************************************************************/
int g_memcmp(void* s1, void* s2, int len)
{
  return memcmp(s1, s2, len);
}

/*****************************************************************************/
int g_file_open(char* file_name)
{
#ifdef _WIN32
  return (int)CreateFile(file_name, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
#else
  return open(file_name, O_RDWR | O_CREAT);
#endif
}

/*****************************************************************************/
int g_file_close(int fd)
{
#ifdef _WIN32
  CloseHandle((HANDLE)fd);
#else
  close(fd);
#endif
  return 0;
}

/*****************************************************************************/
/* read from file*/
int g_file_read(int fd, char* ptr, int len)
{
#ifdef _WIN32
  if (ReadFile((HANDLE)fd, (LPVOID)ptr, (DWORD)len, (LPDWORD)&len, 0))
    return len;
  else
    return -1;
#else
  return read(fd, ptr, len);
#endif
}

/*****************************************************************************/
/* write to file */
int g_file_write(int fd, char* ptr, int len)
{
#ifdef _WIN32
  if (WriteFile((HANDLE)fd, (LPVOID)ptr, (DWORD)len, (LPDWORD)&len, 0))
    return len;
  else
    return -1;
#else
  return write(fd, ptr, len);
#endif
}

/*****************************************************************************/
/* move file pointer */
int g_file_seek(int fd, int offset)
{
#ifdef _WIN32
  return SetFilePointer((HANDLE)fd, offset, 0, FILE_BEGIN);
#else
  return lseek(fd, offset, SEEK_SET);
#endif
}

/*****************************************************************************/
/* do a write lock on a file */
/* return boolean */
int g_file_lock(int fd, int start, int len)
{
#ifdef _WIN32
  return LockFile((HANDLE)fd, start, 0, len, 0);
#else
  struct flock lock;

  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = start;
  lock.l_len = len;
  if (fcntl(fd, F_SETLK, &lock) == -1)
    return 0;
  return 1;
#endif
}

/*****************************************************************************/
int g_strlen(char* text)
{
  return strlen(text);
}

/*****************************************************************************/
char* g_strcpy(char* dest, char* src)
{
  return strcpy(dest, src);
}

/*****************************************************************************/
char* g_strcat(char* dest, char* src)
{
  return strcat(dest, src);
}
