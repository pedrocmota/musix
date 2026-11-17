# Musix

The karaoke effect from the Realtek Audio Console (voice cancel and pitch
shift) for any audio output on Windows. It is the same processing Realtek
ships, transcribed from `RltkAPOU642.dll` and packaged as a standalone APO.

## Usage

Open the app, go to the gear icon and click Install on the output you use. It
asks for administrator rights and audio drops out for a second. Then turn the
effect on from the main screen.

- With a vendor APO (Realtek) Musix is chained alongside it. With the generic
  Windows APO (HDMI, USB, Bluetooth) it takes the slot; uninstalling restores
  the original.
- It does not apply to exclusive-mode streams (WASAPI exclusive, ASIO) or when
  "Audio enhancements" are turned off in Windows. Bluetooth works on the music
  profile (A2DP) only.
- Reinstalling the device driver wipes the entry: install again.
- If Windows ends up with no audio after a failed install, run
  `scripts\restore-audio.ps1` as administrator to restart the services.

## Building

Rust (MSVC toolchain), Node 20+ and the Visual Studio Build Tools with the
Windows SDK.

```bat
scripts\build-apo.cmd
npm install
npm run tauri build
```

The first command builds the APO and drops the DLL into `src-tauri\resources`;
the last one produces `Musix.exe` and the installer under
`src-tauri\target\release\bundle\nsis\`. `build-apo.cmd` also builds, in
`apo\build`, `apo_host` (exercises the DLL outside `audiodg`) and `musix_cli`
(applies the effect to a WAV; `apo\tests\ref_f32.py` checks its output against
a numpy implementation).

## Layout

```
apo/        the effect (C/C++): dsp/, src/ (COM), cli/, tests/
src/        React UI
src-tauri/  Rust backend and Tauri config
scripts/    build-apo.cmd, restore-audio.ps1
```

The app runs unprivileged and talks to the APO through
`%ProgramData%\Musix\musix.cfg` (polled every 150 ms) and `status.txt`.
Install relaunches the executable elevated (`Musix.exe --elevated install
{guid}`), which registers the DLL, writes Musix into the endpoint's MFX slot
under `MMDevices\Audio\Render` with a backup in `backup-{guid}.json`, and
restarts audio. The format matches the PowerShell scripts from the earlier
project. `--list` prints the outputs as JSON.
