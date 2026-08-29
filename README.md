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
2. **Bug Fix - GPS Sleep not working as intended:** Switch RX and TX to inputs after sending the command to go into low power mode in attempt to stop parasitic back-powering.

### v0.04a
1. **Bug Fix - Messages tagged with a region scope are flooded out unscoped:** Fixed the way the modified code decided if a message was scoped or not.
2. **Code Cleanup - Cleaned up unused code:** Removed some more now unused code.
3. **Bug Fix - Various:** Fixed various small issues found during code cleanup

### v0.05a
1. **Bug Fix - Channel Hash:** Fixed broken channel hash after removal of encryption which was breaking some repeater functionality.
2. **Bug Fix - Packet Length:** Removal of encryption left packet header 2 byte shorter, this broke some other stuff so have padded those 2 byte with 0's,  May remove the unused bytes again in the future.
3. **Code Cleanup - Removed some unused code:** Removed some more code that is no longer used.
4. **Unit Tests Fail:** Removed Unit tests for Kiss Modem firmware variant as it is not used in this fork.
5. **PR Build Tests Fail:** Fixed PR Build Test by commenting out currently unused targets.
6. **New Board - WY_RPT:** Added WY_RPT build target and added to the PR build test.
7. **New Feature - Band Edges:** Added constraints to limit frequency selection to within the UK 70cm band.
8. **New Feature - Path Hash Mode:** Path hash mode locked to 2 so that 3 byte hash is always used.

### v0.1.0
1. **Bug Fix - App error during setup:** App throws an error when setting the radio settings in the setup wizard, Firmware was rejecting changing the Path Hash Mode but the app reads the settings and then writes them back to the node.  Firmware now accepts the write from the app but does not change the value that is in use.
2. **New Feature - TX Inhibit:** TX on Repeaters and Companions is disabled until after the node name has been changed. This prevents transmitting without a callsign - though for now no confirmation is made that a callsign was set in the name. 
3. **Bug Fix - BLE Name Prefix:** Locked the companion's Bluetooth advertised name prefix to `HamCore-` so no board variant can override it. Our companion app only lists devices advertising that prefix, so a variant-specific override would have made the device invisible to the app.
4. **New Feature - Distinct Guest/Admin Remote Login:** Replaced the old shared password-string login with two distinct request types, one per app login button. Guest login requires no authentication at all and is always granted (never more than guest, never downgrading a client already provisioned locally with higher permissions). Admin login is a separate, currently-rejected request type reserved for future signature/pubkey-based authentication - Remote Admin stays fully disabled until that's implemented.
5. **Bug Fix - Heard Repeats:** Fixed bug that was causing channel PSK to not be stored properly, even though this was no longer used for encryption the channel hash is used for other stuff like heard repeats. 
6. **Bug Fix - Guest Login Never Replied:** `sendLogin()` was still sending the old password string, so the byte the repeater checked against the new guest/admin request types never matched anything and no reply was ever sent. The sender now sends the matching login-type byte instead, for repeater (non-room) logins.
7. **Bug Fix - TX Inhibit Sent Stale Name:** The first message queued before the name was changed (eg. the boot-time self-advert) was still transmitted as-is once TX inhibit lifted, carrying the old name. Anything queued while transmission is withheld is now discarded instead of held. Also, changing the name now triggers an immediate re-advert on both Repeaters and Companions, instead of waiting for the next periodic interval.
8. **Bug Fix - Repeater Never Answered Status/Telemetry Requests:** The repeater's handling of incoming request packets was left over from when remote admin was first blanket-disabled, and unconditionally ignored every request (including guest-permitted ones like status). It now correctly dispatches to the existing request handler and replies, while remote CLI-over-radio commands stay blocked as intended.
