/*
 * active port forwarder - software for secure forwarding
 * Copyright (C) 2003,2004 jeremian <jeremian [at] poczta.fm>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "activefor.h"
#include "network.h"
#include "stats.h"
#include "modules.h"

#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#include <getopt.h>

static void usage(char* info);
static void sig_int(int);
static void callback(int, int, void*);

static struct option long_options[] = {
  {"help", 0, 0, 'h'},
  {"udpmode", 0, 0, 'u'},
  {"reverseudp", 0, 0, 'U'},
  {"servername", 1, 0, 'n'},
  {"manageport", 1, 0, 'm'},
  {"hostname", 1, 0, 'd'},
  {"portnum", 1, 0, 'p'},
  {"verbose", 0, 0, 'v'},
  {"keyfile", 1, 0, 'k'},
  {"heavylog", 1, 0, 'O'},
  {"lightlog", 1, 0, 'o'},
  {"pass", 1, 0, 301},
  {"ipv4", 0, 0, '4'},
  {"ipv6", 0, 0, '6'},
  {"load", 1, 0, 'l'},
  {"Load", 1, 0, 'L'},
  {0, 0, 0, 0}
};

int
main(int argc, char **argv)
{
  int i, n, numofcon, length, buflength, notsent, temp2; 
  ConnectuserT* contable = NULL;
  clifd master;
  unsigned char buff[9000];
  char hostname[100];
  struct timeval tv;
  int maxfdp1, usernum, usercon, merror;
  socklen_t len, addrlen;
  struct sockaddr* cliaddr;
  fd_set rset, allset, wset, tmpset;
  char verbose = 0;
  char logging = 0;
  char* name = NULL;
  char* manage = NULL;
  char* desnam = NULL;
  char* despor = NULL;
  char* keys = NULL;
  char* logfname = NULL;
  char ipfam = 0;
  unsigned char pass[4] = {1, 2, 3, 4};
  char udp = 0;
  char reverse = 0;
  char type = 0;
  struct sigaction act;
  moduleT module = {0, NULL, NULL, NULL, NULL}, secmodule = {0, NULL, NULL, NULL, NULL};

  SSL_METHOD* method;
  SSL_CTX* ctx;
  RSA* rsa;
 
  sigfillset(&(act.sa_mask));
  act.sa_flags = 0;
	
  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, NULL);
  act.sa_handler = sig_int;
  sigaction(SIGINT, &act, NULL);

  while ((n = getopt_long(argc, argv, "huUn:m:d:p:vk:O:o:46l:L:", long_options, 0)) != -1) {
    switch (n) {
      case 'h': {
        usage(AF_VER("Active port forwarder (client)"));
        break;
      }
      case 'n': {
        name = optarg;
        break;
      }
      case 'm': {
        manage = optarg;
        break;
      }
      case 'd': {
        desnam = optarg;
        break;
      }
      case 'p': {
        despor = optarg;
        break;
      }
      case 'v': {
        ++verbose;
        break;
      }
      case 'u': {
        udp = 1;
        break;
      }
      case 'U': {
        reverse = 1;
        break;
      }
      case 'k': {
        keys = optarg;
        break;
      }
      case 'O': {
        logfname = optarg;
        logging = 3;
        break;
      }
      case 'o': {
        logfname = optarg;
        logging = 1;
        break;
      }
      case 301: {
        n = strlen(optarg);
        memset(pass, 0, 4);
        for (i = 0; i < n; ++i) {
          pass[i%4] += optarg[i];
        }
        break;
      }
      case '4': {
        if (ipfam != 0) {
          ipfam = -1;
        }
        else {
          ipfam = 4;
        }
        break;
      }
      case '6': {
        if (ipfam != 0) {
          ipfam = -1;
        }
        else {
          ipfam = 6;
        }
        break;
      }
      case 'l': {
        module.name = optarg;
        break;
      }
      case 'L': {
        secmodule.name = optarg;
        break;
      }
      case '?': {
        usage("");
        break;
      }
    }
  }

  if (optind < argc) {
    usage("Unrecognized non-option elements");
  }

  if (name == NULL) {
    usage("Name of the server is required");
  }
  if (manage == NULL) {
    manage = "50126";
    if (reverse)
      usage("Port on the server is required in reverse mode");
  }
  if (desnam == NULL) {
    gethostname(hostname, 100);
    desnam = hostname;
  }
  if (despor == NULL) {
    usage("Destination port number is required");
  }

  if (loadmodule(&module)) {
      printf("Loading a module %s failed!\n", module.name);
      exit(1);
  }
  if (loadmodule(&secmodule)) {
      printf("Loading a module %s failed!\n", secmodule.name);
      exit(1);
  }
  
  TYPE_SET_SSL(type);
  TYPE_SET_ZLIB(type);

  if (ipfam == -1) {
    printf("Conflicting types of ip protocol family... exiting\n");
    exit(1);
  }
  else if (ipfam == 4) {
    TYPE_SET_IPV4(type);
  }
  else if (ipfam == 6) {
    TYPE_SET_IPV6(type);
  }

  ipfam = 0x01;
  if (TYPE_IS_IPV4(type)) {
    ipfam |= 0x02;
  }
  else if (TYPE_IS_IPV6(type)) {
    ipfam |= 0x04;
  }
  if (ip_connect(&(master.commfd), name, manage, ipfam)) {
    printf("tcp_connect_%s error for %s, %s\n",
		    (ipfam & 0x02)?"ipv4":(ipfam & 0x04)?"ipv6":"unspec", name, manage);
    exit(1);
  }

  if (!reverse) {
    SSL_library_init();
    method = SSLv3_client_method();
    ctx = SSL_CTX_new(method);
    if (SSL_CTX_set_cipher_list(ctx, "ALL:@STRENGTH") == 0) {
      printf("Setting cipher list failed... exiting\n");
      exit(1);
    }
    if (keys == NULL) {
      gettimeofday(&tv, 0);
      srand(tv.tv_sec);
      if (verbose)
        printf("Generating RSA key...\n");
      if (verbose>1) {
          rsa = RSA_generate_key(2048, 65537, callback, NULL);
          printf("\n");
      }
      else {
        rsa = RSA_generate_key(2048, 65537, NULL, NULL);
      }
      if (verbose) {
        if (RSA_check_key(rsa)==1) {
          printf("   OK!\n");
        }
        else {
          printf("   FAILED\n");
        }
      }
    }
    else {
      if (SSL_CTX_use_RSAPrivateKey_file(ctx, keys, SSL_FILETYPE_PEM) != 1) {
        printf("Setting rsa key failed (%s)... exiting\n", keys);
        exit(1);
      }
    }
    master.ssl = SSL_new(ctx);
    if (SSL_set_fd(master.ssl, master.commfd) != 1) {
      printf("Problem with initializing ssl... exiting\n");
      exit(1);
    }
    if (verbose>1)
      printf("Trying SSL_connect\n");
    if ((n = SSL_connect(master.ssl)) == 1) {
      if (verbose) {
        printf("SSL_connect successfull\n");
      }
    }
    else {
      printf("SSL_connect has failed (%d)... exiting\n", n);
      exit(1);
    }

    buff[0] = AF_S_LOGIN;
    buff[1] = pass[0];
    buff[2] = pass[1];
    buff[3] = pass[2];
    buff[4] = pass[3];
    send_message(type, master, buff, 5);
    buff[0] = 0;
    get_message(type, master, buff, -5);

    if ( buff[0] == 0 ) {
      printf("Wrong password\n");
      exit(1);
    }
    if ( buff[0] != AF_S_LOGIN ) {
      printf("Incompatible server type or server full\n");
      exit(1);
    }

    type = buff[3];
    usernum = buff[1];
    usernum = usernum << 8;
    usernum += buff[2];
  } /* !reverse */
  else {
    usernum = 1;
    master.ssl = NULL;
  }

  contable = calloc( usernum, sizeof(ConnectuserT));
  if (contable == NULL) {
    printf("Calloc error - unable to succesfully communicate with server\n");
    exit(1);
  }
	
  len = 4;
  if (getsockopt(master.commfd, SOL_SOCKET, SO_SNDBUF, &buflength, &len) == -1) {
    printf("Can't get socket send buffer size - exiting...\n");
    exit(1);
  }

  if (loginit(verbose, logging, logfname)) {
    printf("Can't open file to log to... exiting\n");
    exit(1);
  }
	
  if (!verbose)
    daemon(0, 0);
	
  FD_ZERO(&allset);
  FD_ZERO(&wset);
	
  FD_SET(master.commfd, &allset);
  maxfdp1 = master.commfd + 1;

  /* UDP REVERSE MODE */
  
  if (reverse) {
    ipfam = 0;
    if (TYPE_IS_IPV4(type)) {
      ipfam |= 0x02;
    }
    else if (TYPE_IS_IPV6(type)) {
      ipfam |= 0x04;
    }
    if (ip_listen(&(contable[0].connfd), desnam, despor, &addrlen, ipfam)) {
      printf("udp_listen_%s error for %s, %s\n",
		      (ipfam & 0x02)?"ipv4":(ipfam & 0x04)?"ipv6":"unspec", desnam, despor);
      exit(1);
    }
    cliaddr = malloc(addrlen);
    maxfdp1 = (maxfdp1>contable[0].connfd+1) ? maxfdp1 : contable[0].connfd+1;
    FD_SET(contable[0].connfd, &allset);
    aflog(1, "CLIENT STARTED mode: udp reverse");
    for ( ; ; ) {
      len = 4;
      if (getsockopt(master.commfd, SOL_SOCKET, SO_SNDBUF, &temp2, &len) != -1) {
        if (temp2 != buflength) {
          buflength = temp2;
          aflog(2, "Send buffer size changed...");
        }
      }
      len = addrlen;
      rset = allset;
      aflog(3, ">select");
      select(maxfdp1, &rset, NULL, NULL, NULL);
      aflog(3, " >>after select...");
      
      if (FD_ISSET(contable[0].connfd, &rset)) { /* FD_ISSET   CONTABLE[0].CONNFD   RSET*/
        n = recvfrom(contable[0].connfd, &buff[5], 8091, 0, cliaddr, &len);
#ifdef SIOCOUTQ
        if (ioctl(master.commfd, SIOCOUTQ, &notsent)) {
          aflog(0, "ioctl error -> exiting...");
          exit(1);
        }
        if (buflength <= notsent + n + 5) { /* when we can't do this */
          aflog(2, "drop: size:%d, buf:%d, w:%d/%d", n, buflength, notsent, buflength);
#else
        if (ioctl(master.commfd, TIOCOUTQ, &notsent)) {
          aflog(0, "ioctl error -> exiting...");
          exit(1);
        }
        if (notsent <= n + 5) { /* when we can't do this */
          aflog(2, "drop: size:%d, buf:%d, w:%d/%d", n, buflength, buflength - notsent, buflength);
#endif
	}
        else {
          if (n > 0) {
            aflog(2, "Sending %d bytes to service (w:%d/%d) (FROM:%s)", n,
#ifdef SIOCOUTQ
			notsent
#else
			buflength - notsent
#endif
			, buflength, sock_ntop(cliaddr, len, NULL, NULL));
            buff[0] = AF_S_MESSAGE;
            buff[1] = AF_S_LOGIN;
            buff[2] = AF_S_MESSAGE;
            buff[3] = n >> 8;
            buff[4] = n;
            writen(master.commfd, buff, n + 5);
          }
        }
      } /* - FD_ISSET   CONTABLE[0].CONNFD   RSET */
      if (FD_ISSET(master.commfd, &rset)) { /* FD_ISSET   MASTER.COMMFD   RSET */
        n = readn(master.commfd, buff, 5);
        if (n == 5) {
          if ((buff[0] != AF_S_MESSAGE) || (buff[1] != AF_S_LOGIN) || (buff[2] != AF_S_MESSAGE)) {
            aflog(0, "Incompatible server type (not udp?) or data corruption -> exiting...");
            exit(1);
          }
          length = buff[3];
          length = length << 8;
          length += buff[4]; /* this is length of message */
          n = readn(master.commfd, buff, length);
        }
        else {
          n = 0;
        }
        if (n == 0) { /* server quits -> we do the same... */
          aflog(0, "premature quit of the server -> exiting...");
          exit(1);
        }
        aflog(2, "Sending %d bytes to user (TO:%s)", n, sock_ntop(cliaddr, addrlen, NULL, NULL));
        sendto(contable[0].connfd, buff, n, 0, cliaddr, addrlen);
      } /* - FD_ISSET   MASTER.COMMFD   RSET */
    }
    exit(0); /* we shouldn't get here */
  }

  /* NORMAL MODE */
	
  aflog(1, "CLIENT STARTED mode: %s", (udp)?"udp":"tcp");
  aflog(1, "SERVER SSL: %s, ZLIB: %s, MODE: %s", (TYPE_IS_SSL(type))?"yes":"no",
		  (TYPE_IS_ZLIB(type))?"yes":"no", (TYPE_IS_TCP(type))?"tcp":"udp");
  aflog(2, "CIPHER: %s VER: %s", SSL_get_cipher_name(master.ssl), SSL_get_cipher_version(master.ssl));
  if (ismloaded(&module)) {
    aflog(1, "LOADED MODULE: %s INFO: %s", module.name, module.info());
  }
  if (ismloaded(&secmodule)) {
    aflog(1, "LOADED MODULE (ser): %s INFO: %s", secmodule.name, secmodule.info());
  }
	
  for ( ; ; ) {
    rset = allset;
    tmpset = wset;
    aflog(3, ">select");
    select(maxfdp1, &rset, &tmpset, NULL, NULL);
    aflog(3, " >>after select...");

    for (i = 0; i < usernum; ++i) {
      if ((contable[i].state == S_STATE_OPEN)||(contable[i].state == S_STATE_STOPPED)) {
        if (FD_ISSET(contable[i].connfd, &rset)) { /* FD_ISSET   CONTABLE[i].CONNFD   RSET */
          aflog(3, " user[%d]: FD_ISSET", i);
          n = read(contable[i].connfd, &buff[5], 8091);
          if (n == -1) {
            aflog(0, "  error (%d): while reading from service", n);
            n = 0;
          }
#ifdef SIOCOUTQ
          if (ioctl(master.commfd, SIOCOUTQ, &notsent)) {
            aflog(0, "ioctl error -> exiting...");
            exit(1);
          }
          if (udp) {
            len = 4;
            if (getsockopt(master.commfd, SOL_SOCKET, SO_SNDBUF, &temp2, &len) != -1) {
              if (temp2 != buflength) {
                buflength = temp2;
                aflog(2, "Send buffer size changed...");
              }
            }
            if (buflength <= notsent + n + 5) { /* when we can't send this */
              aflog(2, " user[%d]: DROP size:%d, buf:%d, w:%d/%d", i, n+5, buflength, notsent, buflength);
#else
          if (ioctl(master.commfd, TIOCOUTQ, &notsent)) {
            aflog(0, "ioctl error -> exiting...");
            exit(1);
          }
          if (udp) {
            len = 4;
            if (getsockopt(master.commfd, SOL_SOCKET, SO_SNDBUF, &temp2, &len) != -1) {
              if (temp2 != buflength) {
                buflength = temp2;
                aflog(2, "Send buffer size changed...");
              }
            }
            if (notsent <= n + 5) { /* when we can't send this */
              aflog(2, " user[%d]: DROP size:%d, buf:%d, w:%d/%d", 
			      i, n+5, buflength, buflength-notsent, buflength);
#endif
              continue; /* drop this packet */
            }
          }
          if (n) {
              if (ismloaded(&secmodule)) {
                switch ((temp2 = secmodule.filter(contable[i].namebuf, &buff[5], &n))) {
                  case 1: case 4: {
                    aflog(3, "  user[%d] (by ser): PACKET IGNORED BY MODULE", i);
		    if (temp2 == 4) {
                      aflog(1, "RELEASED MODULE (ser): %s INFO: %s", secmodule.name, secmodule.info());
		      releasemodule(&secmodule);
		    }
                    continue;
                    break;
                  }
                  case 2: case 5: {
                    aflog(2, "  user[%d] (by ser): DROPPED BY MODULE", i);
                    close(contable[i].connfd);
                    FD_CLR(contable[i].connfd, &allset);
                    FD_CLR(contable[i].connfd, &wset);
                    contable[i].state = S_STATE_CLOSING;
                    freebuflist(&contable[i].head);
                    buff[0] = AF_S_CONCLOSED; /* closing connection */
                    buff[1] = i >> 8;	/* high bits of user number */
                    buff[2] = i;		/* low bits of user number */
                    send_message(type, master, buff, 5);
		    if (temp2 == 5) {
                      aflog(1, "RELEASED MODULE (ser): %s INFO: %s", secmodule.name, secmodule.info());
		      releasemodule(&secmodule);
		    }
		    continue;
                    break;
                  }
                  case 3: {
                    aflog(1, "RELEASED MODULE (ser): %s INFO: %s", secmodule.name, secmodule.info());
		    releasemodule(&secmodule);
                    break;
                  }
                }
              }
            buff[0] = AF_S_MESSAGE; /* sending message */
            buff[1] = i >> 8;	/* high bits of user number */
            buff[2] = i;		/* low bits of user number */
            buff[3] = n >> 8;	/* high bits of message length */
            buff[4] = n;		/* low bits of message length */
            aflog(2, "  user[%d]: TO msglen: %d [%d/%d]", i, n,
#ifdef SIOCOUTQ
			notsent
#else
			buflength - notsent
#endif
			, buflength);
            send_message(type, master, buff, n+5);
          }
          else if (!udp) {
            aflog(2, "  user[%d]: CLOSING", i);
            close(contable[i].connfd);
            FD_CLR(contable[i].connfd, &allset);
            FD_CLR(contable[i].connfd, &wset);
            contable[i].state = S_STATE_CLOSING;
            freebuflist(&contable[i].head);
            buff[0] = AF_S_CONCLOSED; /* closing connection */
            buff[1] = i >> 8;	/* high bits of user number */
            buff[2] = i;		/* low bits of user number */
            send_message(type, master, buff, 5);
          }
        } /* - FD_ISSET   CONTABLE[i].CONNFD   RSET */
      }
    }
    for (i = 0; i < usernum; ++i) {
      if (contable[i].state == S_STATE_STOPPED) {
        if (FD_ISSET(contable[i].connfd, &tmpset)) { /* FD_ISSET   CONTABLE[i].CONNFD   TMPSET */
          aflog(3, " user[%d]: FD_ISSET - WRITE", i);
          n = contable[i].head->msglen - contable[i].head->actptr;
          temp2 = write(contable[i].connfd, &(contable[i].head->buff[contable[i].head->actptr]), n);
          if ((temp2 > 0) && (temp2 != n)) {
            contable[i].head->actptr+=temp2;
          }
          else if ((temp2 == -1) && (errno == EAGAIN)) {
            aflog(3, " user[%d]: Couldn't write?", i);
          }
          else if (temp2 == -1) {
            close(contable[i].connfd);
            FD_CLR(contable[i].connfd, &allset);
            FD_CLR(contable[i].connfd, &wset);
            contable[i].state = S_STATE_CLOSING;
            buff[0] = AF_S_CONCLOSED; /* closing connection */
            buff[1] = i >> 8;	/* high bits of user number */
            buff[2] = i;		/* low bits of user number */
            send_message(type, master, buff, 5);
          }
          else {
            deleteblnode(&contable[i].head);
            if (contable[i].head == NULL) {
              contable[i].state = S_STATE_OPEN;
              FD_CLR(contable[i].connfd, &wset);
              buff[0] = AF_S_CAN_SEND; /* stopping transfer */
              buff[1] = i >> 8;       /* high bits of user number */
              buff[2] = i;            /* low bits of user number */
              aflog(3, "  FROM user[%d]: BUFFERING MESSAGE ENDED", i);
              send_message(type, master, buff, 5);
            }
          }
        } /* - FD_ISSET   CONTABLE[i].CONNFD   TMPSET */
      }
    }
    if (FD_ISSET(master.commfd, &rset)) { /* FD_ISSET   MASTER.COMMFD   RSET */
      aflog(3, " masterfd: FD_ISSET");
      n = get_message(type, master, buff, 5);
      if (n != 5) {
        aflog(2, "  FATAL ERROR! (%d)", n);
        if (n == -1) {
          if (TYPE_IS_SSL(type)) {
            merror = SSL_get_error(master.ssl, n);
            switch (merror) {
              case SSL_ERROR_NONE : {
                aflog(2, "FE: none");
                break;
              }
              case SSL_ERROR_ZERO_RETURN : {
                aflog(2, "FE: zero");
                break;
              }
              case SSL_ERROR_WANT_READ : {
                aflog(2, "FE: w_read");
                break;
              }
              case SSL_ERROR_WANT_WRITE : {
                aflog(2, "FE: w_write");
                break;
              }
              case SSL_ERROR_WANT_CONNECT : {
                aflog(2, "FE: w_connect");
                break;
              }
              case SSL_ERROR_WANT_X509_LOOKUP : {
                aflog(2, "FE: w_x509_lookup");
                break;
              }
              case SSL_ERROR_SYSCALL : {
                aflog(2, "FE: syscall");
                break;
              }
              case SSL_ERROR_SSL : {
                SSL_load_error_strings();
                aflog(2, "FE: ssl:%s",
                ERR_error_string(ERR_get_error(), (char*) buff));
                break;
              }
            }
            continue; /* what happened? */
          }
        }
        if (n != 0)
          exit(1);
      }
      if (n == 0) { /* server quits -> we do the same... */
        aflog(0, "  SERVER: premature quit -> exiting...");
        exit(1);
      }
      numofcon = buff[1];
      numofcon = numofcon << 8;
      numofcon += buff[2]; /* this is id of user */
      length = buff[3];
      length = length << 8;
      length += buff[4]; /* this is length of message */
      switch (buff[0]) {
        case AF_S_CONCLOSED : {
          if ((numofcon>=0) && (numofcon<=usernum)) {
            usercon--;
            if (contable[numofcon].state == S_STATE_CLOSING) {
              contable[numofcon].state = S_STATE_CLEAR;
              aflog(2, "  user[%d]: CLOSED", numofcon);
            }
            else if ((contable[numofcon].state==S_STATE_OPEN) || (contable[numofcon].state==S_STATE_STOPPED)){
              aflog(2, "  user[%d]: CLOSED", numofcon);
              close(contable[numofcon].connfd);
              FD_CLR(contable[numofcon].connfd, &allset);
              FD_CLR(contable[numofcon].connfd, &wset);
              contable[numofcon].state = S_STATE_CLEAR;
              freebuflist(&contable[numofcon].head);
              buff[0] = AF_S_CONCLOSED; /* closing connection */
              buff[1] = numofcon >> 8;		/* high bits of user number */
              buff[2] = numofcon;		/* low bits of user number */
              send_message(type, master, buff, 5);
            }
          }
          break;
        }
        case AF_S_CONOPEN : {
          if ((numofcon>=0) && (numofcon<=usernum)) {
            usercon++;
            if (contable[numofcon].state == S_STATE_CLEAR) {
              n = get_message(type, master, buff, length);
              memcpy(contable[numofcon].namebuf, buff, 128);
              memcpy(contable[numofcon].portbuf,&buff[128],7);
              aflog(2, "  user[%d]: OPENING", numofcon);
              aflog(1, "user[%d]: IP:%s PORT:%s", numofcon,
              contable[numofcon].namebuf, contable[numofcon].portbuf);
              if (ismloaded(&module) && module.allow(contable[numofcon].namebuf, contable[numofcon].portbuf)) {
                aflog(2, "   IT'S NOT ALLOWED - DROPPING", numofcon);
                buff[0] = AF_S_CANT_OPEN; /* not opening connection */
                buff[1] = numofcon >> 8;		/* high bits of user number */
                buff[2] = numofcon;		/* low bits of user number */
                send_message(type, master, buff, 5);
                usercon--;
                continue;
              }
              if (udp) {
                ipfam = 0;
              }
              else {
                ipfam = 0x01;
              }
              if (TYPE_IS_IPV4(type)) {
                ipfam |= 0x02;
              }
              else if (TYPE_IS_IPV6(type)) {
                ipfam |= 0x04;
              }
              if (ip_connect(&(contable[numofcon].connfd), desnam, despor, ipfam)) {
                aflog(2, "   CAN'T OPEN - DROPPING", numofcon);
                buff[0] = AF_S_CANT_OPEN; /* not opening connection */
                buff[1] = numofcon >> 8;		/* high bits of user number */
                buff[2] = numofcon;		/* low bits of user number */
                send_message(type, master, buff, 5);
                usercon--;
                continue;
              }
              temp2 = fcntl(contable[numofcon].connfd, F_GETFL, 0);
              fcntl(contable[numofcon].connfd, F_SETFL, temp2 | O_NONBLOCK);
              FD_SET(contable[numofcon].connfd, &allset);
              maxfdp1 = (maxfdp1 > (contable[numofcon].connfd+1)) ? maxfdp1 : (contable[numofcon].connfd+1);
              buff[0] = AF_S_CONOPEN; /* opening connection */
              buff[1] = numofcon >> 8;		/* high bits of user number */
              buff[2] = numofcon; 		/* low bits of user number */
              send_message(type, master, buff, 5);
              contable[numofcon].state = S_STATE_OPEN;
            }
          }
          break;
        }
        case AF_S_MESSAGE : {
          aflog(2, "  user[%d]: FROM msglen: %d", numofcon, length);
          n = get_message(type, master, buff, length);
          if ((numofcon>=0) && (numofcon<=usernum)) {
            if (contable[numofcon].state == S_STATE_OPEN) {
              if (ismloaded(&module)) {
                switch ((temp2 = module.filter(contable[numofcon].namebuf, buff, &n))) {
                  case 1: case 4:{
                    aflog(3, "  user[%d]: PACKET IGNORED BY MODULE", numofcon);
		    if (temp2 == 4) {
                      aflog(1, "RELEASED MODULE: %s INFO: %s", module.name, module.info());
		      releasemodule(&module);
		    }
                    continue;
                    break;
                  }
                  case 2: case 5:{
                    aflog(2, "  user[%d]: DROPPED BY MODULE", numofcon);
                    close(contable[numofcon].connfd);
                    FD_CLR(contable[numofcon].connfd, &allset);
                    FD_CLR(contable[numofcon].connfd, &wset);
                    contable[numofcon].state = S_STATE_CLOSING;
                    freebuflist(&contable[numofcon].head);
                    buff[0] = AF_S_CONCLOSED; /* closing connection */
                    buff[1] = numofcon >> 8;	/* high bits of user number */
                    buff[2] = numofcon;		/* low bits of user number */
                    send_message(type, master, buff, 5);
		    if (temp2 == 5) {
                      aflog(1, "RELEASED MODULE: %s INFO: %s", module.name, module.info());
		      releasemodule(&module);
		    }
		    continue;
                    break;
                  }
                  case 3: {
                    aflog(1, "RELEASED MODULE: %s INFO: %s", module.name, module.info());
		    releasemodule(&module);
                    break;
                  }
                }
              }
              aflog(2, "  user[%d]: FROM msglen: %d SENT", numofcon, n);
              temp2 = write(contable[numofcon].connfd, buff, n);
              if ((temp2 > 0) && (temp2 != n)) {
                insertblnode(&(contable[numofcon].head), temp2, n, buff);
                contable[numofcon].state = S_STATE_STOPPED;
                FD_SET(contable[numofcon].connfd, &wset);
                buff[0] = AF_S_DONT_SEND; /* stopping transfer */
                buff[1] = numofcon >> 8;        /* high bits of user number */
                buff[2] = numofcon;             /* low bits of user number */
                aflog(3, "   FROM user[%d]: BUFFERING MESSAGE STARTED", numofcon);
                send_message(type, master, buff, 5);
              }
              else if ((temp2 == -1) && (errno == EAGAIN)) {
                insertblnode(&(contable[numofcon].head), 0, n, buff);
                contable[numofcon].state = S_STATE_STOPPED;
                FD_SET(contable[numofcon].connfd, &wset);
                buff[0] = AF_S_DONT_SEND; /* stopping transfer */
                buff[1] = numofcon >> 8;        /* high bits of user number */
                buff[2] = numofcon;             /* low bits of user number */
                aflog(3, "   FROM user[%d]: BUFFERING MESSAGE STARTED", numofcon);
                send_message(type, master, buff, 5);
              }
              else if (temp2 == -1) {
                close(contable[numofcon].connfd);
                FD_CLR(contable[numofcon].connfd, &allset);
                FD_CLR(contable[numofcon].connfd, &wset);
                contable[numofcon].state = S_STATE_CLOSING;
                freebuflist(&contable[numofcon].head);
                buff[0] = AF_S_CONCLOSED; /* closing connection */
                buff[1] = numofcon >> 8;	/* high bits of user number */
                buff[2] = numofcon;		/* low bits of user number */
                send_message(type, master, buff, 5);
              }
            }
            else if (contable[numofcon].state == S_STATE_STOPPED) {
              aflog(3, "   FROM user[%d]: BUFFERING MESSAGE", numofcon);
              insertblnode(&(contable[numofcon].head), 0, n, buff);
            }
          }
          break;
        }
        case AF_S_CLOSING : { /* server shut down -> exiting... */
          aflog(0, "  SERVER: CLOSED -> exiting... cg: %ld bytes", getcg());
          exit(1);
          break;
        }
        case AF_S_DONT_SEND: {
          FD_CLR(contable[numofcon].connfd, &allset);
          break;
        }
        case AF_S_CAN_SEND: {
          FD_SET(contable[numofcon].connfd, &allset);
          break;
        }
        default : { /* unrecognized type of message -> exiting... */
          aflog(0, "  SERVER: unrecognized message -> exiting... cg: %ld bytes", getcg());
          exit(1);
          break;
        }
      }
    } /* - FD_ISSET   MASTER.COMMFD   RSET */
  }
}

static void
usage(char* info)
{
  printf("\n%s\n\n", info);
  printf("  Options:\n");
  printf("  -h, --help          - prints this help\n");
  printf("  -n, --servername    - where the second part of the active\n");
  printf("                        port forwarder is running (required)\n");
  printf("  -m, --manageport    - manage port number - server must be\n");
  printf("                        listening on it (default: 50126)\n");
  printf("  -d, --hostname      - the name of this host/remote host - the final\n");
  printf("                        destination of the packets (default: the name\n");
  printf("                        returned by hostname function)\n");
  printf("  -p, --portnum       - the port we are forwarding connection to (required)\n");
  printf("  -k, --keyfile       - the name of the file with RSA key (default: (none))\n");
  printf("  -u, --udpmode       - udp mode - client will use udp protocol to\n");
  printf("                        communicate with the hostname\n");
  printf("  -U, --reverseudp    - reverse udp forwarding. Udp packets will be forwarded\n");
  printf("                        from hostname:portnum (-p) to the server name:portnum\n");
  printf("                        (-m)\n");
  printf("  -O, --heavylog      - logging everything to a logfile\n");
  printf("  -o, --lightlog      - logging some data to a logfile\n");
  printf("  -v, --verbose       - to be verbose - program won't enter the daemon mode\n");
  printf("                        (use several times for greater effect)\n");
  printf("  --pass              - set the password used for client identification\n");
  printf("                        (default: no password)\n");
  printf("  -4, --ipv4          - use ipv4 only\n");
  printf("  -6, --ipv6          - use ipv6 only\n");
  printf("  -l, --load          - load a module for user's packets filtering\n");
  printf("  -L, --Load          - load a module for service's packets filtering\n\n");

  exit(0);
}

static void
sig_int(int signo)
{
  aflog(1, "CLIENT CLOSED cg: %ld bytes", getcg());
  exit(0);
}

void
callback(int i, int j, void* k)
{
  if (k == NULL) {
    printf("%d", i);
    fflush(stdout);
  }
}
