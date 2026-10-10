# Security policy

## Supported versions

ztrip is in beta. Only the latest release (and `main`) gets security fixes.

## Reporting a vulnerability

Please report security problems privately, not in a public issue:

1. Go to the repository's **Security** tab and choose **Report a vulnerability**
   (GitHub private vulnerability reporting).
2. Describe the problem, how to reproduce it (a sample file helps), and the
   version or commit you tested.

We aim to reply within 7 days. Once a fix is out, the advisory is
published and you are credited unless you ask not to be.

## Scope

Useful reports include crashes or memory errors from crafted input files
(WAV, `.mgraph.json`, `.nseq.json`, `.tempo.json`, `.sigcache`), and, once live
input ships, problems in how ztrip parses network packets (OSC, Art-Net, sACN)
or MIDI. ztrip has no servers, accounts or telemetry, so there is no hosted
service in scope.
