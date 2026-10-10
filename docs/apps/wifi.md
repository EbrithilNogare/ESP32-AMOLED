# Wi-Fi (settings)

## Purpose

Join a Wi-Fi network from the device itself: pick it from a scan and type
the password on a Nokia-style keypad. Opened from the
[quick settings](quick-settings.md), not from the menu. The network is saved
on the device once it connects.

## Screens

```
 Networks                     Password                      Connecting / result
┌──────────────────────────┐ ┌──────────────────────────┐ ┌──────────────────────────┐
│ WI-FI          Scanning… │ │ PASSWORD              abc│ │ WI-FI                    │
│ ┌──────────────────────┐ │ │ Neighbour 5G             │ │                          │
│ │⌔ HomeNet           🔒│ │ │ ┌──────────────────────┐ │ │         ( ✓ )            │
│ │  Connected           │ │ │ │ pas_r|               │ │ │                          │
│ └──────────────────────┘ │ │ └──────────────────────┘ │ │       Connected          │
│ ┌──────────────────────┐ │ │ [1 .,-_][2 abc][3 def]   │ │ Neighbour 5G · 192.…     │
│ │⌔ Neighbour 5G      🔒│ │ │ [4 ghi] [5 jkl][6 mno]   │ │ Saved on this device     │
│ └──────────────────────┘ │ │ [7 pqrs][8 tuv][9 wxyz]  │ │                          │
│ ┌──────────────────────┐ │ │ [* #$%] [0 spc][# ABC]   │ │                          │
│ │⌔ Cafe Guest          │ │ │                          │ │                          │
│ └──────────────────────┘ │ │ [ ⌫ ]   [   Connect   ]  │ │        [ Done ]          │
│      [ ⟳ Scan again ]    │ └──────────────────────────┘ └──────────────────────────┘
└──────────────────────────┘
```

Simulator frames: `docs/design/screens/29-wifi-networks.png`,
`30-wifi-password.png`, `31-wifi-connected.png`.

- **Networks:** unique names, strongest first, signal bars, a lock for
  secured networks; the network in use is blue ("Connected" or "Saved").
- **Password:** the network's name, the typed text (the letter still
  cycling in blue, underlined, then the cursor), the mode (`abc` / `ABC` /
  `123`) top right. Long passwords scroll so the end stays visible.
- **Result:** "Connected" with the IP, or "Couldn't connect" with a hint.

## Typing (Nokia multi-tap)

| Key | Letters | |
| --- | --- | --- |
| 1 | `. , - _ @ ! ? 1` | punctuation |
| 2 … 9 | `abc2`, `def3`, `ghi4`, `jkl5`, `mno6`, `pqrs7`, `tuv8`, `wxyz9` | |
| 0 | space, `0` | |
| * | `` #$%&*+=/\:;'"()[]{}<>^~`\| `` | symbols |
| # | | mode: abc → ABC → 123 |

Tap a key again within 0.9 s for its next character; a pause or another key
commits it. In `123` every key types its digit at once. ⌫ deletes the last
character. Logic: `lib/MultiTap` (host-tested).

## Interaction

| Input | Effect |
| --- | --- |
| Tap a network | secured: password screen; open: connect right away |
| Scan again | new scan |
| Connect | needs 8+ characters (WPA2), else a hint; then the connecting screen |
| Back (BOOT) | password → networks; failed → password (text kept); connecting → networks (the attempt carries on); connected / networks → menu |
| Done / Try again | menu / password |

## Behaviour

- The screen holds the radio while open (`core::net::acquire`).
- A scan pauses a connection attempt in progress (the driver can't do both)
  and restarts it afterwards; it runs alongside an established connection.
- **Saved only if it works:** `core::net::join()` tries the new network; once
  it connects, the name and password are written to NVS (`wifi`). If it
  doesn't connect within `WIFI_JOIN_TIMEOUT_MS` (15 s), or the radio goes
  off first, the previously saved network is used again. A typo never
  replaces a working network.
- A saved network takes precedence over `WIFI_SSID` in `secrets.h`;
  `secrets.h` is only the fallback for a fresh device.

## Security

The password is stored in plain text in NVS (flash encryption is off), like
the credentials in `secrets.h` are in the firmware image. Anyone with the
board and a USB cable can read both.

## Config

| Macro | Default | Meaning |
| --- | --- | --- |
| `WIFI_JOIN_TIMEOUT_MS` | 15000 | how long a newly picked network gets to connect |

## Acceptance (on device)

- [ ] The scan lists nearby 2.4 GHz networks within ~3 s
- [ ] Multi-tap feels right: cycling, commit after the pause, ABC / 123
- [ ] Joining with the right password saves it: still connects after a
      reboot, a USB flash and a wireless update
- [ ] A wrong password ends in "Couldn't connect" within ~15 s and the old
      network still works
- [ ] An open network connects without the keypad
