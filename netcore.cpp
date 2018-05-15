#include "skia.hpp"
#include <xpc/xpc.h>
#include <FunctionHook.h>

typedef void * tcp_connection_t;
typedef void * nw_endpoint_t;
typedef void * nw_path_t;

enum nw_endpoint_type {
    nw_endpoint_type_invalid = 0,
    nw_endpoint_type_address = 1,
    nw_endpoint_type_hostname = 2,
    nw_endpoint_type_bonjour = 3,
    nw_endpoint_type_ledbelly = 4,
};

enum network_proxy_type {
    network_proxy_type_direct = 1,
    network_proxy_type_pac_script = 1001,
    network_proxy_type_pac_url = 1002,
    network_proxy_type_http = 2001,
    network_proxy_type_https = 2002,
    network_proxy_type_ftp = 2003,
    network_proxy_type_gopher = 2004,
    network_proxy_type_socks_v4 = 3001,
    network_proxy_type_socks_v5 = 3002,
};

#define LazyFunction(type, name, args...) static type (* const name)(args) = reinterpret_cast<decltype(name)>(MSFindSymbol(NULL, "_" # name))
LazyFunction(nw_endpoint_t, tcp_connection_get_first_endpoint, tcp_connection_t connection);
LazyFunction(nw_endpoint_type, nw_endpoint_get_type, nw_endpoint_t endpoint);
LazyFunction(const void *, nw_endpoint_get_address, nw_endpoint_t endpoint);
LazyFunction(int, nw_endpoint_get_address_family, nw_endpoint_t endpoint);
LazyFunction(const char *, nw_endpoint_get_hostname, nw_endpoint_t endpoint);
LazyFunction(in_port_t, nw_endpoint_get_port, nw_endpoint_t endpoint);
LazyFunction(bool, nw_endpoint_is_local_domain, nw_endpoint_t endpoint);
#undef LazyFunction

static pthread_key_t proxy_key;

static const socket_address *get_proxy() {
    return reinterpret_cast<socket_address *>(pthread_getspecific(proxy_key));
}

static void set_proxy(const socket_address *proxy) {
    pthread_setspecific(proxy_key, proxy);
}

static socket_address proxy_for_endpoint(nw_endpoint_t endpoint) {
    socket_address proxy;
    if (endpoint == NULL) {
        return proxy;
    }
    nw_endpoint_type type = nw_endpoint_get_type(endpoint);
    if (!((type == nw_endpoint_type_address || type == nw_endpoint_type_hostname) && !nw_endpoint_is_local_domain(endpoint))) {
        return proxy;
    }
    std::string name;
    in_port_t port = nw_endpoint_get_port(endpoint);
    if (type == nw_endpoint_type_address) {
        int af = nw_endpoint_get_address_family(endpoint);
        if (!(af == AF_INET || af == AF_INET6)) {
            return proxy;
        }
        bool ipv6 = af == AF_INET6;
        size_t address_len = ipv6 ? sizeof(struct in6_addr) : sizeof(struct in_addr);
        struct in6_addr address;
        memcpy(&address, nw_endpoint_get_address(endpoint), address_len);
        if (skia::instance().should_bypass(address, port, ipv6)) {
            return proxy;
        }
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(af, &address, buffer, sizeof(buffer)) != NULL) {
            name = buffer;
        }
    } else if (type == nw_endpoint_type_hostname) {
        name = nw_endpoint_get_hostname(endpoint) ?: "";
        struct in6_addr buffer;
        if (inet_aton(name.c_str(), reinterpret_cast<struct in_addr *>(&buffer)) == 1) {
            if (skia::instance().should_bypass(buffer, port, false)) {
                return proxy;
            }
        } else {
            if (skia::instance().should_bypass(name, "")) {
                return proxy;
            }
        }
    }
    if (name.length() == 0) {
        return proxy;
    }
    proxy = skia::instance().query_proxy(name, ntohs(port));
    if (proxy.addr == 0) {
        log("direct connect: %s:%u...applied", name.c_str(), ntohs(port));
    } else {
        log("proxied connect: %s:%u...%s:%u...applied", inet_ntoa(*reinterpret_cast<const struct in_addr *>(&proxy.addr)), ntohs(proxy.port), name.c_str(), ntohs(port));
    }
    return proxy;
}

FHFunction(void, tcp_connection_handle_path_changed, tcp_connection_t connection, xpc_object_t path_dictionary, xpc_object_t connected_path_dictionary) {
    socket_address proxy = proxy_for_endpoint(tcp_connection_get_first_endpoint(connection));
    if (proxy.addr != 0) {
        set_proxy(&proxy);
    }
    FHOriginal(tcp_connection_handle_path_changed)(connection, path_dictionary, connected_path_dictionary);
    set_proxy(NULL);
}

FHFunction(xpc_object_t, nw_path_copy_proxy_settings, nw_path_t path) {
    const socket_address *proxy = get_proxy();
    if (proxy != NULL && proxy->addr != 0) {
        xpc_object_t proxy_dictionary = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_int64(proxy_dictionary, "proxy_type", network_proxy_type_socks_v5);
        xpc_dictionary_set_string(proxy_dictionary, "proxy_host", inet_ntoa(*reinterpret_cast<const struct in_addr *>(&proxy->addr)));
        xpc_dictionary_set_int64(proxy_dictionary, "proxy_port", proxy->port);
        xpc_object_t proxies_array = xpc_array_create(&proxy_dictionary, 1);
        xpc_release(proxy_dictionary);
        return proxies_array;
    } else {
        return FHOriginal(nw_path_copy_proxy_settings)(path);
    }
}

FHConstructor {
    pthread_key_create(&proxy_key, NULL);
    FHHook(tcp_connection_handle_path_changed);
    FHHook(nw_path_copy_proxy_settings);
}

#define DEBUG_NETCORE 1

#if DEBUG && DEBUG_NETCORE

#include <asl.h>

FHFunction(asl_object_t, asl_open, const char *ident, const char *facility, uint32_t opts) {
    return FHOriginal(asl_open)(ident, facility, opts | ASL_OPT_STDERR);
}

FHFunction(int, netcore_logging_at_level, int level) {
    return 1;
}

FHFunction(void, netcore_log_init_once) {
    asl_object_t *logClient = reinterpret_cast<asl_object_t *>(MSFindSymbol(NULL, "_gLogClient"));
    int *logFilter = reinterpret_cast<int *>(MSFindSymbol(NULL, "_gLogFilter"));
    int *logDatapath = reinterpret_cast<int *>(MSFindSymbol(NULL, "_gLogDatapath"));
    FHOriginal(netcore_log_init_once);
    asl_set_filter(*logClient, ASL_FILTER_MASK_UPTO(ASL_LEVEL_DEBUG));
    *logFilter = 7;
    *logDatapath = 1;
}

FHConstructor {
    FHHook(asl_open);
    FHHook(netcore_logging_at_level);
    FHHook(netcore_log_init_once);
}

#endif
