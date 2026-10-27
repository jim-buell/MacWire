# Security

MacWire Helper is an unauthenticated HTTP service for a trusted local network.
Do not expose it to the Internet. Reachable clients can request content downloads;
feeds and downloaded pages can direct the Helper to other HTTP/HTTPS addresses,
including local services. There is no SSRF isolation. Download, pixel and worker
limits reduce resource use but do not make hostile inputs safe or impose a total
request deadline. Use a maintained Python installation and current dependencies.
Keep subscriber URLs, configurations, caches and logs private.

For a suspected vulnerability, use the repository's private
vulnerability reporting facility if it is enabled. Do not put credentials,
subscriber URLs or exploit details in a public issue. No response SLA is promised.
