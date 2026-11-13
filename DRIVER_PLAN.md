# Drivers for Marconi: fork ViGEmBus, or write our own?

Research and plan, written 2026-11-13. Nothing here is built. It is an **Idea** in `ROADMAP.md` and waits for the
owner's go-ahead (section 9 lists the decisions). It answers the owner's question: "can we make our own fork of
ViGEmBus? or if not our own driver? we might want drivers for a bunch of stuff".

How to read the marks: every fact has a source with its URL and the date I read it (all 2026-11-13). Anything I
could not confirm from a source is marked **UNVERIFIED**. Estimates of time and money are mine, marked
**ESTIMATE**, not facts.

## 1. Summary for the owner

**Short answer.** Yes, you can legally fork ViGEmBus, but it is probably the wrong thing to do. A fork would make
Red Marmoset the maintainer of a Windows kernel driver, which means a yearly certificate, a Microsoft developer
account, security fixes forever, and a driver that can stop working on a Windows update. And the people who made it
gave it up because they ran out of time.

**What is true today**

- ViGEmBus is free to copy and change (BSD-3-Clause licence). Its author retired and archived it on 2 Nov 2023. The
  last version is 1.22.0. It still works on Windows 11 according to third-party sites, which I could not
  independently confirm. The name "ViGEm" is retired after a trademark dispute, so a fork needs a new name.
- Shipping *any* Windows driver of your own needs: a Microsoft hardware developer account, an **EV code-signing
  certificate** (about 250 to 600 pounds a year, a hardware token, and a company check), and Microsoft's signature on
  each driver release. It is days to weeks to set up. After that each release is a submission you make, a few days
  at most (**ESTIMATE**).
- Microsoft's own virtual gamepad route (VHF) is kernel-mode only, and games that use only XInput will not see it.
  Only a driver that looks like a real Xbox 360 pad (what ViGEmBus does) gets Xbox-only games working.
- Linux needs no driver for any of this (uinput). On a Mac you need Apple to grant an entitlement, and a free,
  already-working open-source driver (Karabiner's) covers keyboard and mouse but, as far as I could read, not
  gamepads.
- Android and iOS cannot host drivers at all. They are only ever the sending end.

**Recommendation (details in section 8)**

1. **Now:** ship the existing ViGEmBus 1.22.0 as an optional extra, exactly as `SERVER_PLAN.md` decision 6 says,
   behind the controller interface so it can be swapped. No certificate needed for this.
2. **Next, a cheap test (about a week):** check whether the *USB/IP* route works for an Xbox 360 identity: Microsoft's
   in-box driver plus a signed, BSD-licensed USB/IP driver (usbip-win2) that someone else maintains, with our own
   user-mode code on top. If XInput games see it, Red Marmoset never owns a kernel driver.
3. **Only if that fails, or when you want a virtual screen or virtual microphone:** register the company with
   Microsoft (EV certificate and Partner Center account) and write the *smallest possible* driver ourselves,
   starting with the one that is user-mode (virtual display).
4. **Mac:** apply to Apple for the DriverKit entitlement now, because it costs nothing and approval can take weeks
   or months. Use Karabiner's driver for keyboard and mouse in the meantime.

**The three most important unknowns**

1. Does a virtual Xbox 360 pad made over USB/IP (usbip-win2 plus our own code) show up in XInput games and keep
   rumble? Nobody I could find has confirmed it (section 3.2). It decides whether we ever need our own kernel driver.
2. May a driver signed by *attestation* be shipped to customers in our own installer? Microsoft's page calls
   attestation signing "for testing scenarios" and says such drivers cannot go on Windows Update; real HLK-tested
   (WHCP) signing is the route for retail. I could not confirm where the line is for an own-installer release
   (section 2.3).
3. Whether Apple will grant a DriverKit HID entitlement to a small studio, and how long it takes. The reports are
   mixed and mostly years old (section 5.2).

## 2. Shipping any driver on Windows in 2026

### 2.1 What you need (all of it, in order)

| Step | What | Source |
|---|---|---|
| 1 | An **EV code-signing certificate** in the company's name, from one of the approved authorities (Certum, DigiCert, GlobalSign, IdenTrust, Sectigo, SSL.com). It is needed to *register* for the hardware program and for every attestation or WHCP submission. | [Microsoft: code signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs) (page dated 2025-05-29, read 2026-11-13) |
| 2 | A Microsoft Entra ID (Azure AD) tenant and a **global administrator** account (can be created free during registration), plus a named **legal contact** who can sign agreements and receive email from Microsoft. | [Microsoft: register for the Hardware Developer Program](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/hardware-program-register) (page dated 2026-08-11) |
| 3 | Register in Partner Center (company details or a D-U-N-S number), accept four agreements (Code Signing Agreement, WHCP agreement, Microsoft Marks licence, Windows Analytics agreement), upload the EV certificate, answer a business questionnaire emailed to the global administrator, wait for approval. | same page |
| 4 | Build with the Windows Driver Kit, put the driver in a CAB, sign the CAB with the EV certificate, submit it in the Hardware dashboard; Microsoft re-signs it. | [Microsoft: attestation signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation) (page dated 2025-07-15, updated 2026-04-14) |

**Azure Trusted Signing (Microsoft's cheaper cloud signing) does not replace the EV certificate for driver
submissions.** The only source is a Microsoft Q&A thread, which cites the Trusted Signing FAQ
([Q&A, read 2026-11-13](https://learn.microsoft.com/en-us/answers/questions/5866910/hardware-program-verification-using-azures-trusted)).
Treat as UNVERIFIED until checked against the FAQ itself.

### 2.2 Cost and time for a UK limited company

| Item | Figure | Source and status |
|---|---|---|
| EV certificate | About 244 to 340 pounds a year from UK resellers (Sectigo 244.36, Comodo 332 to 339, GlobalSign 298 to 316); DigiCert 598 US dollars a year at a US reseller. A hardware token is included or a cloud signing service is used. | [Sectigo at a UK reseller](https://cheapsslshop.com/uk/sectigo-ev-code-signing-certificates), [GlobalSign at a UK reseller](https://globalsign.tbs-certificates.co.uk/code_signing_globalsign.html) (read 2026-11-13). Reseller prices, not the authorities' own. Get a quote from the authority. |
| Certificate lifetime | A vendor says from 1 March 2026 the maximum validity of public code-signing certificates is 458 days, so budget a renewal about yearly. | A vendor page only; **UNVERIFIED** against the CA/Browser Forum text. |
| Company vetting | Full business vetting: legal name, address and phone that match an independent register. For a UK company the Companies House record is the natural match. | Reseller text ([cheapsslsecurity.com](https://cheapsslsecurity.com/comodo/evcodesigningcertificate.html)); exact documents for Red Marmoset **UNVERIFIED**, the authority will say. |
| Partner Center hardware account | I found no fee on the registration page. | **UNVERIFIED** that it is free; confirm at registration. |
| Setup time | Certificate: days to a couple of weeks (**ESTIMATE**). Partner Center approval: depends on Microsoft's questionnaire (**UNVERIFIED**, plan on one to three weeks). | |
| Each release afterwards | Attestation: submit and download, usually minutes to a day (**ESTIMATE**, unconfirmed). | |
| HLK testing (WHCP) | Needs the Windows Hardware Lab Kit and a test setup, a day or more per driver and Windows version (**ESTIMATE**). It is the recommended method and signs for all OS versions. | [Microsoft: driver signing options](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings) (page dated 2026-03-23) |

### 2.3 Attestation or WHQL: which one is allowed to ship?

What Microsoft's page says, quoted in substance ([driver signing options](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings), read 2026-11-13):

- Attestation signing is offered "for testing purposes only", does not need HLK tests, works on Windows 10 desktop
  and later, and supports kernel-mode and user-mode drivers.
- Attestation-signed drivers **cannot be published to Windows Update for retail audiences**. For that you go
  through the Windows Hardware Compatibility Program (WHCP, the HLK route).
- An attestation signature means Windows trusts the driver, not that it is "Windows Certified".
- HLK-tested, dashboard-signed drivers work on all Windows versions.

What I could not settle: whether handing an attestation-signed driver to customers inside our own installer is
*permitted* (as opposed to technically working). Microsoft's wording suggests the intended retail path is WHCP.
Practice in this space (ViGEmBus's own release notes mention "WHQL tested and signed" for 1.16.115, per
[its releases page](https://github.com/nefarius/ViGEmBus/releases)) leans to the WHCP route. **UNVERIFIED.** Ask
Microsoft Hardware Dev Center support before committing; it is the single point that changes the plan's cost.

### 2.4 Development: test signing, Secure Boot and HVCI

- Test signing: `bcdedit -set TESTSIGNING ON`, reboot, a "Test Mode" watermark appears. If Secure Boot is on, Windows
  refuses the change, and you must switch Secure Boot off in the firmware. With Memory Integrity (HVCI) on, the
  binary still has to be test-signed with any self-made certificate; unsigned is not allowed.
  ([Microsoft: loading test-signed code](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option), page dated 2025-04-16.)
- Better for a developer's own machine: **preproduction signing** lets a test machine keep Secure Boot, HVCI and
  code integrity on while loading a pre-release driver, but only on machines provisioned to trust it, and it is
  submitted through the Hardware Dev Center APIs
  ([driver signing options](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings)).
- HVCI: the vulnerable-driver blocklist is also enforced when HVCI, Smart App Control or S mode is on, and is
  updated with each major Windows release; HVCI is on by default on most new Windows 11 devices
  ([Microsoft: recommended driver block rules](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/design/microsoft-recommended-driver-block-rules),
  and the search summary I read; the "on by default" wording is from secondary pages). So any driver of ours must
  be HVCI-compatible (no executable writable memory and so on) or users with Memory Integrity on cannot load it. The
  exact HVCI rules for our driver: **UNVERIFIED**, to be checked with the HLK and Driver Verifier.
- Secure Boot itself does not block a properly signed driver. Nothing I read says otherwise.

### 2.5 What you owe after shipping (**UNVERIFIED** as a list; this is my reading of the above)

Rebuild and resubmit when a new Windows release changes something or when the certificate expires; answer security
reports (a bus driver that lets programs create devices is exactly what attackers look for, so restrict who may talk
to it); keep working on Windows on Arm if wanted (ViGEmBus gained ARM64 in 1.21.442, per its releases page); keep
the Hardware account, EV certificate and a second person who can sign in, or the product can no longer be updated.

## 3. ViGEmBus

### 3.1 Facts

| Question | Answer | Source |
|---|---|---|
| Licence | **BSD-3-Clause** (confirmed on the project's page). Practical meaning: you may fork, change and sell it; keep the copyright notice and licence text in source and in the documentation or other materials of binary releases; do not use the author's name to promote the result. | [GitHub README, read 2026-11-13](https://github.com/nefarius/ViGEmBus) |
| Status | Archived and read-only since 2 Nov 2023; "project retired". Last release **1.22.0** ("It's dead, Jim"), the updater removed. In-place upgrades reportedly fail; running the setup twice works. | [GitHub README](https://github.com/nefarius/ViGEmBus), [releases](https://github.com/nefarius/ViGEmBus/releases) |
| Why | A trademark conflict with ViGEM GmbH found in May 2023 led to a mutual agreement; the name "ViGEm" and the vigem.org domain were retired in late 2023. The author also cites limited time after seven years mostly alone. | [End-of-life statement](https://docs.nefarius.at/projects/ViGEm/End-of-Life/) |
| What it is | A Windows kernel-mode driver emulating well-known USB game controllers (Xbox 360, DualShock 4). | README |
| Windows 11 | Release notes say 1.18.367 supports "Windows 10/11 only". Third-party guides say 1.22.0 works on Windows 11 24H2 and with HVCI. I found no authoritative test and no named, confirmed breakage. | [XOutput explainer](https://xoutput.org/what-is-vigembus/) (third-party; **UNVERIFIED**) |
| Known risk | Older installs contacted `updates.vigem.org`, a domain the project no longer owns (privacy: it can see the IP address). 1.22.0 removed the updater. | [End-of-life statement](https://docs.nefarius.at/projects/ViGEm/End-of-Life/) |

### 3.2 What a fork would have to do and keep doing

A fork is allowed by the licence. What it would really take:

1. **Rename** everything: the "ViGEm" name is retired under an agreement I have not seen (**UNVERIFIED** terms).
   Change driver name, service name, device IDs and client library names so it cannot clash with an installed
   original (**ESTIMATE**: needed, I could not check the code from here).
2. **Sign it ourselves.** Nefarius's Microsoft signature applies to his exact binaries; any change needs a new
   submission under Red Marmoset's account (section 2).
3. **Maintain it:** Windows Driver Kit updates, HVCI and Windows 11/12 changes, ARM64, security review, and support
   for people whose game does not see the pad. The author said this alone was what he could not keep up with.
4. **Possibly drop the parts we do not need.** Marconi only needs "create a pad, send state, receive rumble", not
   the whole client library. I have not read the source size or language (**UNVERIFIED**).
5. **Ask whether it is the right base at all.** A 2023-era bus driver written to be used by anything on the machine
   is a larger attack surface than we need.

Cost, **ESTIMATE**: 2 to 4 weeks of an engineer who knows the Windows driver model for the first signed fork, then
a few days a year plus emergencies. Without such an engineer the first release is slower and riskier; there is
none in the team today as far as I know (**UNVERIFIED**; owner to say).

### 3.3 Successors and alternatives that exist today

| Project | What it is | Licence | Status I could confirm |
|---|---|---|---|
| **ViGEmBus 1.22.0** | the original | BSD-3-Clause | retired, still shipped (above) |
| **VirtualPad** (Nefarius) | The announced successor, a "Nefarius VirtualPad Framework" | A DS4Windows contributor read its licence as proprietary and noted a clause against redistributing the installer without permission ([discussion](https://github.com/Ryochan7/DS4Windows/discussions/3124)). **UNVERIFIED**: I did not find the licence text. | Appears in the author's docs only as a menu entry ([docs](https://docs.nefarius.at/projects/ViGEm)); a third-party site says it is in progress or commercial. Could be a paid option, ask Nefarius. |
| **VIIPER** (Alia5) | Creates virtual USB devices over USB/IP: Xbox 360 pad, DualShock 4, DualSense, Switch 2 Pro, HID keyboard and mouse. Runs on Linux and Windows. | **GPL-3.0** (client libraries MIT) | Active (hundreds of commits, open issues); DS4Windows 5 candidates reportedly use it ([README](https://github.com/Alia5/VIIPER); the DS4Windows claim is from a search summary, **UNVERIFIED**). Whether games see its Xbox 360 pad as XInput: the README does not say. |
| **usbip-win2** (vadimgrn) | The USB/IP client driver VIIPER needs on Windows. It "comes with a signed kernel driver" (WHLK or attestation, signed through the Open Source Codesigning Initiative). | **BSD-2-Clause** | Active (Windows 10 1903+, Windows 11 ARM64) ([README](https://github.com/vadimgrn/usbip-win2)). A downstream project reports a race condition affecting audio endpoints in usbip-win2 issue #181 (secondary; **UNVERIFIED**), said not to affect controllers. |
| **HidHide**, **DsHidMini** (Nefarius) | Hides a real controller from other programs; a driver for PS3 pads | not checked | Both appear in the author's docs menu; I did not confirm their state. HidHide matters only if Marconi someday wants to hide a physical pad. |

**Licence consequence for Marconi:** Marconi is MIT (`LICENSE`). Linking GPL-3.0 VIIPER into Marconi would make
the combined program GPL. Running it as a separate program that we ship and talk to may be acceptable, but that is
a legal question (**UNVERIFIED**, not legal advice). Writing our own small USB/IP server (the protocol is public,
in the Linux kernel documentation) avoids it.

## 4. Windows without a custom kernel driver, by device

### 4.1 Gamepads: what games see

| Route | Needs our own driver? | Games see | Notes |
|---|---|---|---|
| ViGEmBus 1.22.0 | No (someone else's) | Xbox 360 pad through XInput (and as an XUSB device in DirectInput) | Works with nearly all PC games. Unmaintained. |
| **VHF** (Virtual HID Framework) | **Yes**, a kernel-mode driver | A HID game controller, through DirectInput, raw HID, SDL, Steam Input, GameInput | Microsoft states: "VHF supports a HID source driver only in kernel mode." Its sample is a KMDF driver loading `vhf.sys` as a lower filter ([VHF doc](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-), page dated 2025-04-22). No rumble unless we implement output reports. A user-mode (UMDF) variant exists in Microsoft samples, I believe: **UNVERIFIED**, not opened. |
| USB/IP virtual Xbox 360 device | No (usbip-win2 is signed and maintained by others) | Plausibly an XUSB device, so XInput works | **Unknown #1.** Microsoft says XInput finds devices through the XUSB interface; a HID-only device is invisible to XInput-only games ([DirectInput and XUSB devices](https://learn.microsoft.com/nb-no/windows/win32/xinput/directinput-and-xusb-devices)). If the in-box Xbox driver binds to the virtual device's USB IDs, it would work. Not confirmed anywhere I read. |

**Is an Xbox 360 identity only possible with a bus driver like ViGEm?** Not proven. Today ViGEm is how everyone does
it. A USB/IP loopback is, on paper, a second way (it makes Windows think a real USB device arrived, and Microsoft's
own Xbox driver then loads). It needs a test, not more reading.

**The Windows Gaming Input and GameInput APIs** are for *reading* devices. GameInput's overview describes it as a
single interface to keyboards, mice and gamepads, a "functional superset" of XInput, DirectInput and raw HID
([GameInput introduction](https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/input/overviews/input-overview), page dated 2026-10-29). Nothing there says an application can create or inject a device, so they do not replace a driver
(**UNVERIFIED** that no injection exists; I read one overview page only).

### 4.2 Touch and pen: no driver needed

`CreateSyntheticPointerDevice` is a user-mode function for a desktop app to inject touch (up to the system maximum)
or pen (one contact) from Windows 10 version 1809 ([Microsoft doc](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createsyntheticpointerdevice), read 2026-11-13). Whether it works from a Windows service
or into elevated and secure-desktop windows, as Marconi Server's service model needs, is **UNVERIFIED**; the keyboard
and mouse path has the same question and the service work already faced it. Do this before any driver idea.

### 4.3 Virtual display: Indirect Display Driver (IddCx)

- Microsoft's model for a virtual monitor. It is a **user-mode (UMDF)** driver running in session 0, "so any driver
  instability doesn't affect the stability of the system as a whole". Microsoft supplies a sample
  ([IDD overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview), page dated 2025-11-05).
- It still **is a driver**: it needs the same EV certificate, Partner Center account and Microsoft signature
  (attestation supports user-mode drivers, per the driver-signing page above).
- Safest first driver we could own: user mode cannot crash Windows. Its job for Marconi: a server with no monitor, or
  a laptop with its lid closed, can be given a screen of any size. It also pairs with the existing capture code,
  because Desktop Duplication then just captures the virtual monitor.
- There are open-source implementations (a "Virtual Display Driver" project appears in search results). I did not
  read their licences or signing: **UNVERIFIED**. Check before copying any code.
- **ESTIMATE:** 3 to 6 weeks for a first working virtual monitor if we start from Microsoft's sample, plus signing
  setup (section 2).

### 4.4 Sound

- **Server to client** (hearing the remote computer): no driver. WASAPI loopback capture already is the plan in
  `SERVER_PLAN.md` section 7.
- **Client to server** (the viewer's microphone appearing as a microphone on the server): needs a virtual audio
  device, which is a driver. Microsoft's sample SysVAD is a WDM (kernel-mode) virtual audio driver; a newer ACX or
  SDCA sample exists ([SysVAD](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample), [sample audio drivers](https://learn.microsoft.com/fi-fi/windows-hardware/drivers/audio/sample-audio-drivers)). A developer reported trouble signing a virtual audio driver with an EV
  certificate ([Q&A 2024](https://learn.microsoft.com/en-in/answers/questions/1668206/i-fail-to-sign-a-windows-audio-driver-for-a-virtua), outcome not read). Is a user-mode audio route available? **UNVERIFIED.**
  Suggest: do not plan it; revisit after the display driver.

## 5. Other platforms

### 5.1 Linux: no driver

`/dev/uinput` lets a program create virtual input devices with ioctls and `write()`; the kernel documentation
recommends libevdev ([kernel uinput doc](https://docs.kernel.org/input/uinput.html), read 2026-11-13). The page does not mention
gamepads or the permissions needed (usually a udev rule or the `input` group; **UNVERIFIED**). Marconi Server on Linux
already injects through X11; gamepads via uinput need no certificates. Wayland virtual input: separate, **not researched**.

### 5.2 macOS: DriverKit

- A virtual HID device is a DriverKit extension (a "dext") shipped inside an app. Development builds need no Apple
  approval; **distribution needs entitlements Apple must grant** to the developer account. The entitlement a HID
  dext carries depends on its kernel support class; for `AppleUserHIDDevice` an Apple engineer named
  `com.apple.developer.driverkit.family.hid.device`. An entitlement named `...family.hid.virtual.device` is,
  according to an Apple engineer, defunct. Another one, `com.apple.developer.hid.virtual.device`, is for the
  CoreHID virtual device API and is not for dexts. Apple engineers say the restriction is deliberate because
  virtual HID devices can be used to attack the system. Source: Apple developer forum threads
  ([HID entitlement guide](https://developer.apple.com/forums/thread/843327), [HID virtual device, Mac App Store](https://developer.apple.com/forums/thread/822647)); I read them only as search summaries; **UNVERIFIED** until read in full.
- How to ask: the system-extension request page on developer.apple.com, and bundle the base DriverKit entitlement
  with the HID ones in one request ([Apple: requesting entitlements for DriverKit](https://developer.apple.com/documentation/driverkit/requesting-entitlements-for-driverkit-development); the page text did not load, so the steps come from forum summaries).
- Waiting times: many forum threads report requests sitting "Submitted" for weeks, some 10+ weeks, and a Code-Level
  Support request rather than a normal support case being what moved one
  ([forum](https://developer.apple.com/forums/thread/847383)). **Apply early; it is free.**
- The extension is then signed with the Developer ID and notarized with the app (same as the existing macOS plan),
  and the user approves it once in System Settings.
- **Karabiner-DriverKit-VirtualHIDDevice** ([repo](https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice), read 2026-11-13):
  - **Licence: Unlicense** (public domain style), as its page states.
  - Provides a **virtual keyboard and virtual mouse** built on DriverKit. Its page does not mention gamepads.
  - Active (1,037 commits). Lists macOS 13 to 26 on Intel and Apple Silicon, and 27 on Apple Silicon (as the page
    says; I did not verify the macOS 27 line).
  - You install its `.pkg`, activate the system extension with its manager app, approve it in macOS prompts (a
    restart may be needed), and run its **daemon as root**; client programs must run as root to send input. A
    header-only C++ client library is included.
  - Its signing and entitlements are the Karabiner author's. If we redistribute his package, the Mac user installs
    his extension, signed by him, and we need no entitlement of our own. If we build our own copy, we need our own.
    Which of the two is acceptable under his licence and conventions: the Unlicense allows both; any trademark use
    I did not check.
  - For Marconi a Mac server already has keyboard and mouse injection through the normal accessibility route, so
    Karabiner's driver matters only for things the normal route cannot do. Gamepads on a Mac server would need a
    dext of our own (or checking whether GameController framework has a virtual controller route: **UNVERIFIED**).

### 5.3 Android and iOS: what is impossible

- **Neither can load a driver or create virtual input devices for the whole system.** Both are only ever the
  *sending* end (reading the touch screen, sensors and a connected pad) and the viewing end. That matches what
  Marconi already does.
- **Android exception, not a driver:** an app can present the phone as a Bluetooth HID device (keyboard, mouse,
  gamepad, digitizer) to a host it is paired with, from API level 28 (`BluetoothHidDevice`, one app at a time,
  only while in the foreground)
  ([Android doc](https://developer.android.com/reference/android/bluetooth/BluetoothHidDevice), read 2026-11-13).
  That would let a phone drive a PC pad with no Marconi Server involved. It is a different product from remote
  control and is listed only so the owner knows it exists.
- iOS: I found no equivalent (**UNVERIFIED**); iOS apps cannot publish HID devices to other computers that I know of.

## 6. The interface that keeps the choice open

`SERVER_PLAN.md` Phase 7 already says the server has a `GamepadSink` interface with one implementation per platform
(`VirtualGamepad_Windows.cpp` on ViGEmClient, uinput on Linux, a DriverKit extension on macOS). That is the right
seam: the pad's *state* (buttons, sticks, triggers, rumble) is the same whatever creates the device. Keep three
rules so the Windows part can be swapped between ViGEmBus, USB/IP and our own driver without touching the
protocol or the client:

1. The sink talks in Marconi's own pad state, never in ViGEm structures.
2. Creating a device can fail with a plain-words reason ("the controller driver isn't installed"), and the client
   menu shows it, as `SystemKeyCapture::GetUnsupportedReason` already does.
3. The installer's optional component is described by what it does for the user, not by the driver's name.

## 7. Risks

| Risk | Meaning | Answer |
|---|---|---|
| ViGEmBus stops loading on a future Windows | Controllers silently stop working for users. | Detect and say so in plain words; have the USB/IP route tested as the fallback; keep the sink swappable. |
| We own a signed driver and the certificate lapses or the one person with the token leaves | We cannot release fixes. | Two people on the Partner Center account; calendar the renewal; keep the token somewhere safe. |
| Driver bug blue-screens a user's PC | Reputation, support, possibly liability. | Prefer user-mode drivers (display), tiny kernel code, Driver Verifier, a kill switch in the installer. |
| Security: a bus driver reachable by any program | A new local attack route, and a driver Microsoft may add to the blocklist. | Admin-only access to the control interface; short, reviewed code; do not copy a whole archived driver. |
| Microsoft's signing page changes (EV rule, attestation limits) | Plan cost changes. | Re-read the code-signing pages (section 2) before each phase starts. |
| GPL contamination from VIIPER | Marconi is MIT. | Do not link VIIPER; write our own USB/IP server or ask legal. |
| Apple says no | No Mac gamepad. | Keyboard and mouse still work via the normal route; Karabiner's driver as a fallback for those. |

## 8. Recommended path

| Phase | What | Effort (ESTIMATE) | Money | Gate |
|---|---|---|---|---|
| 0, now | Keep `SERVER_PLAN.md` Phase 7 as written: ViGEmBus 1.22.0 as an optional installer component, behind `GamepadSink`. Add the plain-words README note that the driver is unmaintained. Apply to Apple for the DriverKit HID entitlement. Ask Microsoft Hardware Dev Center whether attestation-signed drivers may be shipped in our own installer. | 1 to 2 days of owner time (the Apple form and the Microsoft question) | 0 | Owner's go-ahead |
| 1 | **USB/IP experiment**, on a Windows machine with usbip-win2: attach a virtual Xbox 360 device to the local machine (try VIIPER first to see whether it works at all, then a tiny test program of our own). Check that an XInput test program and two real games see it, and whether rumble returns. | About 1 week, one Windows machine | 0 | Result decides phase 2 |
| 2a | If phase 1 works: write `VirtualGamepad_Windows` over our own user-mode USB/IP device server, redistribute usbip-win2 (BSD-2) in the installer. No certificate needed for the pad. | 3 to 5 weeks | 0 to little | Phase 1 passed and licence read |
| 2b | If phase 1 fails or the owner wants zero third-party drivers: register the company (EV certificate, Entra, Partner Center), then **fork or rewrite the smallest pad driver**: a bus-style driver that creates an Xbox 360 identity. | Registration 1 to 3 weeks of waiting; driver 4 to 8 weeks | About 250 to 600 pounds a year, plus test hardware | Owner decision (section 9) |
| 3 | **Virtual display driver** (IddCx, user mode), from Microsoft's sample. Reuses the Partner Center account from 2b, or triggers registration. | 3 to 6 weeks | as 2b | Owner wants the virtual display idea |
| 4 | Touch and pen through `CreateSyntheticPointerDevice` (no driver) once the server protocol carries them. | 1 to 2 weeks | 0 | owner wants touch and pen |
| Later | Virtual microphone. Mac gamepad dext. A driver-update process (rebuild per Windows release). | not estimated | | |

**Effort totals (ESTIMATE):** the cheapest good outcome is phase 0 plus 1 plus 2a: about two months, no certificate.
The own-driver outcome is 2b plus 3: roughly four months of one engineer who knows Windows drivers, plus yearly
renewal and a small amount of continuing work.

## 9. What the owner must decide

1. **Go-ahead** for this plan (it is an Idea in `ROADMAP.md` until then).
2. **Ship the existing ViGEmBus 1.22.0 in the meantime?** Recommended yes (it is what `SERVER_PLAN.md` decision 6 and
   the controller interface already allow). It is BSD-3-Clause, so redistribution is allowed with the licence text
   kept. The risk is that it is retired.
3. **Do you want Red Marmoset to own a Windows driver at all?** Own driver: control, but a maintenance duty with a
   certificate. USB/IP route: nothing to own, but a dependency on usbip-win2 and its maintainer.
4. **Whether the virtual display, virtual microphone, touch or pen ideas matter enough to justify the company
   registration** (that is the step with a cost and a wait).
5. **Who is the "legal contact" and global administrator** for Microsoft (two named people, with working email).
6. **Is a paid VirtualPad licence worth a conversation?** Only if the owner prefers paying to maintaining; its terms
   are UNVERIFIED.
7. **Licence stance:** are we happy to ship GPL parts as separate programs? Default no.

### Checklist: what the owner must obtain

Windows:
- [ ] Company details ready: Companies House registered name, number and address; a business phone number listed
      independently; a D-U-N-S number is optional ("I don't have one" is an option in registration).
- [ ] EV code-signing certificate (hardware token or cloud signing; about 250 to 600 pounds a year).
- [ ] Microsoft Entra ID tenant with a global administrator, and a legal contact who can sign (both with working email).
- [ ] Partner Center "Hardware" program registration, the four agreements, the EV certificate uploaded, the
      questionnaire answered.
- [ ] A test PC dedicated to drivers (virtual machine for the first tests, a real one for games), Windows Driver Kit
      and Visual Studio, and the Windows Hardware Lab Kit if going the WHCP route.
- [ ] Written answer from Microsoft on shipping attestation-signed drivers in an own installer (the Hardware Dev Center
      support route).

macOS:
- [ ] Apple Developer Program membership in the company's name (already needed for the Mac and iOS apps).
- [ ] DriverKit entitlement request for HID, through the system-extension request page (and, if it stalls, a
      Code-Level Support request).
- [ ] A Developer ID certificate and notarization (already in the Mac plan).

Linux, Android, iOS: nothing to obtain.

## 10. Sources (all read 2026-11-13)

- ViGEmBus: https://github.com/nefarius/ViGEmBus , https://github.com/nefarius/ViGEmBus/releases ,
  https://docs.nefarius.at/projects/ViGEm/End-of-Life/ , https://docs.nefarius.at/projects/ViGEm
- Successors: https://github.com/Alia5/VIIPER , https://github.com/vadimgrn/usbip-win2 ,
  https://github.com/Ryochan7/DS4Windows/discussions/3124 , https://xoutput.org/what-is-vigembus/
- Microsoft signing: https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs ,
  .../code-signing-attestation , .../driver-signing-offerings , .../hardware-program-register ,
  https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option
- Microsoft APIs: https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf- ,
  https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview ,
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createsyntheticpointerdevice ,
  https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/input/overviews/input-overview ,
  https://learn.microsoft.com/nb-no/windows/win32/xinput/directinput-and-xusb-devices
- Certificates: https://cheapsslshop.com/uk/sectigo-ev-code-signing-certificates ,
  https://globalsign.tbs-certificates.co.uk/code_signing_globalsign.html (reseller prices)
- Linux: https://docs.kernel.org/input/uinput.html
- macOS: https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice ,
  https://developer.apple.com/documentation/driverkit/requesting-entitlements-for-driverkit-development ,
  https://developer.apple.com/forums/thread/843327 , https://developer.apple.com/forums/thread/847383
- Android: https://developer.android.com/reference/android/bluetooth/BluetoothHidDevice
- Windows virtual audio: https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample

Not confirmed from a source and carried as UNVERIFIED above: VirtualPad's licence and release state; HidHide and
DsHidMini status; whether attestation drivers may ship in an own installer; the Azure Trusted Signing exclusion; the
Partner Center fee and approval time; a UMDF route for virtual gamepads; whether an XInput game sees a USB/IP Xbox 360
pad; whether `CreateSyntheticPointerDevice` works from a service; Apple's current approval criteria and timing; the
HVCI rules our own driver would have to meet; all time estimates.
