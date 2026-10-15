# USB AP preparation fixture

`gridopoly-ap-deployed-10-0.sh` is the exact, non-secret AP launcher read from
`/usr/local/libexec/gridopoly-ap` on the Pi on 2026-10-15. It references credentials
through environment variables and contains no deployed password or MAC address.

SHA256: `0538095d81181708fd67f7bfb090205b0c0fd64915a1674e17b8359c09592930`.
It emits the deployed hardcoded `ap_max_inactivity=10`, `disassoc_low_ack=0`.
Keep LF bytes: production preflight intentionally checks the exact source hash.
CRLF conversion of the *installed* script is rejected, not normalized silently.

The fixture makes USB regression independent of the unrelated uncommitted AP
parameterization. It is test input, not an installation recommendation or a
replacement for the current source/deployment scripts.
