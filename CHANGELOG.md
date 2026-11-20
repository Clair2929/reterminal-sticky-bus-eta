# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres
to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-11-20

### Fixed

- **Singapore stop-name lookup ("Station ID lookup failed").** The stop directory
  `data.busrouter.sg` sits behind a CloudFront/ECDSA edge that intermittently drops
  the TLS handshake with mbedtls 3.6.2. The directory download now uses a
  multi-source fallback (jsDelivr → GitHub raw → `data.busrouter.sg`) served from
  RSA certificates, closes TLS 1.3 in favour of TLS 1.2, retries with backoff, and
  only accepts a download that reached the full `Content-Length`. The WiFi modem
  sleep mode is temporarily disabled during the download and restored afterwards.

### Changed

- **No preset stop name.** `BUS_STOP_ID` / `BUS_STOP_NAME` default to empty, so a
  fresh flash (or erased NVS) no longer shows a hard-coded stop. An empty Station ID
  skips the bogus `?id=` request and the Bus page shows `Set station in Bus Setup`
  until a stop is saved.
- **Refresh interval is now required** in Bus Setup (whole number, 1–60 minutes).
- **WiFi AP scanning is gated to the WiFi Config page.** The periodic scan now only
  runs while that page is active, so it no longer interrupts large downloads or
  in-flight connections.

### Added

- **EPD driver auto-detection for SSD1677 / SSD2677.** The panel model is resolved by
  reading the controller chip ID (SPI `0x70`) and, when unavailable, by sampling the
  BUSY idle level, with SSD1677 as the final fallback. This lets the same firmware
  run on both the original and the SSD2677-equipped devices.

[0.1.0]: https://github.com/Clair2929/reterminal-sticky-bus-eta/releases/tag/v0.1.0
