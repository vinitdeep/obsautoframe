# Installing V Media AI AutoFrame

## System requirements

| | Minimum | Recommended |
|---|---|---|
| OS | Windows 10 64-bit | Windows 11 64-bit |
| OBS Studio | 30.0 (64-bit) | latest (tested with **33.0.1**) |
| CPU | 4 cores | 6 cores / 12 threads (e.g. Intel i7-10750H, AMD Ryzen 5 3600) |
| GPU | anything OBS runs on | optional: any DirectX 12 GPU for AI acceleration (GTX 1650 / 1660 and newer) |
| RAM | 8 GB | 16 GB |
| Disk | 70 MB | |

No RTX card, CUDA, cuDNN, TensorRT, internet connection, account or subscription is needed.
The Microsoft Visual C++ runtime the plugin needs is installed by OBS Studio itself.

## Option A - Installer (recommended)

1. Close OBS Studio.
2. Run `VMedia-AI-AutoFrame-Setup-x64.exe` and accept the Windows administrator prompt.
3. Keep the default folder:
   `C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe`
4. Start OBS Studio.

The installer never writes into the OBS program folder, so it cannot overwrite OBS files or
DLLs shared with other plugins, and OBS updates do not remove the plugin.
If OBS is running, the installer waits until you close it.

## Option B - ZIP (manual)

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

## Check that it loaded

`Help > Log Files > View Current Log` should contain:

```
[vmedia-autoframe] V Media AI AutoFrame 1.1.1 loaded (built for libobs 30.0.0)
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
