# Security Policy

Thank you for helping keep the NEXUS CSI Sensor and the people who run it safe.
This project ships as a network-connected device with a web dashboard and
over-the-air updates, so we take reports seriously and respond quickly.

---

## Supported versions

Security fixes land on the most recent release line. Older lines are not
back-patched.

| Version | Supported |
|---------|-----------|
| 1.0.x   | Yes       |
| < 1.0   | No        |

If you are running an unreleased build from `main`, please reproduce on the
latest tagged release before reporting so we know whether the issue is already
fixed.

---

## Reporting a vulnerability

**Please do not open a public issue for a security problem.** Public issues are
visible to everyone, including people who might abuse the flaw before a fix is
out.

Use one of these private channels instead:

1. **GitHub private vulnerability reporting (preferred).** Go to the repository's
   **Security** tab and choose **Report a vulnerability**. This opens a private
   advisory that only you and the maintainers can see.
2. **Email.** If private reporting is unavailable to you, email the maintainers
   at the security address listed on the repository profile. If you cannot find
   one, open a normal issue that says only "I would like to report a security
   issue privately, how should I reach you?" and share no details in it.

### What to include

The more of this you can give us, the faster we can confirm and fix it:

- A clear description of the issue and why you believe it is a security problem.
- The affected version or commit, and the hardware you tested on.
- Step-by-step reproduction, including any request payloads, and whether the
  attacker needs to be authenticated or on the local network.
- The impact you were able to demonstrate (information disclosure, auth bypass,
  remote code execution, denial of service, and so on).
- Any proof-of-concept code or captures. Please keep these private to us.

---

## What to expect

We are a small project, so these are honest targets rather than a contractual
SLA:

| Stage | Target |
|-------|--------|
| First acknowledgement | within 3 business days |
| Initial assessment and severity | within 7 business days |
| Fix or documented mitigation | tracked in the private advisory until resolved |

We practice **coordinated disclosure**. We will work with you on a fix, agree on
a disclosure date, and credit you in the release notes and the advisory unless
you would rather stay anonymous. Please give us a reasonable window to ship a
fix before any public writeup.

---

## Scope

**In scope**

- The firmware in [`firmware/`](firmware/): the HTTP server, REST API,
  WebSocket, authentication and session handling, CSRF protection, OTA update
  path, and configuration/storage code.
- The dashboard assets in [`web/`](web/).
- Anything that lets a network attacker bypass authentication, read another
  session, tamper with a firmware update, or crash or brick the device.

**Out of scope**

- Attacks that require physical access to the board (UART, JTAG, chip
  decapping). The ESP32 is not a secure element and we do not claim it resists a
  local attacker with the hardware in hand.
- The default credentials themselves. They are documented and you are expected
  to change them on first login (see below). "The default password is known" is
  not a vulnerability.
- Denial of service that only works from the local WiFi network the device is
  intentionally joined to, unless it is disproportionate (for example, one
  unauthenticated request permanently bricks the device).
- Findings in third-party dependencies (ESP-IDF, mbedTLS, cJSON, Chart.js).
  Please report those upstream, though we are glad to hear about them.

---

## The security model in one paragraph

The device authenticates dashboard users with salted **SHA-256** password
hashing and constant-time comparison, issues **HttpOnly**, **SameSite=Strict**
session cookies with a one-hour lifetime, and requires a **CSRF token** on every
state-changing request. Login attempts are rate-limited with a lockout to blunt
brute force. Firmware updates are written to an inactive OTA partition, verified,
and only marked valid after a healthy boot, so a bad image rolls back instead of
bricking the device. Responses carry `nosniff` and anti-framing headers. None of
this helps if the device is exposed directly to the internet or left on default
credentials, so please read the hardening notes.

For the full design, see
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and the security sections of
[`README.md`](README.md).

---

## Hardening checklist for operators

You own most of the real-world risk. Please do all of these:

- [ ] **Change the default login immediately.** The factory account is
      `admin` / `nexus-admin` and it is public knowledge. Change it the first
      time you log in. See [`docs/USAGE.md`](docs/USAGE.md#first-run-connect-it-to-your-wifi).
- [ ] **Keep the device on a trusted network.** Treat it like any other IoT
      device: put it on your normal or IoT VLAN, not on a public hotspot.
- [ ] **Never port-forward the dashboard to the internet.** It is designed for
      local network use. If you need remote access, reach it through a VPN.
- [ ] **Keep firmware current.** Apply releases that mention security fixes
      promptly through the OTA page.
- [ ] **Power it from a stable supply.** Brown-outs corrupt state and can
      interrupt an update.

---

## A note on responsible use

This is a sensing project. Please only deploy it in spaces you own or control,
and be transparent with the people who share those spaces. The
[responsible-use guidance](docs/USE_CASES.md#using-this-responsibly) is part of
the project for a reason.
