# Deployment

How the daemon is installed and run: the systemd template unit, the system user and group, the
socket and lock files it puts on disk, and two band processes side by side. Boards, wiring and the
`--hw` presets are in [Hardware](hardware.md); the flags themselves are in [CLI](cli.md).

## Contents

- [Users and groups](#users-and-groups)
- [Installing](#installing)
- [Sockets on disk](#sockets-on-disk)
- [Lock files](#lock-files)
- [Lock order](#lock-order)
- [Exit codes and restart behaviour](#exit-codes-and-restart-behaviour)
- [Multi-instance operation](#multi-instance-operation)
- [Readiness](#readiness)
- [Running without systemd](#running-without-systemd)
- [Acceptance checks](#acceptance-checks)

## Users and groups

The daemon runs unprivileged as the dedicated system user `loraham`. The shipped unit sets
`User=loraham`, `Group=loraham`, `SupplementaryGroups=spi gpio` and `UMask=0007`: hardware access
comes from the Raspberry Pi OS `spi` and `gpio` device groups, not from root. Root is not required
for GPIO or SPI access.

Create the group and the user once, and add every client user to the group:

```bash
getent group loraham >/dev/null || sudo groupadd --system loraham
getent passwd loraham >/dev/null || \
  sudo useradd --system -g loraham -G spi,gpio -M -s /usr/sbin/nologin loraham
sudo usermod -aG loraham <user>
```

Both directories that `tmpfiles.d` provisions are owned by that user, so the user must exist
**before** `systemd-tmpfiles --create` runs.

## Installing

Two deployable artefacts live under `../loraham_daemon/systemd/`, with no developer-specific paths:

| File | Purpose |
|---|---|
| `systemd/loraham-daemon@.service` | per-band template unit |
| `systemd/tmpfiles.d/loraham.conf` | provisions the lock directory and the socket directory |

The tmpfiles fragment contains exactly two lines:

```
d      /run/lock/loraham   0755 loraham loraham -
d      /run/loraham        2750 loraham loraham -
```

Install, after creating the user and group:

```bash
sudo install -D -m0755 loraham_daemon /usr/local/bin/loraham_daemon
sudo install -D -m0644 systemd/tmpfiles.d/loraham.conf /etc/tmpfiles.d/loraham.conf
sudo systemd-tmpfiles --create /etc/tmpfiles.d/loraham.conf
sudo install -D -m0644 systemd/loraham-daemon@.service \
  /etc/systemd/system/loraham-daemon@.service
sudo systemctl daemon-reload
sudo systemctl enable --now loraham-daemon@433.service loraham-daemon@868.service
```

The unit is `Type=simple` and runs `ExecStart=/usr/local/bin/loraham_daemon --radio %i`. It sources
no `EnvironmentFile` and sets `UnsetEnvironment=LORAHAM_RUNTIME_DIR LORAHAM_SOCKET_DIR`, so nothing
inherited from the manager can redirect the shared lock namespace or the socket directory.

Do not edit the shipped unit. To run the binary from another path, or to give one band a hardware
profile, use a drop-in — `sudo systemctl edit loraham-daemon@433.service` — with an empty
`ExecStart=` followed by the real line:

```ini
[Service]
ExecStart=
ExecStart=/usr/local/bin/loraham_daemon --radio %i --hw uputronics-ce0
```

Use `uputronics-ce1` in the drop-in for the other band.

## Sockets on disk

All public sockets live in `/run/loraham`, owned `loraham:loraham` with mode `2750` (setgid, no
group write). Group members traverse the directory and connect; only the directory owner — the
unprivileged daemon user — can create files in it, so no local user can plant a non-socket
collision at a socket pathname.

| Constant | Path | Carries |
|---|---|---|
| `DATA433_SOCKET` | `/run/loraham/lora433.sock` | raw DATA |
| `DATA868_SOCKET` | `/run/loraham/lora868.sock` | raw DATA |
| `DATA433_FRAMED_SOCKET` | `/run/loraham/lora433f.sock` | framed DATA |
| `DATA868_FRAMED_SOCKET` | `/run/loraham/lora868f.sock` | framed DATA |
| `CONF433_SOCKET` | `/run/loraham/loraconf433.sock` | CONF commands |
| `CONF868_SOCKET` | `/run/loraham/loraconf868.sock` | CONF commands |

The wire formats are documented in [DATA protocol](data-protocol.md) and
[CONF protocol](conf-protocol.md).

`--radio 433` creates exactly `lora433.sock`, `lora433f.sock` and `loraconf433.sock`; `--radio 868`
creates the corresponding 868 trio. The inactive band's sockets are never created, so a client can
detect the active backend by socket path.

Each listener is `AF_UNIX` / `SOCK_STREAM` with a listen backlog of `MAX_CLIENTS`. After `bind()`
the socket file is `chmod` `0660` and the descriptor is set non-blocking, both before `listen()`.
The `0660` mode with the setgid directory is what makes `loraham`-group client access work.

Setup replaces a stale socket file at any of these paths by unlinking it. An existing filesystem
entry that is not a socket is rejected instead, with `errno` `EEXIST` and

```
ERROR: socket path exists but is not a socket: <path>
```

`LORAHAM_SOCKET_DIR` overrides the socket directory for non-root dev and test runs only; the
systemd unit clears it. The lock namespace `/run/lock/loraham` is separate and is not affected by
that override. How the client programs locate the sockets is described in
[`../clients/README.md`](../clients/README.md).

## Lock files

Hardware lock files live under `/run/lock/loraham`, in three classes:

| File | Scope | Held |
|---|---|---|
| `instance-433.lock`, `instance-868.lock` | per-band ownership | whole process lifetime |
| `gpio<N>.lock` | one per claimed BCM pin | whole process lifetime |
| `spi0.lock` | the shared SPI bus | one SPI transaction |

All lock files are owner-only `0600`, and a legacy `0660` file is corrected with `fchmod()` on
open. A `loraham` client user therefore can never hold a hardware lock, block startup, or force a
runtime SPI timeout: socket access and hardware ownership stay separated even though clients and
daemon share the group.

The directory is provisioned `0755 loraham loraham` by `tmpfiles.d`. It must **not** be a per-unit
systemd `RuntimeDirectory`: systemd removes such a directory when that unit stops, which would
unlink the shared lock inode out from under the still-running peer and silently break cross-process
SPI serialisation. The unit declares no `RuntimeDirectory`.

Before any lock file is opened the directory is validated: opened `O_RDONLY | O_DIRECTORY |
O_NOFOLLOW` so a symlink is rejected, required to be a real directory that is not group- or
world-writable, and required to be owned by root **or** by the daemon's own effective uid. The
shipped deployment satisfies the second case: the owner is `loraham`, not root. The daemon never
silently creates the production directory — it must be pre-provisioned. Lock files are then created
with `openat()` relative to the validated directory fd, with `O_NOFOLLOW`, and must be regular
files.

Failure is closed. If the directory or a lock file cannot be validated or established, the radio
does not start: `begin()` is skipped and the daemon exits with code `4`
(`LORAHAM_EXIT_LOCK_ERROR`). There is no `/tmp` fallback and no unlocked transfer.

`LORAHAM_RUNTIME_DIR` redirects the lock directory for dev and test runs only. In override mode the
directory is created `0700` if missing and the trusted-owner requirement is relaxed, while the
symlink and group/world-writable checks still apply.

The advisory-lock helpers in `loraham_runtime.h` differ in what they retry.
`loraham_flock_acquire_ex()` and `loraham_flock_release()` retry on `EINTR` only.
`loraham_flock_acquire_ex_deadline()` — the helper the SPI lock uses — also retries on
`EWOULDBLOCK`/`EAGAIN`, polling `flock(LOCK_EX | LOCK_NB)` at 1 ms intervals against a
`CLOCK_MONOTONIC` deadline and returning `-1` with `errno` `ETIMEDOUT` on expiry. The SPI deadline
is `LORAHAM_SPI_LOCK_TIMEOUT_MS`, 2000 ms.

`locking_pihal.h` defines `LockingPiHal`, a RadioLib `PiHal` subclass that takes that lock in
`spiBeginTransaction()` and releases it in `spiEndTransaction()`, serialising one complete SPI
transaction across processes and bands. If the lock cannot be established, `spi_lock_ready()` stays
false and the radio is not started.

## Lock order

Locks are taken in one order, and only that order:

1. the per-band instance lock, before sockets, GPIO, SPI or radio setup;
2. one `gpio<N>.lock` per claimed BCM pin, in ascending pin order, before any GPIO access;
3. `spi0.lock`, inside each SPI transaction and never during instance setup.

The instance lock is acquired with `flock(LOCK_EX | LOCK_NB)` on a descriptor held for the whole
process lifetime, by `daemon_instance_lock.cpp`. It is released only after every socket has been
closed and unlinked, which closes the shutdown/restart race: a same-band restart cannot bind
sockets that the outgoing instance then deletes. Nothing unlinks the lock file, and the kernel drops
the advisory lock when the owning process dies, so a crash leaves nothing to clean up.

The status-LED claim runs only after all pin locks are held. It is an activity indicator and a
secondary hardware-exclusivity check, not the ownership barrier, and it is released during radio
shutdown, before the sockets.

## Exit codes and restart behaviour

The unit sets `Restart=on-failure`, `RestartSec=2` and `RestartPreventExitStatus=3 4`.

| Code | Name | Meaning | Restart |
|---:|---|---|---|
| `1` | — | genuine radio hardware failure (chip not found, transient init error) | yes |
| `3` | `LORAHAM_EXIT_INSTANCE_BUSY` | same-band instance already running | no |
| `4` | `LORAHAM_EXIT_LOCK_ERROR` | startup prerequisite unavailable — lock infrastructure **or** GPIO — fail closed | no |
| `5` | `LORAHAM_EXIT_RUNTIME_RADIO_IO_ERROR` | runtime radio-I/O fatal after operation began: SPI, the bus lock, or a GPIO call once the radio was live | yes |

Code `4` covers both startup prerequisites, because neither is fixed by trying again: an unusable
lock directory or lock file, and a GPIO open or claim that fails during startup — `gpiochip0` that
cannot be opened (wrong device, missing node, no permission) or a line held by a process outside
this daemon's own pin locks. A wrongly wired or unpermitted box therefore stops with a diagnosis
instead of restarting every two seconds forever.

Codes `3` and `4` are not fixed by restarting, so systemd does not restart-spin on them. Code `5`
is deliberately distinct from `4`: a hard, non-`EINTR` `flock` failure on lock or unlock, a bus
error, a wedged-peer timeout, any transfer attempt without the lock held, **or a GPIO call that
fails once the radio is operational**, is a controlled fatal that exits `5`, and a restart can
legitimately clear it. The log line is tagged `[RADIO] FATAL` — it was `[SPI] FATAL`, which sent
operators looking at the wrong bus when the failure was a GPIO one.

A duplicate same-band start prints

```
[LOCK] Band 433 wird bereits von einer anderen Instanz betrieben – beende.
```

## Multi-instance operation

`--radio 433` or `--radio 868` is mandatory and one process drives one band. Two stacked Uputronics
boards therefore mean two daemon processes, one per band. They share the SPI bus through the
per-transaction `spi0.lock` and are separated by disjoint GPIO claims. Users of the removed
`--radio both` run the two template units `loraham-daemon@433` and `loraham-daemon@868` instead,
with per-unit drop-ins for anything that used to be a band-suffixed flag.

Pin conflicts are caught by the daemon itself, before any GPIO access including the status-LED
claim: each process takes one `gpio<N>.lock` per pin it will drive. A pin already held by another
process fails the boot closed with

```
[GPIO] Fehler: GPIO <N> bereits von einem anderen Prozess beansprucht
```

and exit `4`. Radio initialisation additionally refuses to start when no pin locks are held, so
there is no path that bypasses the conflict gate.

## Readiness

Because the unit is `Type=simple`, systemd reports the service active as soon as the process
starts, before the radio is confirmed ready — the sockets are bound before `lora_init()` runs. An
orchestrator should treat a band as healthy only once it can connect to that band's CONF socket,
`/run/loraham/loraconf433.sock` or `/run/loraham/loraconf868.sock`, and get a `GET STATUS`
response. `Type=notify` with `sd_notify` is a possible future enhancement and is intentionally not
used today.

## Running without systemd

Two directories have to be writable by the calling user: the socket directory, and the lock
directory. Under systemd both are provisioned by tmpfiles.d; started by hand, neither exists, and
the daemon fails closed with `[LOCK] Fehler: Sperrverzeichnis /run/lock/loraham nicht nutzbar`.

The lock directory must also not be group- or world-writable — a plain `mkdir` under the usual
`umask 002` gives `0775` and the daemon refuses it with `gruppen-/weltbeschreibbar (mode=0775)`.

```bash
mkdir -p ~/loraham-run && chmod 755 ~/loraham-run

LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run \
  ./loraham_daemon --radio 433 --hw loraham

LORAHAM_SOCKET_DIR=/tmp LORAHAM_RUNTIME_DIR=~/loraham-run \
  ./loraham_daemon --radio 868 --hw loraham
```

Both processes share one lock directory: that is how they serialise the SPI bus between them, so
do not give them one each. `--hw` names the board — see [hardware.md](hardware.md); it defaults to
`loraham` and is written out here because it is the flag most often wrong.

Without `-d` the process stays in the foreground and prints its traffic on the terminal. With `-d`
it double-forks and redirects stdout and stderr to `/tmp/lora_daemon.log`, opened `O_CREAT |
O_APPEND | O_NOFOLLOW` with mode `0640`. Under systemd, use neither: `Type=simple` expects the
foreground model, and the single shared log file is unnecessary behind a supervisor.

## Acceptance checks

Both services up, and the socket trio present:

```bash
systemctl is-active loraham-daemon@433.service loraham-daemon@868.service
test -S /run/loraham/loraconf433.sock && test -S /run/loraham/loraconf868.sock
printf 'GET STATUS\n' | socat - UNIX-CONNECT:/run/loraham/loraconf433.sock
```

The lock directory is trusted, and owned by the daemon user:

```bash
sudo stat -c '%U:%G %a %n' /run/lock/loraham
# expect: loraham:loraham 755 /run/lock/loraham
```

Root ownership would also pass the code's trust check, but it is not what the shipped tmpfiles
fragment creates.

The shared SPI lock file must stay the same file while one band restarts — the device:inode must
not change:

```bash
sudo stat -c '%d:%i %n' /run/lock/loraham/spi0.lock   # note device:inode
sudo systemctl restart loraham-daemon@433.service
sudo stat -c '%d:%i %n' /run/lock/loraham/spi0.lock   # must be the SAME device:inode
```

Independent lifecycle, and duplicate rejection:

```bash
sudo systemctl stop loraham-daemon@433.service    # 868 keeps running
systemctl is-active loraham-daemon@868.service    # active
sudo systemctl restart loraham-daemon@433.service
sudo -u loraham /usr/local/bin/loraham_daemon --radio 433; echo "exit=$?"   # expect exit=3
```

The duplicate must run as `loraham`. Run as root against the shipped `loraham`-owned
`/run/lock/loraham`, the trusted-directory check fails — the owner uid is neither `0` nor the
caller's effective uid — and the process exits `4` before it ever attempts `flock`, which does not
demonstrate anything about same-band ownership.
