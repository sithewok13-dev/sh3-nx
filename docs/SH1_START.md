# Silent Hill 1 on iPhone: starting notes

This branch starts the Silent Hill 1 work. It's based on the SH3 `ios` branch, which holds the SH2 iOS layer.

## Earlier sessions to look back on

Open these in the Claude app, or have a session read them with the
`list_events` tool (claude-code-remote) using the session ID.

| Game | Session | ID |
|---|---|---|
| Silent Hill 2 | "Silent Hill 2 iPhone emulation" | `session_01XqQAjtSmHM9jo7GYGriYUK` |
| Silent Hill 3 | "Silent Hill 3 on iPhone" | `session_01XqQAjrgcBUTRyeSbHuNDik` |

- https://claude.ai/code/session_01XqQAjtSmHM9jo7GYGriYUK
- https://claude.ai/code/session_01XqQAjrgcBUTRyeSbHuNDik

To find just the final replies, call `list_events` with `kinds: ["result"]` and `limit: 100`.
Then page back using `before_id`.

## Where things are

- **SH2:** code is in `sithewok13-dev/sh2-nx` (branch `ios`). Builds are in the private
  `sithewok13-dev/sh2-ios-build`. The latest is build 104 (1.02), in release `app`.
- **SH3:** code is in `sithewok13-dev/sh3-nx` (branch `ios`). Builds go in the private
  `sithewok13-dev/sh3-ios-build`, which has the secrets `SH3_ISO_URL` and `BUILD_TOKEN`.
  The plan is `jk-files/docs/plans/sh3-ios.md`.
- **Shared docs:** in `jk-files`: `docs/HOUSE_STYLE.md` and `docs/FREE_MAC_BUILDS.md`.
  Each game gets its own token and its own private build repo.

## Status of the SH3 session (1 Nov 2026)

It hasn't started yet. The permission check refused to attach `jk-files` and `sh3-ios-build`,
so the owner has to approve them in that session.

## Big difference for SH1

SH2 and SH3 work by recompiling the **Windows PC** `.exe` to C, then adding a Windows layer
(D3D8, DirectSound...) on SDL2/GLES. **Silent Hill 1 never had a PC release. It's PS1 only**,
so that method doesn't carry over directly. Options to check first:

1. Build on the community SH1 decompilation, if it's far enough along.
2. Recompile the PS1 MIPS code statically, which also means replacing the PS1 hardware
   (GPU, SPU, CD).
3. Fall back to a PS1 emulator core.

The iOS parts of SH2 and SH3 can be reused whichever way it goes: touch controls, the GLES
renderer, the launcher, and the free Mac builds on CI.

## Setup to copy from SH3 (owner)

1. Make a private repo `sh1-ios-build`.
2. Make a `sh1 builds` key, then add it as `BUILD_TOKEN` to that repo and to the code repo.
3. Add the SH1 disc image link as `SH1_ISO_URL`.

## Research findings (1 Nov 2026)

### The short answer

Someone has already done the hard part. A community port of Silent Hill 1 runs natively
(no emulator) on PC, Mac, Android and **iPhone**. It's built on the community decompilation.
We should build on that port, not start from scratch.

### The three options

**1. Community decompilation + native port (recommended)**

- Decompilation: https://github.com/shdecompilations/silent-hill-decomp (was `Vatuu/silent-hill-decomp`).
  It targets the US 1.1 disc (`SLUS-00707`). All 43 map overlays are matched. Its own README
  says it is *not* port-ready and won't make a port itself. Progress is on decomp.dev
  (this session's network couldn't open that site).
- Native port: https://github.com/SlickAmogus/silent-hill-decomp, beta, very active (builds on
  31 Oct 2026). It's playable start to finish, with all maps, saves, widescreen, high res and
  mods. It uses SDL2, OpenAL and PsyCross (a layer that stands in for the PS1 SDK).
  The port code is GPL-3.0. The decompiled game code has no licence.
- Its "realtime" release already has `SilentHill-ios-unsigned.ipa`, an Apple-silicon Mac
  build and an Android APK: https://github.com/SlickAmogus/silent-hill-decomp/releases/tag/realtime
- Caveats: their own notes say the iOS build was **never tested on a real iPhone**. The touch
  controls are mostly for menus. Mac/Linux are still "work in progress".
- No game data is included. The player supplies their own BIN of the disc.

**2. PS1 static recompilation**

- PSXRecomp (https://github.com/RetroPortingToolKit/psxrecomp) is the SH2/SH3 method
  for PS1. It's in beta and has shipped Tomba!, Tomba! 2, Ape Escape and Pepsiman. It's
  Windows-first, and nobody has run Silent Hill through it. SH1 loads lots of code overlays
  per map, which is the hard case for it.
- Not worth it now that a native port exists.

**3. PS1 emulator core**

- This works today (for example RetroArch with a PS1 core, or Provenance). But iOS doesn't
  allow fast emulation (JIT) for apps installed like ours, so it would be slower. It also
  gives none of the native port's benefits (high res, widescreen, mods).
- Keep it as a fallback only.

### Plan if the owner agrees

1. **Quick test, no building:** the owner installs their unsigned IPA in Feather with their own
   SH1 BIN. This tells us if it runs on a real iPhone at all.
2. **Our own build:** fork the port into a private `sh1-ios-build` (the SH2/SH3 pattern).
   Then add our iOS layer: SH2's touch controls, launcher and game-file import, set up per
   HOUSE_STYLE. Build it free on CI per FREE_MAC_BUILDS. GPL-3.0 means our port changes stay
   open source, but game files are never committed.

### What the owner needs to provide

- A BIN dump of **Silent Hill (USA), SLUS-00707**. PAL and Japanese discs also work in the
  port, but the US disc is the one tested. Use a private link as `SH1_ISO_URL`, never a
  public repo.
- The usual setup: a private `sh1-ios-build` repo and a `BUILD_TOKEN` (see above).
- Session approvals: allow `sithewok13-dev/jk-files` and `SlickAmogus/silent-hill-decomp`
  for the next session. This session was refused both, so HOUSE_STYLE.md and
  FREE_MAC_BUILDS.md **have not been read yet**. The next session must read them first.
