#pragma once

// Provisioning-Vollbild mit einem zentrierten QR-Code: scannen verbindet
// das Smartphone direkt mit dem AP. Der Captive-DNS-Hijack des Geraets sorgt
// danach dafuer, dass der Browser automatisch die Setup-Seite oeffnet.
// SSID + Passwort + URL erscheinen daneben als Fallback fuer manuellen Connect.

#ifdef __cplusplus
extern "C" {
#endif

void qr_screen_show(const char *ap_ssid, const char *ap_pass,
                    const char *setup_url);

// Optional: Status-Text unter den QR-Codes ueberschreiben (z.B. "Client
// verbunden - oeffne Browser auf 192.168.4.1").
void qr_screen_set_status(const char *text);

#ifdef __cplusplus
}
#endif
