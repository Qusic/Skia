#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <arpa/inet.h>
#include <sys/syslog.h>
#include "config.hpp"

#define log_(level, format, args...) syslog(LOG_##level, "Skia: " format, ##args)
#define log(format, args...) log_(NOTICE, format, ##args)
#define err(format, args...) log_(ERR, format, ##args)

#define DEBUG 0

#if DEBUG
#define debug(code) ({ (code); })
#else
#define debug(code)
#endif

struct socket_address {
    in_addr_t addr = 0;
    in_port_t port = 0;
    socket_address() {}
    socket_address(in_addr_t addr, in_port_t port): addr(htonl(addr)), port(htons(port)) {}
};

struct socket_network {
    in_addr_t addr = 0, mask = 0;
    in_port_t port = 0;
    socket_network() {}
    socket_network(in_addr_t addr, in_addr_t mask, in_port_t port): addr(htonl(addr)), mask(htonl(mask)), port(htons(port)) {}
};

class skia {
private:
    std::unordered_map<std::string, socket_address> proxy_cache;
    std::shared_timed_mutex mutex;
    config proxy_config;
    const std::vector<socket_network> bypass_networks = {
        socket_network(0x7f000000, 0xff000000, 0), // loopback 127.0.0.0/255.0.0.0
        socket_network(0x0a000000, 0xff000000, 0), // private network 10.0.0.0/255.0.0.0
        socket_network(0xac100000, 0xfff00000, 0), // private network 172.16.0.0/255.240.0.0
        socket_network(0xc0a80000, 0xffff0000, 0), // private network 192.168.0.0/255.255.0.0
    };
    skia() {}
    ~skia() {}
    std::string current_application();
    socket_address query_proxy(const std::string &target_name, const uint16_t &target_port, bool &no_cache);
public:
    static skia &instance();
    bool should_bypass(const int &sock);
    bool should_bypass(const struct sockaddr *addr);
    bool should_bypass(const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6);
    bool should_bypass(const std::string &target_name, const std::string &target_serv);
    void extract_target(const struct sockaddr *addr, struct in6_addr &target_addr, in_port_t &target_port, bool &ipv6);
    socket_address query_proxy(const std::string &target_name, const uint16_t &target_port);
    socket_address query_proxy(const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6);
};

class resolve_table {
private:
    std::unordered_map<std::string, size_t> name_table;
    std::unordered_map<size_t, std::string> index_table;
    std::shared_timed_mutex mutex;
    size_t index = 0;
    const uint8_t addr_prefix = 240;
    const uint8_t bits_count = (sizeof(in_addr_t) - sizeof(addr_prefix)) * 8;
    const size_t addr_count = (1 << bits_count) - 1;
    resolve_table() {}
    ~resolve_table() {}
    size_t make_index(const std::string &name);
    size_t name_to_index(const std::string &name);
    std::string index_to_name(const size_t &index);
    in_addr_t index_to_addr(const size_t &index);
    size_t addr_to_index(const in_addr_t &addr);
public:
    static resolve_table &instance();
    in_addr_t name_to_addr(const std::string &name);
    std::string addr_to_name(const in_addr_t &addr);
    bool is_resolved_addr(const in_addr_t &addr);
};
