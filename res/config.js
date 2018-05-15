/* Skia Configuration Script */

/*
 * Predefined Functions
 *
 * primaryIpAddress(): string
 * dnsResolve(host: string): string
 * isPlainHostName(hostname: string): boolean
 * hostNameDomainLevel(hostname: string): number
 * isHostNameInDomain(hostname: string, domain: string): boolean
 * isHostResolvable(host: string): boolean
 * isHostInNetwork(host: string, network: string, netmask: string): boolean
 *
 */

/*
 * The config script must define this function, which will be called
 * by Skia for every network connection that is made by applications.
 *
 * queryProxy(app: string, host: string, port: number): {host: string, port: number, noCache: boolean}
 * @app - bundle identifier of the application that made the connection.
 *        if it is not available then the value will be the process name.
 * @host - destination host name or ip address.
 * @port - destination port number.
 * @returns - result object for the query. return null to bypass proxy.
 * @returns.host - ip address of the designated proxy server.
 *                 use null value to bypass proxy.
 * @returns.port - port number of the designated proxy server.
 *                 it must be a positive integer.
 * @returns.noCache - whether cache the result or not. the cache will
 *                    persist until the application is terminated.
 *                    the default value is false.
 *
 */

function queryProxy(app, host, port) {
  // These destinations are always connected directly and you do not need to check them here.
  // 127.0.0.1/8, 10.0.0.0/255.0.0.0, 172.16.0.0/255.240.0.0, 192.168.0.0/255.255.0.0 and localhost.

  // You can have as many proxies as you wish.
  var proxies = [
    {host: '127.0.0.1', port: 2000},
    {host: '127.0.0.1', port: 2001}
  ];

  // This connection should not leave your local network.
  if (isPlainHostName(host)) {
    return null;
  }

  // Bypass some internal networks.
  var internalNetworks = [
    ['222.205.0.0', '255.255.0.0'],
    ['210.32.0.0',  '255.255.0.0']
  ];
  for (var i = 0; i < internalNetworks.length; i++) {
    if (isHostInNetwork(host, internalNetworks[i][0], internalNetworks[i][1])) {
      return null;
    }
  }

  // Bypass some internal domains.
  var internalDomains = [
    'zju.edu.cn',
    'cc98.org',
    'nexushd.org'
  ];
  for (i = 0; i < internalDomains.length; i++) {
    if (isHostNameInDomain(host, internalDomains[i])) {
      return null;
    }
  }

  // Some apps are fucked by GFW.
  var blockedApps = [
    'com.facebook.Paper',
    'com.facebook.Facebook',
    'com.atebits.Tweetie2'
  ];
  for (i = 0; i < blockedApps.length; i++) {
    if (app == blockedApps[i]) {
      return proxies[0];
    }
  }

  // Or if you would like to use a different proxy on some occasions.
  if (app == 'com.google.ingress') {
    return proxies[1];
  }

  // Now it is time for a long long list.
  // Here we do not iterate over an array for better performance.
  var blockedDomains = {
    'google.com': true,
    'gmail.com': true,
    'gstatic.com': true,
    'googleusercontent.com': true,
    'googleapis.com': true,
    'facebook.com': true,
    'fbcdn.net': true,
    'twitter.com': true,
    'twimg.com': true,
    't.co': true
  };
  var dotPosition = 0;
  var hostSubstring = host;
  while (dotPosition >= 0) {
    if (blockedDomains[hostSubstring]) {
      // Random proxy, well, if you like.
      return proxies[Math.floor(Math.random() * proxies.length)];
    }
    dotPosition = host.indexOf('.', dotPosition + 1);
    hostSubstring = host.slice(dotPosition + 1);
  }

  // Finally, we may use direct connection as default.
  return null;
}
