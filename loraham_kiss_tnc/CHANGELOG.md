# Changelog

- Harden numeric CLI/config parsing to reject non-finite values and documented LoRa bounds.
- Add KISS decoder/encoder boundary regression tests.
- Add AX.25/TNC2 boundary regression tests.
- Harden TX failure handling with RX restore retry and stats.
- Reconnect LoRaHAM DATA socket after TX write failure.
- Centralize LoRaHAM DATA socket disconnect handling.
- Use explicit KISS client socket error handling.
- Add graceful SIGINT/SIGTERM shutdown handling.
