var __skia_dnsCache = {};

function primaryIpAddress() {
  var addresses = __skia_primaryAddresses();
  if (addresses && addresses.length) {
    for (var i = 0; i < addresses.length; i++) {
      var address = addresses[i];
      if (address.split('.').length == 4) {
        return address;
      }
    }
  }
  return null;
}

function dnsResolve(host) {
  if (__skia_dnsCache.hasOwnProperty(host)) {
    return __skia_dnsCache[host];
  } else {
    var addresses = __skia_dnsResolve(host);
    if (addresses && addresses.length) {
      for (var i = 0; i < addresses.length; i++) {
        var address = addresses[i];
        if (address.split('.').length == 4) {
          __skia_dnsCache[host] = address;
          return address;
        }
      }
    }
    return null;
  }
}

function isPlainHostName(hostname) {
  return (hostname.indexOf('.') == -1) ? true : false;
}

function hostNameDomainLevel(hostname) {
  return hostname.split('.').length - 1;
}

function isHostNameInDomain(hostname, domain) {
  hostname = hostname.toLowerCase();
  domain = domain.toLowerCase();
  return (hostname.substring(hostname.length - domain.length, hostname.length) == domain) ? true : false;
}

function isHostResolvable(host) {
  var address = dnsResolve(host);
  return ((typeof address == "string") && address.length) ? true : false;
}

function isHostInNetwork(host, network, netmask) {
  var address = dnsResolve(host);
  if (address) {
    var addressParts = address.split('.');
    var networkParts = network.split('.');
    var netmaskParts = netmask.split('.');
    if ((addressParts.length == netmaskParts.length) && (networkParts.length == netmaskParts.length)) {
      for (var i = 0; i < netmaskParts.length; i++) {
        if ((addressParts[i] & netmaskParts[i]) != (networkParts[i] & netmaskParts[i])) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

function __skia_queryProxy(app, host, port) {
  __skia_dnsCache = {};
  var result = queryProxy(app, host, port);
  __skia_dnsCache = {};
  return result;
}
