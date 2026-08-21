# HamCore

**HamCore** is a modified, unencrypted fork of the MeshCore firmware specifically tailored for Amateur (Ham) Radio operating conditions and regulatory compliance.

> **Regulatory Notice:** This firmware removes all payload encryption and session key derivation to comply with amateur radio licensing rules prohibiting coded or obscured transmissions (such as UK Ofcom and FCC Part 97 rules).

---

## Key Features & Changes

* **Full Encryption Removal:** All message encryption, transport keys, and secret handling have been stripped out to ensure 100% unencrypted, compliant operation on ham bands.
* **Remote Admin Disabled:** Remote administrative commands are disabled to prevent unauthorized over-the-air modification of node settings.
* **Ham Band Defaults:** Default radio profiles are tuned out-of-the-box for the 70cm Amateur Radio Band.
* **Smart GPS Hibernation:** Integrated low-power software sleep and wake management for u-blox M10 GNSS modules (such as the Beitian BE-220). Toggling GPS "Off" in the UI commands the module into a ~15 uA software backup mode rather than leaving it powered in the background.
* **Updated Display UI:** Modified startup screen replacing the stock MeshCore branding with HamCore for instant visual identification of unencrypted firmware.
* **Streamlined Build Targets:** Cleaned up target board variants for easier maintenance and compilation (additional targets can be re-added as needed).

---

## Changelog

### v0.01a

1. **License Compliance:** Removed all payload and transport encryption layer logic.
2. **Security:** Disabled Remote Admin functionality.
3. **Target Cleanup:** Simplified board targets for improved build maintainability.
4. **RF Defaults:** Shifted default radio configurations into the 70cm amateur band allocation.
5. **Companion Branding:** Replaced stock logo and `meshcore.io` references on the OLED display with "HamCore" branding.
6. **Hardware Power Savings:** Added u-blox M10 UBX software hibernation support when GPS is toggled off in settings.

### v0.02a
1. **Bug Fix - Repeater self_id all 0's:** Fixed identity generation and advert content.
2. **Target Cleanup:** Removed Heltec WSL3 and tidied up the v3 variant.
3. **Repeater Branding:** Replaced stock logo and `meshcore.io` references on the OLED display with "HamCore" branding.

### v0.03a
1. **Bug Fix - Repeater showing all 0's during node discovery:** Node Discovery response sending all 0's for name and id, this has been updated to send correct data.
2. **Bug Fix - GPS Sleep not working as intended:** Switch RX and TX to inputs after sending the command to go into low power mode in attempt to stop parasitic back-powering. **NEEDS TESTING**
3. **Bug Fix - App showing no repeats even when repeaters are in range and repeating the message:** implemented fix for missing heard repeats. **NEEDS TESTING**
