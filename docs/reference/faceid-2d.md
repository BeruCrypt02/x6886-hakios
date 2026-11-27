# 2D Face ID architecture for the X6886 port

Design document, 24 Nov 2026. Offline only. No code was written, compiled, or
signed. Hardware facts come from `docs/X6886-DRIVER-AUDIT.md`; framework
symbols come from `refs/theos-sdks/iPhoneOS16.5.sdk`.

## What this is and what it is not

This is a camera-based 2D face matcher. It captures frames from the X6886 front
camera, finds a face with Vision, checks for blink and head turn, and compares
the result against a locally stored template. On a match it calls SpringBoard
to dismiss the lock screen or completes an `LAContext` auth request.

Apple Face ID uses a TrueDepth projector, an infrared camera, and a Secure
Enclave. This device has none of that hardware. The X6886 front camera is a
GalaxyCore gc13a0ff, 13.1 MP, f/2.0 (`X6886-specs.txt:148-174`;
`fdt.dts:1913,4848-4856`). The dump has no depth sensor node and no Secure
Enclave.

| Capability | Apple Face ID | This design |
| --- | --- | --- |
| Depth map | Projected IR dots | None. RGB frames only |
| Template storage | Secure Enclave | File in the XNU userspace root, POSIX permissions only |
| Liveness | Hardware-backed depth and IR | Blink and head-turn heuristics on RGB frames |
| Unlock authority | Secure Enclave signature to SpringBoard | Our process calls SpringBoard after its own match |
| App Store / Apple ID auth | Accepted | Rejected. No Enclave attestation |
| Payment and keychain gates | Accepted | Rejected. Do not route them through this matcher |
| Photo or video attack | Resisted by depth | Not resisted. A still photo can pass if liveness is fooled |
| Use case | Security boundary | Convenience unlock on a jailbroken development port |

Treat any successful match as a convenience gesture. Keep the passcode as the
real security boundary.

## Hardware prerequisite

The matcher cannot run until the XNU front camera driver works. That driver
does not exist yet.

| Dependency | Stock evidence | Status |
| --- | --- | --- |
| gc13a0ff on I2C 4 at 0x1a | `fdt.dts:4848-4856` | No XNU driver |
| CSI receive and DMA | `mipi_tx_config@11f60000`, `fdt.dts:6515-6520` | No XNU driver |
| Camera rails | `vcama1`, `vcamd`, `vcamio` at `fdt.dts:1907-1911,3642-3643,3703-3704` | No XNU driver |
| 30 fps front video | `X6886-specs.txt:172` | Unverified in XNU |
| Panel for animation | BOE nt37706a, `fdt.dts:6538-6555` | LK scanout only |

## Pipeline

| Stage | Owner | API or mechanism | SDK evidence |
| --- | --- | --- | --- |
| 1. Capture | Helper daemon or tweak process | `AVCaptureSession`, front `AVCaptureDevice`, `AVCaptureVideoDataOutput` | `AVCaptureSession.h:487` (`startRunning`), `:497` (`stopRunning`); `AVCaptureVideoDataOutput.h:57` (`setSampleBufferDelegate:queue:`) |
| 2. Frame delivery | Serial dispatch queue | `captureOutput:didOutputSampleBuffer:fromConnection:` | `AVCaptureVideoDataOutput.h:238`; queue must be serial to avoid reordered frames |
| 3. Face detect | Vision | `VNImageRequestHandler` with `VNDetectFaceRectanglesRequest`, revision 3 | `VNDetectFaceRectanglesRequest.h:21,26,44`; `:18` documents `VNFaceObservation` output |
| 4. Pose and confidence | Vision | `VNFaceObservation.confidence`, `.roll`, `.yaw`, `.pitch` | `VNObservation.h:51,123,128,133` |
| 5. Landmarks | Vision | `VNDetectFaceLandmarksRequest`, revision 3, fed from stage 3 | `VNDetectFaceLandmarksRequest.h:30,33,46,58` |
| 6. Liveness | Our matcher | Eye aspect ratio over time from landmarks; yaw delta over time | Logic is ours. Vision supplies the points |
| 7. Template match | Our matcher | Normalized landmark geometry plus a compact appearance descriptor | No Apple API. See `Matching` below |
| 8. Decision | Our matcher | Majority vote over k frames with a timeout | Thresholds below are starting points |
| 9. Unlock / auth | Tweak | SpringBoard or `LAContext` reply | See `Theos design` |

`VNSequenceFaceObservation` is not declared in the local iPhoneOS16.5 SDK. A
search of `refs/theos-sdks/iPhoneOS16.5.sdk` found no declaration. Use
per-frame `VNFaceObservation` and keep sequence state in our own code. Do not
write code against that class until a newer SDK is supplied.

## Enrollment

| Step | Action | Accept condition |
| --- | --- | --- |
| 1 | Start capture session on the front camera | `startRunning` returns and one frame arrives |
| 2 | Run stage 3 and discard frames with `confidence < 0.85` | At least 20 usable frames |
| 3 | Reject profiles with `abs(yaw) > 0.35` rad or `abs(pitch) > 0.35` rad | Face roughly frontal |
| 4 | Run stage 5 and require both eye regions present | Landmarks valid |
| 5 | Normalize landmark geometry by interocular distance and rotate by `-roll` | Scale and roll invariant vector |
| 6 | Keep one descriptor per frame; drop outliers by median distance | Template = median vector plus tolerance |
| 7 | Write template to a root-owned file, mode 0600 | Store a version and a checksum |
| 8 | Stop the session | `stopRunning` called and `isRunning == NO` |

Thresholds in steps 2 and 3 are starting points. They need measured false
accept and false reject rates on this device. Enrollment files live in the
XNU userspace root under a dedicated directory with restrictive permissions.
Do not treat Keychain as hardware protection on this port. There is no Secure
Enclave to back it.

## Real-time matching

| Item | Rule |
| --- | --- |
| Frame rate | 30 fps target; drop late frames via `alwaysDiscardsLateVideoFrames = YES` (`AVCaptureVideoDataOutput.h:190`) |
| Work queue | One serial queue for capture, Vision, and match. Vision calls stay off the main thread |
| Candidate gate | `VNFaceObservation.confidence >= 0.8`; one face in frame |
| Pose gate | `abs(yaw) < 0.4` rad, `abs(pitch) < 0.4` rad during the match decision |
| Match score | Distance between the live normalized vector and the enrolled median, below the stored tolerance |
| Temporal vote | 5 of 7 consecutive frames must pass |
| Timeout | 5 s from auth start, then fall back to passcode |
| Concurrency | One matcher at a time. A second auth request cancels the first |

The descriptor in step 5 of enrollment is geometry first. Adding an appearance
descriptor raises accuracy and raises false accepts under makeup, glasses, and
beard changes. Start with geometry, measure, then decide.

## Liveness

RGB frames carry no depth signal. Liveness uses motion over time.

| Check | Signal | Starting rule |
| --- | --- | --- |
| Blink | Eye landmark vertical distance divided by eye width, tracked over frames | Drop below 45 percent of the open value, then recover, within 1.5 s |
| Head turn | `VNFaceObservation.yaw` delta (`VNObservation.h:128`) | At least 0.25 rad in one direction, then return |
| Order | Both checks | Require blink and turn in one 5 s window |
| Replay | Frame timing and pose continuity | Reject jumps over 0.3 rad between adjacent frames |

A prerecorded video can reproduce blink and turn. This design lowers the odds
of a casual photo attack. It does not stop a determined replay. Say so in the
UI and in any product copy.

## Theos design

Toolchain state in this environment: `llvm-20`, `clang-20`, `ldid`, and
`theos` are not on `PATH`. `refs/theos-sdks/iPhoneOS16.5.sdk` is present and
contains patched private-symbol `.tbd` files (`refs/theos-sdks/README.md:1-6`).
No build or sign step was attempted.

| Item | Value |
| --- | --- |
| SDK | `refs/theos-sdks/iPhoneOS16.5.sdk` copied to `$THEOS/sdks/` |
| Compiler | `llvm-20` once installed; target `arm64-apple-ios` |
| Signer | `ldid` with entitlements taken from the running SpringBoard on device. Exact entitlement keys are not verifiable offline |
| Layout | One tweak dylib injected into `SpringBoard`, one helper daemon for capture and match |
| IPC | Darwin notification for start and stop; a mach or XPC channel for the result |
| Private headers | None for SpringBoard in the SDK. Use `%c(Class)` and runtime selector lookup |

### Hook points

| Target | Selector or symbol | Evidence | Use |
| --- | --- | --- | --- |
| `LAContext` | `evaluatePolicy:localizedReason:reply:` | `LAContext.h:171-172` | App auth: run our matcher, call `reply(YES, nil)` on match, otherwise call the original |
| `LAContext` | `canEvaluatePolicy:error:` | `LAContext.h:114` | Report capability only when our template exists |
| `LAContext` | `biometryType` | `LAContext.h:385` declares `LABiometryTypeFaceID` for macOS only | Do not rely on the OS reporting Face ID. Use our own capability flag |
| `SBLockScreenManager` | class | `SpringBoard.tbd:2100` | Lock screen unlock |
| `SBLockScreenManager` | ivars `_biometricAuthenticationCoordinator`, `_isUILocked` | `SpringBoard.tbd:8520,8529` | State checks only. Ivars are not method declarations |
| `SBLockScreenBiometricAuthenticationCoordinator` | ivars `_isAuthenticated`, `_pendingUnlockRequest` | `SpringBoard.tbd:8495,8501` | Confirms SpringBoard has a biometric coordinator path |
| `LAUIPearlGlyphView` | class | `LocalAuthenticationPrivateUI.tbd:19` | Standard Face ID pearl glyph animation |
| `SBUIFaceIDCameraGlyphView`, `SBUIFaceIDCoachingView` | classes | `SpringBoardUIServices.tbd:302-303` | Lock screen Face ID glyph and coaching view |
| `SBUIPasscodeBiometricAuthenticationView` | class | `SpringBoardUIServices.tbd:314` | Combined passcode and biometric container |

SpringBoard's `.tbd` lists classes and ivars, not method declarations. The
selectors `+sharedInstance`, `-isUILocked`, and `-unlockFromSource:withDuration:`
are community-known candidates, not facts from this SDK. Confirm them on
device with `class_copyMethodList` before writing `%hook` bodies. A wrong
selector produces a silent no-op or an exception at unlock time. Do not ship
an unverified hook into SpringBoard.

### Unlock flow

1. SpringBoard posts a lock-screen biometric attempt notification.
2. The tweak asks the daemon to start capture. It shows the pearl animation.
3. The daemon returns `match`, `no-match`, `liveness-fail`, or `low-light`.
4. On `match`, the tweak calls the confirmed SpringBoard unlock selector and
   plays the success transition.
5. On any other result, the tweak shows the failure glyph and leaves the
   passcode path untouched.
6. The daemon stops capture in every branch, including timeout and error.

### App auth flow

1. Hook `evaluatePolicy:localizedReason:reply:`.
2. For `LAPolicyDeviceOwnerAuthenticationWithBiometrics`, start the matcher.
3. On match, call the original `reply` block with `YES` and `nil`.
4. On failure, call the original implementation so iOS runs its normal error
   and passcode path.
5. Never suppress the passcode fallback.

The animation is a UI copy. It carries no Enclave signature. Apps that ask the
system for hardware-backed biometrics receive a normal `LAContext` result, so
this design can fool a UI check. It cannot satisfy an app that verifies
Enclave attestation or uses `LARight`/`LAPersistedRight` with a hardware key
(`LocalAuthentication.framework/Headers/` contains `LARight.h` and
`LAPersistedRight.h`). State that limitation to anyone using the tweak.

## Edge cases

| Case | Handling | Evidence or reason |
| --- | --- | --- |
| Low light | Compute mean luma from the pixel buffer. Below threshold, cancel and show passcode | No IR illuminator on this board |
| Screen off or locked | Stop session on `com.apple.springboard.lockcomplete` and display sleep. Zero camera power | Camera use in background is prohibited (`AVCaptureSession.h:76-77`) |
| Camera app opens | Observe `AVCaptureSessionWasInterruptedNotification` with `VideoDeviceInUseByAnotherClient` and release the device (`AVCaptureSession.h:68,80,90`) | Prevents a stuck camera that crashes Camera.app |
| Interruption ends | Do not auto-restart. Wait for the next auth request | Keeps idle battery draw at zero |
| App backgrounding | `stopRunning` on `UIApplicationDidEnterBackground` | `AVCaptureSession.h:77` |
| Second auth request | Cancel and replace the active matcher | One camera, one decision |
| Multiple faces | Reject when Vision returns more than one observation | Ambiguous identity |
| Glasses, mask, beard | Raise tolerance only after collecting measured false-accept data | Geometry shifts between sessions |
| Timeout | 5 s, stop capture, passcode fallback | Bounded latency |
| Enrollment corrupt | Verify checksum; on mismatch delete template and re-enroll | No Enclave integrity protection |
| Match while device is charging | No special path | Charger drivers are separate (`X6886-DRIVER-AUDIT.md`, battery section) |
| Haptics | Use the X6886 `regulator-vibrator` node, not an AW86927 path | `fdt.dts:2333-2340` |
| Animation during low battery | Skip glyph animation and fall back to passcode | Keep the unlock path short |

## Power state machine

| State | Session | Camera | Match loop |
| --- | --- | --- | --- |
| Idle, screen on, no auth | not created | off | stopped |
| Auth requested | `startRunning` | on | running |
| Match success | `stopRunning` | off | stopped |
| Timeout or failure | `stopRunning` | off | stopped |
| Lock or display sleep | `stopRunning` | off | stopped |
| Camera interrupted by another app | `stopRunning`, release device | off | stopped |
| Enrollment | `startRunning` for the enrollment window only | on | running |

Zero battery drain while the screen is off depends on the lock and display
sleep notifications arriving. If they are missed, a watchdog that stops the
session after 10 s of no auth activity is the backstop.

## Verification plan

1. Static: compile the tweak against `iPhoneOS16.5.sdk` with `llvm-20` once the
   toolchain is installed. No build in this session.
2. Runtime introspection: enumerate `SBLockScreenManager` methods on device.
   Record the three selectors used by the tweak.
3. Driver gate: confirm the front camera delivers frames in XNU before running
   any matcher code. Stop if it does not.
4. Enrollment test: collect 20 frames, print each descriptor, check spread.
5. Liveness test: blink only, turn only, both, neither. Record accept and reject.
6. Resource test: open Camera.app during auth, then close it. Camera.app must
   open and capture normally afterwards.
7. Power test: leave the phone locked for 30 minutes with the tweak loaded.
   Camera must stay off and battery draw must match a baseline without the
   tweak.
8. Fallback test: cover the lens, confirm passcode path appears within 5 s.
9. Limitation test: present a printed photo. Record the result. Do not claim
   photo resistance unless it fails.

## Open decisions

- Descriptor format and template size.
- Threshold values after measurement on this device.
- Which SpringBoard unlock selector survives iOS 16.5 on this port.
- Entitlements for a SpringBoard-injected dylib on this XNU build.
- Whether `LAContext` hooks can present the pearl view without an Enclave,
  or whether the tweak must draw its own copy of the animation.
- Storage location for the template inside the APFS userspace root.
