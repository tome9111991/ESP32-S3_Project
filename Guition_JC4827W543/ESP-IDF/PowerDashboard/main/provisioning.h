#pragma once

// AP-basierte Erstkonfiguration + dauerhafter HTTP-Server fuer Settings.
//
// - provisioning_start_ap() : SoftAP hoch + QR-Screen + HTTP-Server.
//   Blockiert nicht. Generiert SSID/Passwort und liefert sie via Out-Params.
// - provisioning_start_http_only() : nur HTTP-Server (im Normal-Modus).

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void provisioning_start_ap(char *ssid_out, size_t ssid_size,
                           char *pass_out, size_t pass_size);

void provisioning_start_http_only(void);

void provisioning_start_ssdp(const char *ip_addr);

void provisioning_stop(void);

// Diagnose-JSON kommt aus main.cpp, der HTTP-Server reicht es nur weiter.
int powerdash_diag_json(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
