#ifndef __ETHERNET_MANAGER_H__
#define  __ETHERNET_MANAGER_H__

typedef enum{
    EthernetManagerOK = 0,
    EthernetManagerPHYError,
    EthernetManagerDHCPError,
}EthernetManagerError;

EthernetManagerError ethernet_init();
EthernetManagerError ethernet_connect();
//EthernetManagerError ethernet_disconnect();

#endif