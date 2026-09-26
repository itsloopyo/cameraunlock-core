The mod reads its settings from `CameraUnlock.ini` in the game folder, at one of these paths depending on the store the game came from:

- `CameraUnlock.ini`
- `Game\Binaries\Win64\CameraUnlock.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- A hotkey set to Ctrl, Shift or Alt on its own. It fired at the start of every Ctrl+Shift chord, so it is left unbound, and the hotkey keeps its Ctrl+Shift chord where it has one.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below.

With every setting at its default, the file reads:

```ini
; Fixture Game head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=0.0

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true
; How far, in metres, leaning sideways moves the view.
; The fixture's own wording.
PositionLimitX=0.3
; Which of the game's collision channels the wall check tests against.
; CollisionChannel=3
; Milliseconds before a lean starts, and the metres its wall trace reaches.
LeanDelayMs=50
LeanTraceLength=1.0

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G

[Camera]
; ControlRotation or UpdateCamera (decoupled).
Mode=UpdateCamera
; Near clip distance, in the game's units.
NearClip=0.1
; Engine values. The commented lines show the built-in values.
; Delete the ; to pin your own.
; UpdateCameraSlot=196
; PovOffset=0x404
; CleanCameraReader=0x1402A0B10
; Offsets the camera hook patches.
HookOffsets=0x10, 0x2A
; Return addresses whose aim is left alone. Empty for none.
AimCallers=
; Widgets that follow the head.
WidgetNames=Crosshair, Compass
; Marker colour: red, green, blue and opacity, each 0 to 1.
MarkerColor=1.0, 0.5, 0.0, 1.0

[Logging]
; Log file, beside the game's executable.
LogPath=HeadTracking.log
; true: write the log.
; WriteLog=false
; Reads this file again.
ReloadKey=F10
```
