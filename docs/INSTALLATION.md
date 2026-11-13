# Installing V Media AI AutoFrame

## System requirements

| | Minimum | Recommended |
|---|---|---|
| OS | Windows 10 64-bit, or macOS 12 (Apple silicon / Intel) | Windows 11 64-bit, or macOS 14+ |
| OBS Studio | 30.0 (64-bit) | latest (tested with **33.0.1**) |
| CPU | 4 cores | 6 cores / 12 threads (e.g. Intel i7-10750H, AMD Ryzen 5 3600) |
| GPU | anything OBS runs on | optional: any DirectX 12 GPU for AI acceleration (GTX 1650 / 1660 and newer) |
| RAM | 8 GB | 16 GB |
| Disk | 70 MB | |

No RTX card, CUDA, cuDNN, TensorRT, internet connection, account or subscription is needed.
The Microsoft Visual C++ runtime the plugin needs is installed by OBS Studio itself.

## Windows - Option A - Installer (recommended)

1. Close OBS Studio.
2. Run `VMedia-AI-AutoFrame-Setup-x64.exe` and accept the Windows administrator prompt.
3. Keep the default folder:
   `C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe`
4. Start OBS Studio.

The installer never writes into the OBS program folder, so it cannot overwrite OBS files or
DLLs shared with other plugins, and OBS updates do not remove the plugin.
If OBS is running, the installer waits until you close it.

## Windows - Option B - ZIP (manual)

1. Close OBS Studio.
2. Extract the ZIP. It contains a folder `vmedia-ai-autoframe`.
3. Copy that folder into `C:\ProgramData\obs-studio\plugins\`
   (create the `plugins` folder if it does not exist). The result must be:

```
C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe\bin\64bit\vmedia-ai-autoframe.dll
C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe\data\...
```

4. Start OBS Studio.

**Portable OBS** (works on OBS 30-33): portable OBS does not look in ProgramData. Copy

* `vmedia-ai-autoframe\bin\64bit\vmedia-ai-autoframe.dll` (and `.pdb`) to
  `<portable OBS>\obs-plugins\64bit\`
* the *contents* of `vmedia-ai-autoframe\data\` to
  `<portable OBS>\data\obs-plugins\vmedia-ai-autoframe\`

## macOS (Apple silicon and Intel)

Requirements: macOS 12 Monterey or newer, OBS Studio 30 or newer. One download works on both
Apple silicon (M1-M4) and Intel Macs.

1. Quit OBS Studio.
2. Open `VMedia-AI-AutoFrame-<version>-macOS.dmg`.
3. Double-click **Install V Media AI AutoFrame.pkg** and follow the steps (it asks for your Mac
   password). It installs to
   `/Library/Application Support/obs-studio/plugins/vmedia-ai-autoframe.plugin`.
4. Start OBS. Right-click your camera source, then choose **Filters** > **+** > **V Media AI AutoFrame**.

The package is not notarized by Apple, so macOS may say it "can't be opened because Apple cannot
check it for malicious software". In that case:

* **macOS 15 Sequoia and newer:** open **System Settings > Privacy & Security**, scroll down,
  click **Open Anyway** next to the message about the package, and confirm.
* **macOS 12 to 14:** right-click (Control-click) the .pkg, choose **Open**, then **Open** again.

**Manual install (no admin password):** copy `Manual install/vmedia-ai-autoframe.plugin` from
the DMG into `~/Library/Application Support/obs-studio/plugins/` (Finder: Shift+Cmd+G). Create
the `plugins` folder if it does not exist. Then run this once in Terminal so OBS is allowed to
load it:

```bash
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/vmedia-ai-autoframe.plugin
```

**GPU on Mac:** *Use GPU* runs the AI through Core ML (Apple GPU / Neural Engine). If Core ML is
not available, the plugin says so in the status line and uses the CPU.

**Uninstall on Mac:** quit OBS and delete `vmedia-ai-autoframe.plugin` from the plugins folder above.

## Check that it loaded

`Help > Log Files > View Current Log` should contain:

```
[vmedia-autoframe] V Media AI AutoFrame 1.1.2 loaded (built for libobs 30.0.0)
```

Then right-click a source > **Filters** > **+** (Effect Filters) - **V Media AI AutoFrame** is in
the list.

## Verify the download (optional)

```powershell
Get-FileHash .\VMedia-AI-AutoFrame-Setup-x64.exe -Algorithm SHA256
```

Compare with `SHA256SUMS.txt` from the release.

## Updating

Close OBS and run the new installer (or replace the folder). Settings are stored in your OBS
scene collection and are kept.

## Uninstalling

*Settings > Apps > Installed apps > V Media AI AutoFrame (OBS plugin) > Uninstall*, or delete
`C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe` while OBS is closed. Scenes that used
the filter will show it as missing; remove it from the source's filter list.
