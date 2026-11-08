// Lumen licences. A licence is a line of text signed (Ed25519) with a private
// key that only the author keeps; Lumen carries the matching public key and
// checks it without internet. Each licence names the computers it opens on by
// their machine code. Without one, Lumen runs fully for a trial period and then
// shows an "evaluation" watermark on its outputs (it never stops a show).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct License {
  std::string name;                  // who it is for (shown in Ajuda → Licença)
  std::string id;                    // the author's own number, e.g. "L-0007"
  std::vector<std::string> machines; // machine codes it opens on
  std::string expires;               // "YYYY-MM-DD"; empty = never
  std::string issued;                // "YYYY-MM-DD"
};

enum class LicenseCheck { Ok, Empty, Malformed, BadSignature, OtherMachine, Expired };
const char *licenseCheckText(LicenseCheck c); // for the interface (Portuguese, translated by T())

// This computer's code, "1A2B-3C4D-5E6F-7A8B", from the operating system's own
// id (Windows: MachineGuid; Linux: machine-id; macOS: hardware UUID). It stays
// the same across Lumen reinstalls; a Windows reinstall changes it.
std::string machineCode();
std::string machineCodeFrom(const std::string &osId);
// Upper case, groups of four: what a customer types or pastes with spaces or
// lower case still matches. Empty if it is not 16 hex digits.
std::string normalizeMachineCode(const std::string &code);

// Verifies `text`: the "LUMEN1.<data>.<signature>" line. Spaces, line breaks
// and -----BEGIN/END----- lines are ignored, so an e-mail or a .lumenlic file
// can be pasted as it is. `out` is filled whenever the signature is good (even
// for another machine or an expired licence). publicKey null: the key built
// into Lumen.
LicenseCheck checkLicense(const std::string &text, const std::string &machine, const std::string &today,
                          License &out, const uint8_t *publicKey = nullptr);

// The licence stored on this computer (machine settings, or its copy in
// licenca.lumenlic in the settings folder).
std::string storedLicense();
// Checks `text` for this computer and, if valid, stores only the licence line
// (not the e-mail around it) in both places.
LicenseCheck installLicense(const std::string &text, License &out);

// The author's side (lumen-keygen and the tests).
void licenseKeyPair(const uint8_t seed[32], uint8_t secretKey[64], uint8_t publicKey[32]);
std::string makeLicense(const License &l, const uint8_t secretKey[64]);
const uint8_t *builtinLicensePublicKey();
std::string toBase64(const uint8_t *data, size_t size);
bool fromBase64(const std::string &text, std::vector<uint8_t> &out);

// Dates as "YYYY-MM-DD" (local).
std::string todayLocal();
long long dayNumber(const std::string &ymd); // days since 1970-01-01; -1 if not a date

// Trial: kTrialDays from the first run on this computer.
constexpr int kTrialDays = 30;
struct TrialInfo {
  int daysLeft = kTrialDays;
  bool expired = false;
};
// The decision alone (tested): first day and latest day seen (-1 = none),
// today, and whether a stored record was altered by hand.
TrialInfo trialFrom(long long firstDay, long long seenDay, long long today, bool tampered);
// Reads and updates the records on this computer (settings and, on Windows,
// the registry), then decides.
TrialInfo trialCheck(const std::string &machine, const std::string &today);

} // namespace lumen
