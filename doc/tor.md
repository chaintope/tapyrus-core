# TOR SUPPORT IN BITCOIN

It is possible to run Bitcoin Core as a Tor hidden service, and connect to such services.

The following directions assume you have a Tor proxy running on port 9050. Many distributions default to having a SOCKS proxy listening on port 9050, but others may not. In particular, the Tor Browser Bundle defaults to listening on port 9150. See [Tor Project FAQ:TBBSocksPort](https://www.torproject.org/docs/faq.html.en#TBBSocksPort) for how to properly
configure Tor.


## 1. Run Bitcoin Core behind a Tor proxy

The first step is running Bitcoin Core behind a Tor proxy. This will already anonymize all
outgoing connections, but more is possible.

	-proxy=ip:port  Set the proxy server. If SOCKS5 is selected (default), this proxy
	                server will be used to try to reach .onion addresses as well.

	-onion=ip:port  Set the proxy server to use for Tor hidden services, including
	                v3 .onion names given to -connect or -addnode. You do not
	                need to set this if it's the same as -proxy. You can use -noonion
	                to explicitly disable access to hidden service.

	-listen         When using -proxy, listening is disabled by default. If you want
	                to run a hidden service (see next section), you'll need to enable
	                it explicitly.

	-connect=X      When behind a Tor proxy, you can specify .onion addresses instead
	-addnode=X      of IP addresses or hostnames in these parameters. It requires
	-seednode=X     SOCKS5. In Tor mode, such addresses can also be exchanged with
	                other P2P nodes.

In a typical situation, this suffices to run behind a Tor proxy:

	./bitcoind -proxy=127.0.0.1:9050


## 2. Run a Bitcoin Core hidden server

If you configure your Tor system accordingly, it is possible to make your node also
reachable from the Tor network. Add these lines to your /etc/tor/torrc (or equivalent
config file): *Needed for Tor version 0.2.7.0 and older versions of Tor only. For newer
versions of Tor see [Section 3](#3-automatically-listen-on-tor).*

	HiddenServiceDir /var/lib/tor/tapyrus-service/
	HiddenServicePort 2357 127.0.0.1:2358
	HiddenServicePort 12383 127.0.0.1:12384

The directory can be different of course. The first port on each line is the one peers
use to reach the onion service, normally the P2P port (2357 by default, 12383 in dev
mode). The second is the node's onion bind, where it listens for Tor inbound peers:
127.0.0.1 at the P2P port + 1 by default, or the address given with
`-bind=<addr>:<port>=onion`.

Peers arriving on the onion bind are never whitelisted by `-whitelist`, because their
address is the Tor daemon's. Don't point the service at the P2P port itself: the node
then can't tell Tor peers from clearnet ones. To prevent that, the node refuses to start
when a `-whitelist` range covers the `-proxy` or `-onion` address, or, with
`-listenonion`, 127.0.0.1, ::1 or the `-torcontrol` address.

The onion bind needs a port of its own. The node refuses to start when an onion bind
uses the port of a `-bind` or `-whitebind` on the same address or a wildcard address,
and when `-port=65535` leaves no P2P port + 1 for an onion bind given without a port.
It warns when every `-bind` is an onion bind, since clearnet peers can then reach the
node only through a `-whitebind`.

	-externalip=X   You can tell bitcoin about its publicly reachable address using
	                this option, and this can be a .onion address. Given the above
	                configuration, you can find your .onion address in
	                /var/lib/tor/bitcoin-service/hostname. For connections
	                coming from unroutable addresses (such as 127.0.0.1, where the
	                Tor proxy typically runs), .onion addresses are given
	                preference for your node to advertise itself with.

	-listen         You'll need to enable listening for incoming connections, as this
	                is off by default behind a proxy.

	-discover       When -externalip is specified, no attempt is made to discover local
	                IPv4 or IPv6 addresses. If you want to run a dual stack, reachable
	                from both Tor and IPv4 (or IPv6), you'll need to either pass your
	                other addresses using -externalip, or explicitly enable -discover.
	                Note that both addresses of a dual-stack system may be easily
	                linkable using traffic analysis.

In a typical situation, where you're only reachable via Tor, this should suffice:

	./bitcoind -proxy=127.0.0.1:9050 -externalip=57qr3yd1nyntf5k.onion -listen

(obviously, replace the .onion address with your own). It should be noted that you still
listen on all devices and another node could establish a clearnet connection, when knowing
your address. To mitigate this, additionally bind the address of your Tor proxy, and keep
the onion bind, since any `-bind` replaces the default one:

	./bitcoind ... -bind=127.0.0.1 -bind=127.0.0.1:2358=onion

If you don't care too much about hiding your node, and want to be reachable on IPv4
as well, use `discover` instead:

	./bitcoind ... -discover

and open port 8333 on your firewall (or use -upnp).

If you only want to use Tor to reach .onion addresses, but not use it as a proxy
for normal IPv4/IPv6 communication, use:

	./bitcoind -onion=127.0.0.1:9050 -externalip=57qr3yd1nyntf5k.onion -discover

## 3. Automatically listen on Tor

Starting with Tor version 0.2.7.1 it is possible, through Tor's control socket
API, to create and destroy 'ephemeral' hidden services programmatically.
Bitcoin Core has been updated to make use of this.

This means that if Tor is running (and proper authentication has been configured),
Bitcoin Core automatically creates a hidden service to listen on. This will positively 
affect the number of available .onion nodes.

This new feature is enabled by default if Bitcoin Core is listening (`-listen`), and
requires a Tor connection to work. It can be explicitly disabled with `-listenonion=0`
and, if not disabled, configured using the `-torcontrol` and `-torpassword` settings.
To show verbose debugging information, pass `-debug=tor`.

The node asks Tor for a v3 onion service that forwards to its onion bind, and keeps the
service key in `onion_v3_private_key` in the data directory. A v3 onion address doesn't
fit the addr message, so the node writes it to the log but doesn't advertise it to
peers. Share it by hand, for example with `-addnode` on other nodes.

If the node can't bind the address the onion service forwards to, for example because
another program uses the port, it refuses to start rather than let Tor forward its
peers to someone else's port. When `-bind` is given without an onion bind, the service
forwards to the first `-bind`, where Tor peers arrive as clearnet peers; the node warns
about this, and adding `-bind=<addr>=onion` fixes it.

Connecting to Tor's control socket API requires one of two authentication methods to be
configured. For cookie authentication the user running bitcoind must have write access
to the `CookieAuthFile` specified in Tor configuration. In some cases, this is
preconfigured and the creation of a hidden service is automatic. If permission problems
are seen with `-debug=tor` they can be resolved by adding both the user running Tor and
the user running bitcoind to the same group and setting permissions appropriately. On
Debian-based systems the user running bitcoind can be added to the debian-tor group,
which has the appropriate permissions. An alternative authentication method is the use
of the `-torpassword` flag and a `hash-password` which can be enabled and specified in
Tor configuration.

## 4. Privacy recommendations

- Do not add anything but Bitcoin Core ports to the hidden service created in section 2.
  If you run a web service too, create a new hidden service for that.
  Otherwise it is trivial to link them, which may reduce privacy. Hidden
  services created automatically (as in section 3) always have only one port
  open.
