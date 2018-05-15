#include "skia.hpp"

skia &skia::instance() {
    static skia instance;
    return instance;
}

std::string skia::current_application() {
    CFStringRef bundle_id = CFBundleGetIdentifier(CFBundleGetMainBundle());
    return bundle_id ? CFStringGetCStringPtr(bundle_id, CFStringGetSystemEncoding()) : getprogname();
}

socket_address skia::query_proxy(const std::string &target_name, const uint16_t &target_port, bool &no_cache) {
    socket_address proxy;
    bool no_cache_flag = false;
    proxy_config.execute([&](JSGlobalContextRef context) {
        JSStringRef query_function_name = JSStringCreateWithUTF8CString("__skia_queryProxy");
        JSObjectRef query_function = JSValueToObject(context, JSObjectGetProperty(context, JSContextGetGlobalObject(context), query_function_name, NULL), NULL);
        JSStringRelease(query_function_name);
        if (query_function == NULL) {
            return;
        }
        JSStringRef application_string = JSStringCreateWithUTF8CString(current_application().c_str());
        JSStringRef target_name_string = JSStringCreateWithUTF8CString(target_name.c_str());
        JSValueRef arguments[] = {
            JSValueMakeString(context, application_string),
            JSValueMakeString(context, target_name_string),
            JSValueMakeNumber(context, target_port),
        };
        JSStringRelease(application_string);
        JSStringRelease(target_name_string);
        JSObjectRef result_object = JSValueToObject(context, JSObjectCallAsFunction(context, query_function, NULL, sizeof(arguments) / sizeof(arguments[0]), arguments, NULL), NULL);
        if (result_object == NULL) {
            return;
        }
        JSStringRef host_property = JSStringCreateWithUTF8CString("host");
        JSStringRef port_property = JSStringCreateWithUTF8CString("port");
        JSStringRef no_cache_property = JSStringCreateWithUTF8CString("noCache");
        JSStringRef host_string = JSValueToStringCopy(context, JSObjectGetProperty(context, result_object, host_property, NULL), NULL);
        char host_buffer[INET6_ADDRSTRLEN];
        JSStringGetUTF8CString(host_string, host_buffer, sizeof(host_buffer));
        JSStringRelease(host_string);
        uint16_t port_number = JSValueToNumber(context, JSObjectGetProperty(context, result_object, port_property, NULL), NULL);
        no_cache_flag = JSValueToBoolean(context, JSObjectGetProperty(context, result_object, no_cache_property, NULL));
        JSStringRelease(host_property);
        JSStringRelease(port_property);
        JSStringRelease(no_cache_property);
        if (inet_aton(host_buffer, reinterpret_cast<struct in_addr *>(&proxy.addr)) == 1) {
            proxy.port = htons(port_number);
        } else {
            proxy.addr = 0;
            proxy.port = 0;
        }
    });
    no_cache = no_cache_flag;
    return proxy;
}

bool skia::should_bypass(const int &sock) {
    if (sock < 0) {
        return true;
    }
    int type;
    socklen_t type_len = sizeof(type);
    if (getsockopt(sock, SOL_SOCKET, SO_TYPE, &type, &type_len) != 0 || type != SOCK_STREAM) {
        return true;
    }
    return false;
}

bool skia::should_bypass(const struct sockaddr *addr) {
    if (addr == NULL) {
        return true;
    }
    sa_family_t family = addr->sa_family;
    if (family != AF_INET && family != AF_INET6) {
        return true;
    }
    return false;
}

bool skia::should_bypass(const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6) {
    if (ipv6) {
    } else {
        in_addr_t target_addr_v4 = target_addr.__u6_addr.__u6_addr32[0];
        for (const socket_network &bypass_network : bypass_networks) {
            if ((target_addr_v4 & bypass_network.mask) == (bypass_network.addr & bypass_network.mask) && (bypass_network.port == 0 || target_port == bypass_network.port)) {
                return true;
            }
        }
    }
    return false;
}

bool skia::should_bypass(const std::string &target_name, const std::string &target_serv) {
    if (target_name.length() == 0) {
        return true;
    }
    struct in_addr addr_buffer;
    if (inet_aton(target_name.c_str(), &addr_buffer) == 1) {
        return true;
    }
    char localhost_buffer[] = "localhost", localhost_name_buffer[256];
    gethostname(localhost_name_buffer, sizeof(localhost_name_buffer));
    if (target_name == localhost_buffer || target_name == localhost_name_buffer) {
        return true;
    }
    return false;
}

void skia::extract_target(const struct sockaddr *addr, struct in6_addr &target_addr, in_port_t &target_port, bool &ipv6) {
    ipv6 = addr->sa_family == AF_INET6;
    if (ipv6) {
        const struct sockaddr_in6 *address_in = reinterpret_cast<const struct sockaddr_in6 *>(addr);
        const struct in6_addr *in_addr = &address_in->sin6_addr;
        if (in_addr->__u6_addr.__u6_addr32[0] == 0x0 && in_addr->__u6_addr.__u6_addr32[1] == 0x0 && in_addr->__u6_addr.__u6_addr16[4] == 0x0 && in_addr->__u6_addr.__u6_addr16[5] == 0xffff) {
            memcpy(&target_addr, in_addr->__u6_addr.__u6_addr32 + 3, sizeof(struct in_addr));
            ipv6 = false;
        } else {
            memcpy(&target_addr, in_addr, sizeof(struct in6_addr));
        }
        target_port = address_in->sin6_port;
    } else {
        const struct sockaddr_in *address_in = reinterpret_cast<const struct sockaddr_in *>(addr);
        const struct in_addr *in_addr = &address_in->sin_addr;
        memcpy(&target_addr, in_addr, sizeof(struct in_addr));
        target_port = address_in->sin_port;
    }
}

socket_address skia::query_proxy(const std::string &target_name, const uint16_t &target_port) {
    socket_address proxy;
    if (target_name.length() == 0 || target_port == 0) {
        return proxy;
    }
    std::string key = target_name + ":" + std::to_string(target_port);
    mutex.lock_shared();
    auto entry = proxy_cache.find(key);
    if (entry != proxy_cache.end()) {
        proxy = entry->second;
        mutex.unlock_shared();
    } else {
        mutex.unlock_shared();
        bool no_cache = false;
        proxy = query_proxy(target_name, target_port, no_cache);
        if (!no_cache) {
            mutex.lock();
            proxy_cache[key] = proxy;
            mutex.unlock();
        }
    }
    return proxy;
}

socket_address skia::query_proxy(const struct in6_addr &target_addr, const in_port_t &target_port, bool ipv6) {
    std::string target_name;
    if (!ipv6 && resolve_table::instance().is_resolved_addr(target_addr.__u6_addr.__u6_addr32[0])) {
        target_name = resolve_table::instance().addr_to_name(target_addr.__u6_addr.__u6_addr32[0]);
    } else {
        char target_name_buffer[INET6_ADDRSTRLEN];
        target_name = inet_ntop(ipv6 ? AF_INET6 : AF_INET, &target_addr, target_name_buffer, sizeof(target_name_buffer));
    }
    return query_proxy(target_name, ntohs(target_port));
}

resolve_table &resolve_table::instance() {
    static resolve_table instance;
    return instance;
}

size_t resolve_table::make_index(const std::string &name) {
    size_t result = 0;
    mutex.lock();
    if (name_table.size() >= addr_count) {
        for (auto entry = name_table.begin(); entry != name_table.end(); entry++) {
            if (entry->second == index) {
                name_table.erase(entry);
                break;
            }
        }
    }
    name_table[name] = index;
    index_table[index] = name;
    result = index;
    index = (index + 1) % addr_count;
    mutex.unlock();
    return result;
}

size_t resolve_table::name_to_index(const std::string &name) {
    size_t index = 0;
    mutex.lock_shared();
    auto entry = name_table.find(name);
    if (entry != name_table.end()) {
        index = entry->second;
        mutex.unlock_shared();
    } else {
        mutex.unlock_shared();
        index = make_index(name);
    }
    return index;
}

std::string resolve_table::index_to_name(const size_t &index) {
    std::string name;
    mutex.lock_shared();
    auto entry = index_table.find(index);
    if (entry != index_table.end()) {
        name = entry->second;
    }
    mutex.unlock_shared();
    return name;
}

in_addr_t resolve_table::index_to_addr(const size_t &index) {
    return htonl((static_cast<in_addr_t>(addr_prefix) << bits_count) | static_cast<in_addr_t>(index + 1));
}

size_t resolve_table::addr_to_index(const in_addr_t &addr) {
    return addr_count & (ntohl(addr) - 1);
}

in_addr_t resolve_table::name_to_addr(const std::string &name) {
    return index_to_addr(name_to_index(name));
}

std::string resolve_table::addr_to_name(const in_addr_t &addr) {
    return index_to_name(addr_to_index(addr));
}

bool resolve_table::is_resolved_addr(const in_addr_t &addr) {
    return reinterpret_cast<const uint8_t *>(&addr)[0] == addr_prefix;
}
