#ifndef BARPA_INET_H
#define BARPA_INET_H

#include "../config.h"

#ifdef CONFIG_BEOS_NETSERVER

# include <socket.h>
int inet_aton (const char * str, struct in_addr * add);
# define PF_INET AF_INET
# define SO_SNDBUF 0x40000001

/* fake */
struct ip_mreq {
    struct in_addr imr_multiaddr;  /* IP multicast address of group */
    struct in_addr imr_interface;  /* local IP address of interface */
};

#include <netdb.h>

#else
# include <arpa/inet.h>
#endif

#endif /* BARPA_INET_H */
