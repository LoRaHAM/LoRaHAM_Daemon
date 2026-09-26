# Contributing

## Branches

| | |
|---|---|
| `dev` | Integration. Everything lands here first, and CI runs on every push. |
| `main` | What downstream pins — [LoRaHAM_Pi Control](https://github.com/makrohard/loraham-pi-control) pins a commit on `main` and builds the daemon from it. |

`main` only ever fast-forwards from `dev`, when the maintainer says so and after downstream has been
told, so that a pin can be moved in the same window. **`main` is never force-pushed**: a rewrite
there makes every existing pin dangle. `dev` may be rebased freely.

The repository's default branch stays `main`. Downstream selectors that are not pinned resolve to
default-branch `HEAD`, so switching it would put unreleased work on real boxes.

Work larger than a commit or two gets a topic branch off `dev`.

## Building and testing

    cd loraham_daemon
    ./build.sh            # release build
    ./build.sh --strict   # -Werror
    ./run_tests.sh        # full suite

Prerequisites and the pinned RadioLib checkout: [`docs/hardware.md`](docs/hardware.md).

CI runs four jobs on every push — `normal`, `strict`, `asan-ubsan`, `tsan` — and all four must be
green before anything reaches `main`.

Two things about running the suite off a Pi:

- Tests that drive a live daemon check for `/dev/spidev0.*` first and report `SKIP` where there is
  none. That is a check for the precondition, not for a daemon that failed: on a machine with an
  SPI device they run for real, and a daemon that does not come up is still a `FAIL`.
- Clear leftover sockets before a local run, or the socket-lifecycle tests report files that a
  previous run left behind:

      rm -f /tmp/lora*.sock /tmp/loraconf*.sock

## Commits

One change per commit. Write the subject in the imperative and use the body to say **why**, not to
restate the diff. Commits carry no AI-assistant attribution trailers.

When a change moves files, keep the move a pure rename in its own commit so `git log --find-renames`
can show it as one; content changes belong in a separate commit.

## Documentation

[`docs/`](docs/) is the reference: [architecture](docs/architecture.md),
[hardware](docs/hardware.md), [deployment](docs/deployment.md), [CLI](docs/cli.md),
[CONF protocol](docs/conf-protocol.md), [DATA protocol](docs/data-protocol.md) and
[limits](docs/limits.md), and [examples](docs/examples.md). [`loraham_daemon/README.md`](loraham_daemon/README.md) is deliberately
short — what the daemon is, and the path from clone to running daemon.
[`clients/README.md`](clients/README.md) covers the client programs and
[`archive/README.md`](archive/README.md) the single-file daemons.

Each fact has one home. If a page needs something another page owns, link to it rather than
restating it — the root README carried both languages and a copy of half the protocol
documentation, and the copies drifted.

The one deliberate exception is [`archive/README.md`](archive/README.md). Someone building an
archived daemon should not have to assemble the instructions from four pages, so that one repeats
the prerequisites and the SPI setup on purpose.

## Reporting a defect

Say which tree it is — [`loraham_daemon/`](loraham_daemon/) or [`archive/`](archive/) — and include:

- the output of `./loraham_daemon --version`
- the `--hw` preset, the band, and which board is fitted
- how it was started: systemd unit, or by hand with `LORAHAM_SOCKET_DIR`
- the daemon's own output — it logs to stderr and says a great deal
- the exact CONF or DATA exchange, if a socket is involved
