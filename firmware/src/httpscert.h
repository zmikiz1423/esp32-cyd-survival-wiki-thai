// Self-signed certificate for https://192.168.4.1, so a phone on the hotspot can use the browser's
// location feature (browsers only allow it on https pages). Phones will show a security warning once.
//
// NOTE: this key is public (it ships in every firmware build), so it gives encryption but no proof of
// identity. Do not reuse it for anything else. To make a new pair, for example:
//   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 36500 \
//     -subj "/CN=Survival Wiki" -addext "subjectAltName=IP:192.168.4.1,DNS:survival.wiki" -keyout key.pem -out cert.pem
#pragma once

static const char HTTPS_CERT[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBojCCAUigAwIBAgIUHgaApOdbWTgKJX1CXhuPxB69HREwCgYIKoZIzj0EAwIw
NzEWMBQGA1UEAwwNU3Vydml2YWwgV2lraTEdMBsGA1UECgwUU3Vydml2YWwgV2lr
aSBkZXZpY2UwIBcNMjYxMDMwMTcyMDA0WhgPMjEyNjEwMDcxNzIwMDRaMDcxFjAU
BgNVBAMMDVN1cnZpdmFsIFdpa2kxHTAbBgNVBAoMFFN1cnZpdmFsIFdpa2kgZGV2
aWNlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE8HQIYkW+I/yG4YSh0WEPts2R
9EnnjFDZ/iiCApAz21LDpbHAfUWkB3BIYcPkXQo2IaVoGqyy65wFrOiU+zjkF6Mw
MC4wHgYDVR0RBBcwFYcEwKgEAYINc3Vydml2YWwud2lraTAMBgNVHRMBAf8EAjAA
MAoGCCqGSM49BAMCA0gAMEUCIQC4n7By9Ee4q8yObNYVbyz1YB5c2Lr3iGf+rhbY
tnjHrwIgTZrxtgBg2MB/TM/TVN5kFZY/B4pZehW3Drcyoon/kVc=
-----END CERTIFICATE-----
)PEM";

static const char HTTPS_KEY[] = R"PEM(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgXZETuGfFGXh/a31m
PVBs+tRXMpmu73NBJT0/qBPmxMahRANCAATwdAhiRb4j/IbhhKHRYQ+2zZH0SeeM
UNn+KIICkDPbUsOlscB9RaQHcEhhw+RdCjYhpWgarLLrnAWs6JT7OOQX
-----END PRIVATE KEY-----
)PEM";
