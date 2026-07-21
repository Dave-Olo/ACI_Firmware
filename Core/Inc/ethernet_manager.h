#ifndef __ETHERNET_MANAGER_H__
#define  __ETHERNET_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum{
    EthernetManagerOK = 0,
    EthernetManagerPHYError,
    EthernetManagerDHCPError,
}EthernetManagerError;

EthernetManagerError ethernet_init();
EthernetManagerError ethernet_connect();

/* Resolve a hostname via DNS. Returns true on success and fills ip_out[4]. */
bool ethernet_dns_resolve(const char* hostname, uint8_t ip_out[4]);

#endif