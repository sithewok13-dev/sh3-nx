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
