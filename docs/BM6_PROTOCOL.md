# BM6 Protocol Notes

These notes distinguish behavior verified on this project's BM6 hardware from
features that still need protocol captures. Do not send guessed commands to a
monitor connected to a vehicle battery.

## Confirmed live stream

- BLE service: `FFF0`
- encrypted command writes: `FFF3`
- encrypted notifications: `FFF4`
- cipher: AES-128-CBC with a zero IV
- decrypted start-stream command: `d1550700000000000000000000000000`
- decrypted live packet prefix: `d15507`

One start-stream command keeps notifications running at approximately 1 Hz
while the BLE connection remains open. Reconnecting for every reading is not
required. The attached bench BM6 was measured at 0.93-1.03 seconds between
valid packets.

## Cranking packets

BM6 app diagnostic captures contain cranking waveform records beginning with
`d15503`. The remaining waveform is fragmented across many notifications and
encodes voltage samples at 100 Hz. Captures also show historical waveform
replay markers, so cranking records can be downloaded rather than only observed
live.

The current dash ignores non-`d15507` packets without logging them. A future
parser needs a bounded packet queue and `d15503` reassembly so the high-rate
waveform cannot block the UI or overwrite live packets.

## Onboard history

Current manufacturer material says the BM6 stores voltage, state of charge,
and temperature internally every two minutes for up to 72 days. Some manuals
for earlier revisions state 30 days. The phone app synchronizes these offline
records after reconnecting, which is the data needed when the dash display is
powered from ignition and misses engine cranking.

The public `JeffWDH/bm6-battery-monitor` implementation documents only the live
stream command. The request, paging, acknowledgement, and completion commands
for onboard history have not yet been identified. The next safe step is to
capture an Android Bluetooth HCI log while the official app performs a history
sync, then decrypt writes to `FFF3` and notifications from `FFF4` with the known
AES key.

## References

- https://github.com/JeffWDH/bm6-battery-monitor
- https://www.tarball.ca/posts/reverse-engineering-the-bm6-ble-battery-monitor/
- https://leagend.com/products/bm6
- https://1stbenz.github.io/en/2026/bm6-realtime-log-tool.html
- https://1stbenz.github.io/en/2026/bm6-cranking-log-tool.html
