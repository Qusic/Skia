#include "skia.hpp"
#include <stdexcept>
#include <netdb.h>
#include <netdb_async.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <FunctionHook.h>

FHOriginalPrototype(int, connect)(int sock, const struct sockaddr *addr, socklen_t addr_len);
FHOriginalPrototype(struct hostent *, gethostbyname)(const char *name);
FHOriginalPrototype(struct hostent *, gethostbyaddr)(const void *addr, socklen_t len, int type);
FHOriginalPrototype(int, getaddrinfo)(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
FHOriginalPrototype(int, getnameinfo)(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags);

static void try_select(int sock, bool for_write) {
    try {
        fd_set sock_set;
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        while (true) {
            FD_ZERO(&sock_set);
            FD_SET(sock, &sock_set);
            int result = select(sock + 1, for_write ? NULL : &sock_set, for_write ? &sock_set : NULL, NULL, &timeout);
            if (result > 0) {
                int error;
                socklen_t error_len = sizeof(error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &error_len);
                if (error == 0) {
                    return;
                } else {
                    throw std::runtime_error(strerror(error));
                }
            } else if (result == -1 && errno == EINTR) {
                continue;
            } else {
                throw std::runtime_error("timed out");
            }
        }
    } catch (const std::runtime_error &error) {
        throw std::runtime_error(std::string("select: ") + error.what());
    }
}

static void timed_connect(int sock, const struct sockaddr *addr, socklen_t addr_len) {
    try {
        class socket_nonblocker {
        private:
            int sock;
        public:
            socket_nonblocker(int sock): sock(sock) {
                fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, NULL) | O_NONBLOCK);
            }
            ~socket_nonblocker() {
                fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, NULL) & ~O_NONBLOCK);
            }
        };
        socket_nonblocker nonblocker(sock);
        if (FHOriginal(connect)(sock, addr, addr_len) == -1 && errno == EINPROGRESS) {
            try_select(sock, true);
            return;
        } else {
            throw std::runtime_error(strerror(errno));
        }
    } catch (const std::runtime_error &error) {
        throw std::runtime_error(std::string("connect: ") + error.what());
    }
}

static void send_bytes(int sock, const uint8_t *bytes, size_t len) {
    try {
        ssize_t current = 0;
        size_t total = 0;
        while (total < len) {
            current = send(sock, bytes + total, len - total, 0);
            if (current > 0) {
                total += current;
            } else if (current < 0) {
                throw std::runtime_error(strerror(errno));
            } else {
                throw std::runtime_error("closed");
            }
        }
    } catch (const std::runtime_error &error) {
        throw std::runtime_error(std::string("send: ") + error.what());
    }
}

static void recv_bytes(int sock, uint8_t *bytes, size_t len) {
    try {
        ssize_t current = 0;
        size_t total = 0;
        while (total < len) {
            try_select(sock, false);
            current = recv(sock, bytes + total, len - total, 0);
            if (current > 0) {
                total += current;
            } else if (current < 0) {
                throw std::runtime_error(strerror(errno));
            } else {
                throw std::runtime_error("closed");
            }
        }
    } catch (const std::runtime_error &error) {
        throw std::runtime_error(std::string("recv: ") + error.what());
    }
}

static bool make_direct(int &sock, const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6) {
    bool result = false;
    std::string target_name;
    std::string target_serv = std::to_string(ntohs(target_port));
    struct addrinfo *addr_info_list, hints;
    hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_IP;
    hints.ai_family = AF_UNSPEC;
    if (!ipv6 && resolve_table::instance().is_resolved_addr(target_addr.__u6_addr.__u6_addr32[0])) {
        target_name = resolve_table::instance().addr_to_name(reinterpret_cast<const in_addr *>(&target_addr)->s_addr);
    } else {
        char target_name_buffer[INET6_ADDRSTRLEN];
        target_name = inet_ntop(ipv6 ? AF_INET6 : AF_INET, &target_addr, target_name_buffer, sizeof(target_name_buffer));
        hints.ai_flags |= AI_NUMERICHOST;
        hints.ai_family = ipv6 ? AF_INET6 : AF_INET;
    }
    std::string log_str = target_name + ":" + target_serv + "...";
    if (FHOriginal(getaddrinfo)(target_name.c_str(), target_serv.c_str(), NULL, &addr_info_list) == 0) {
        for (struct addrinfo *addr_info = addr_info_list; addr_info != NULL; addr_info = addr_info->ai_next) {
            sock = socket(addr_info->ai_family, hints.ai_socktype, 0);
            if (sock == -1) {
                err("direct connect failed: %s%s", log_str.c_str(), strerror(errno));
                continue;
            }
            try {
                timed_connect(sock, addr_info->ai_addr, addr_info->ai_addrlen);
                result = true;
                log("direct connect: %s%s", log_str.c_str(), "ok");
                break;
            } catch (const std::runtime_error &error) {
                close(sock);
                err("direct connect failed: %s%s", log_str.c_str(), error.what());
                continue;
            }
        }
        freeaddrinfo(addr_info_list);
    } else {
        err("direct connect failed: %s%s", log_str.c_str(), strerror(errno));
    }
    return result;
}

static bool make_proxied(int &sock, const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6, const socket_address &proxy) {
    try {
        if (resolve_table::instance().is_resolved_addr(target_addr.__u6_addr.__u6_addr32[0]) && resolve_table::instance().addr_to_name(reinterpret_cast<const in_addr *>(&target_addr)->s_addr).length() == 0) {
            throw std::runtime_error("invalid resolved address");
        }

        sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            throw std::runtime_error(strerror(errno));
        }

        struct sockaddr_in proxy_addr;
        memset(&proxy_addr, 0, sizeof(proxy_addr));
        proxy_addr.sin_len = sizeof(proxy_addr);
        proxy_addr.sin_family = AF_INET;
        proxy_addr.sin_addr.s_addr = proxy.addr;
        proxy_addr.sin_port = proxy.port;
        timed_connect(sock, reinterpret_cast<struct sockaddr *>(&proxy_addr), sizeof(proxy_addr));

        uint8_t buffer[1024];
        // SOCK 5 negotiation
        buffer[0] = 5; // version
        buffer[1] = 1; // number of methods
        buffer[2] = 0; // method #1: no authentication required
        send_bytes(sock, buffer, 3);
        recv_bytes(sock, buffer, 2);
        if (buffer[0] != 5) {
            throw std::runtime_error("invalid proxy");
        }
        if (buffer[1] != 0) {
            throw std::runtime_error("proxy authentication required");
        }
        // SOCK5 request
        buffer[0] = 5; // version
        buffer[1] = 1; // command: connect
        buffer[2] = 0; // reserved
        std::string target_str;
        if (ipv6) {
            buffer[3] = 4; // address type = ipv6
            send_bytes(sock, buffer, 4);
            send_bytes(sock, reinterpret_cast<const uint8_t *>(&target_addr), sizeof(struct in6_addr));
            send_bytes(sock, reinterpret_cast<const uint8_t *>(&target_port), sizeof(in_port_t));
            target_str = inet_ntop(AF_INET6, &target_addr, reinterpret_cast<char *>(buffer), sizeof(buffer)) + std::string(":") + std::to_string(ntohs(target_port));
        } else if (resolve_table::instance().is_resolved_addr(target_addr.__u6_addr.__u6_addr32[0])) {
            buffer[3] = 3; // address type = name
            send_bytes(sock, buffer, 4);
            std::string target_name = resolve_table::instance().addr_to_name(reinterpret_cast<const in_addr *>(&target_addr)->s_addr);
            buffer[0] = std::min(target_name.length(), static_cast<size_t>(UINT8_MAX));
            send_bytes(sock, buffer, 1);
            send_bytes(sock, reinterpret_cast<const uint8_t *>(target_name.c_str()), buffer[0]);
            send_bytes(sock, reinterpret_cast<const uint8_t *>(&target_port), sizeof(in_port_t));
            target_str = target_name + ":" + std::to_string(ntohs(target_port));
        } else {
            buffer[3] = 1; // address type = ipv4
            send_bytes(sock, buffer, 4);
            send_bytes(sock, reinterpret_cast<const uint8_t *>(&target_addr), sizeof(struct in_addr));
            send_bytes(sock, reinterpret_cast<const uint8_t *>(&target_port), sizeof(in_port_t));
            target_str = inet_ntop(AF_INET, &target_addr, reinterpret_cast<char *>(buffer), sizeof(buffer)) + std::string(":") + std::to_string(ntohs(target_port));
        }
        recv_bytes(sock, buffer, 4);
        std::string log_str = std::string(inet_ntoa(*reinterpret_cast<const struct in_addr *>(&proxy.addr))) + ":" + std::to_string(ntohs(proxy.port)) + "..." + target_str + "...";
        if (buffer[0] != 5) {
            throw std::runtime_error(log_str + "invalid proxy");
        }
        switch (buffer[1]) {
            case 0: break;
            case 1: throw std::runtime_error(log_str + "general failure");
            case 2: throw std::runtime_error(log_str + "connection not allowed");
            case 3: throw std::runtime_error(log_str + "network unreachable");
            case 4: throw std::runtime_error(log_str + "host unreachable");
            case 5: throw std::runtime_error(log_str + "connection refused");
            case 6: throw std::runtime_error(log_str + "ttl expired");
            case 7: throw std::runtime_error(log_str + "command not supported");
            case 8: throw std::runtime_error(log_str + "address type not supported");
            default: throw std::runtime_error(log_str + "unknown error " + std::to_string(buffer[1]));
        }
        size_t remaining_len = 0;
        switch (buffer[3]) {
            case 1:
                remaining_len = sizeof(struct in_addr) + sizeof(in_port_t);
                break;
            case 3:
                recv_bytes(sock, buffer, 1);
                remaining_len = buffer[0] + sizeof(in_port_t);
                break;
            case 4:
                remaining_len = sizeof(struct in6_addr) + sizeof(in_port_t);
                break;
            default:
                throw std::runtime_error(log_str + "replied address type not supported");
        }
        recv_bytes(sock, buffer, remaining_len);
        log("proxied connect: %s%s", log_str.c_str(), "ok");
        return true;
    } catch (const std::runtime_error &error) {
        close(sock);
        err("proxied connect failed: %s", error.what());
        return false;
    }
}

FHReplacedPrototype(int, connect)(int sock, const struct sockaddr *addr, socklen_t addr_len) {
    if (skia::instance().should_bypass(sock) || skia::instance().should_bypass(addr)) {
        return FHOriginal(connect)(sock, addr, addr_len);
    }

    struct in6_addr target_addr;
    in_port_t target_port;
    bool ipv6;
    skia::instance().extract_target(addr, target_addr, target_port, ipv6);
    debug({
        char buffer[INET6_ADDRSTRLEN];
        log("connect: %s:%u", inet_ntop(ipv6 ? AF_INET6 : AF_INET, &target_addr, buffer, sizeof(buffer)), ntohs(target_port));
    });

    if (skia::instance().should_bypass(target_addr, target_port, ipv6)) {
        return FHOriginal(connect)(sock, addr, addr_len);
    }

    int new_sock;
    socket_address proxy = skia::instance().query_proxy(target_addr, target_port, ipv6);
    bool result = proxy.addr == 0 ? make_direct(new_sock, target_addr, target_port, ipv6) : make_proxied(new_sock, target_addr, target_port, ipv6, proxy);
    if (result) {
        bool nonblock = fcntl(sock, F_GETFL, NULL) & O_NONBLOCK;
        dup2(new_sock, sock);
        close(new_sock);
        if (nonblock) {
            fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, NULL) | O_NONBLOCK);
            errno = EINPROGRESS;
            return -1;
        } else {
            errno = 0;
            return 0;
        }
    } else {
        errno = ETIMEDOUT;
        return -1;
    }
}

FHReplacedPrototype(struct hostent *, gethostbyname)(const char *name) {
    if (skia::instance().should_bypass(name ?: "", "")) {
        return FHOriginal(gethostbyname)(name);
    }

    static struct hostent result;
    static char result_name[NI_MAXHOST];
    static in_addr_t result_addr;
    static char *result_list[2];
    strlcpy(result_name, name, sizeof(result_name));
    result_addr = resolve_table::instance().name_to_addr(result_name);
    result_list[0] = reinterpret_cast<char *>(&result_addr);
    result_list[1] = NULL;
    result.h_addrtype = AF_INET;
    result.h_length = sizeof(struct in_addr);
    result.h_name = result_name;
    result.h_addr_list = result_list + 0;
    result.h_aliases = result_list + 1;

    debug({
        char buffer[INET6_ADDRSTRLEN];
        log("gethostbyname: %s -> %s", name, inet_ntop(AF_INET, &result.h_addr_list, buffer, sizeof(buffer)));
    });
    return &result;
}

FHReplacedPrototype(struct hostent *, gethostbyaddr)(const void *addr, socklen_t len, int type) {
    if (addr == NULL) {
        return FHOriginal(gethostbyaddr)(addr, len, type);
    }

    if (!(len == sizeof(struct in_addr) && type == AF_INET && resolve_table::instance().is_resolved_addr(*reinterpret_cast<const in_addr_t *>(addr)))) {
        return FHOriginal(gethostbyaddr)(addr, len, type);
    }

    static struct hostent result;
    static in_addr_t result_addr;
    static char result_name[NI_MAXHOST];
    static char *result_list[2];
    result_addr = *reinterpret_cast<const in_addr_t *>(addr);
    strlcpy(result_name, resolve_table::instance().addr_to_name(result_addr).c_str(), sizeof(result_name));
    result_list[0] = reinterpret_cast<char *>(&result_addr);
    result_list[1] = NULL;
    result.h_addrtype = AF_INET;
    result.h_length = sizeof(struct in_addr);
    result.h_name = result_name;
    result.h_addr_list = result_list + 0;
    result.h_aliases = result_list + 1;

    debug({
        char buffer[INET6_ADDRSTRLEN];
        log("gethostbyaddr: %s -> %s", inet_ntop(type, addr, buffer, sizeof(buffer)), result.h_name);
    });
    return &result;
}

static pthread_key_t resolve_key;

static bool get_should_resolve() {
    return pthread_getspecific(resolve_key) != NULL;
}

static void set_should_resolve(bool should_resolve) {
    pthread_setspecific(resolve_key, should_resolve ? &resolve_key : NULL);
}

static bool getaddrinfo_test(const char *hostname, const char *servname, const struct addrinfo *hints) {
    if (hints && (hints->ai_flags & AI_NUMERICHOST)) {
        return true;
    }
    if (skia::instance().should_bypass(hostname ?: "", servname ?: "")) {
        return true;
    }
    return get_should_resolve();
}

static int getaddrinfo_resolve(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    struct addrinfo *result = reinterpret_cast<struct addrinfo *>(malloc(sizeof(struct addrinfo)));
    struct sockaddr_in *result_addr = reinterpret_cast<struct sockaddr_in *>(malloc(sizeof(struct sockaddr_in)));
    char *result_name = reinterpret_cast<char *>(calloc(NI_MAXHOST, sizeof(char)));
    if (result == NULL || result_addr == NULL || result_name == NULL) {
        free(result);
        free(result_addr);
        free(result_name);
        return EAI_MEMORY;
    }
    memset(result_addr, 0, sizeof(struct sockaddr_in));
    result_addr->sin_len = sizeof(struct sockaddr_in);
    result_addr->sin_family = AF_INET;
    result_addr->sin_addr.s_addr = resolve_table::instance().name_to_addr(hostname);
    result_addr->sin_port = 0;
    if (servname) {
        struct servent *serv = getservbyname(servname, NULL);
        result_addr->sin_port = serv ? serv->s_port : htons(atoi(servname));
    }
    strlcpy(result_name, hostname, NI_MAXHOST);
    result->ai_flags = hints ? hints->ai_flags : AI_ADDRCONFIG;
    result->ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
    result->ai_protocol = hints ? hints->ai_protocol : IPPROTO_IPV4;
    result->ai_family = AF_INET;
    result->ai_addrlen = sizeof(struct sockaddr_in);
    result->ai_addr = reinterpret_cast<struct sockaddr *>(result_addr);
    result->ai_canonname = result_name;
    result->ai_next = NULL;

    debug({
        char buffer[INET6_ADDRSTRLEN];
        log("getaddrinfo: %s:%s -> %s:%u", hostname, servname, inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in *>(result->ai_addr)->sin_addr, buffer, sizeof(buffer)), ntohs(reinterpret_cast<struct sockaddr_in *>(result->ai_addr)->sin_port));
    });
    *res = result;
    return 0;
}

FHReplacedPrototype(int, getaddrinfo)(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (getaddrinfo_test(hostname, servname, hints)) {
        return FHOriginal(getaddrinfo)(hostname, servname, hints, res);
    }
    return getaddrinfo_resolve(hostname, servname, hints, res);
}

FHFunction(int, getaddrinfo_async_start, mach_port_t *p, const char *hostname, const char *servname, const struct addrinfo *hints, getaddrinfo_async_callback callback, void *context) {
    if (getaddrinfo_test(hostname, servname, hints)) {
        return FHOriginal(getaddrinfo_async_start)(p, hostname, servname, hints, callback, context);
    }
    int status = EAI_MEMORY;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, p) == 0) {
        *p = MACH_PORT_NULL;
        return status;
    }
    struct addrinfo *res = NULL;
    if ((status = getaddrinfo_resolve(hostname, servname, hints, &res)) != 0) {
        mach_port_destroy(mach_task_self(), *p);
        *p = MACH_PORT_NULL;
        return status;
    }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        callback(status, res, context);
    });
    return status;
}

static bool getnameinfo_test(const struct sockaddr *sa, socklen_t salen, int flags) {
    if (sa == NULL) {
        return true;
    }
    if (!(salen == sizeof(struct sockaddr_in) && sa->sa_family == AF_INET && resolve_table::instance().is_resolved_addr(reinterpret_cast<const struct sockaddr_in *>(sa)->sin_addr.s_addr))) {
        return true;
    }
    return get_should_resolve();
}

static int getnameinfo_resolve(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags) {
    const struct sockaddr_in *sa_in = reinterpret_cast<const struct sockaddr_in *>(sa);
    if (host != NULL && hostlen > 0) {
        if (strlcpy(host, resolve_table::instance().addr_to_name(sa_in->sin_addr.s_addr).c_str(), hostlen) >= hostlen) {
            return EAI_OVERFLOW;
        }
    }
    if (serv != NULL && servlen > 0) {
        struct servent *serv_ent = getservbyport(sa_in->sin_port, NULL);
        if (serv_ent) {
            if (strlcpy(serv, serv_ent->s_name, servlen) >= servlen) {
                return EAI_OVERFLOW;
            }
        } else {
            if (snprintf(serv, servlen, "%d", ntohs(sa_in->sin_port)) >= servlen) {
                return EAI_OVERFLOW;
            }
        }
    }

    debug({
        char buffer[INET6_ADDRSTRLEN];
        log("getnameinfo: %s:%u -> %s:%s", inet_ntop(sa->sa_family, &reinterpret_cast<const struct sockaddr_in *>(sa)->sin_addr, buffer, sizeof(buffer)), ntohs(reinterpret_cast<const struct sockaddr_in *>(sa)->sin_port), host, serv);
    });
    return 0;
}

FHReplacedPrototype(int, getnameinfo)(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen, char *serv, socklen_t servlen, int flags) {
    if (getnameinfo_test(sa, salen, flags)) {
        return FHOriginal(getnameinfo)(sa, salen, host, hostlen, serv, servlen, flags);
    }
    return getnameinfo_resolve(sa, salen, host, hostlen, serv, servlen, flags);
}

FHFunction(int, getnameinfo_async_start, mach_port_t *p, const struct sockaddr *sa, socklen_t salen, int flags, getnameinfo_async_callback callback, void *context) {
    if (getnameinfo_test(sa, salen, flags)) {
        return FHOriginal(getnameinfo_async_start)(p,sa, salen, flags, callback, context);
    }
    int status = EAI_MEMORY;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, p) == 0) {
        *p = MACH_PORT_NULL;
        return status;
    }
    char *host = reinterpret_cast<char *>(calloc(NI_MAXHOST, sizeof(char)));
    char *serv = reinterpret_cast<char *>(calloc(NI_MAXSERV, sizeof(char)));
    if (host == NULL || serv == NULL || (status = getnameinfo_resolve(sa, salen, host, sizeof(host), serv, sizeof(serv), flags)) != 0) {
        mach_port_destroy(mach_task_self(), *p);
        *p = MACH_PORT_NULL;
        free(host);
        free(serv);
        return status;
    }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        callback(status, host, serv, context);
    });
    return status;
}

FHFunction(JSValueRef, _ZL29_JSDnsResolveFunctionCallbackPK15OpaqueJSContextP13OpaqueJSValueS3_mPKPKS2_PS5_, JSContextRef context, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef *exception) {
    set_should_resolve(true);
    JSValueRef value = FHOriginal(_ZL29_JSDnsResolveFunctionCallbackPK15OpaqueJSContextP13OpaqueJSValueS3_mPKPKS2_PS5_)(context, function, thisObject, argumentCount, arguments, exception);
    set_should_resolve(false);
    return value;
}

FHConstructor {
    pthread_key_create(&resolve_key, NULL);
    FHHook(connect);
    FHHook(gethostbyname);
    FHHook(gethostbyaddr);
    FHHook(getaddrinfo);
    FHHook(getaddrinfo_async_start);
    FHHook(getnameinfo);
    FHHook(getnameinfo_async_start);
    FHHook(_ZL29_JSDnsResolveFunctionCallbackPK15OpaqueJSContextP13OpaqueJSValueS3_mPKPKS2_PS5_);
}
