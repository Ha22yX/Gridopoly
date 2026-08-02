# Gridopoly Player Console Demo

Arduino firmware skeleton for the 480 x 480 Viewe UEDX48480021-MD80ET ESP32-S3 round display.

## Controls

- Rotate: move the shared focus.
- Knob A/B is reversed at the hardware-input boundary for the installed orientation.
- Short press: activate.
- Hold 800 ms: back.
- Hold 3 s: open Demo Lab from Home, or return Home from another page.
- Dangerous modal: hold 1.2 s to confirm.
- Touch: direct selection; focus and page state stay synchronized with the knob.

The stand rotates its mating pattern clockwise by 60 degrees so M4 alignment
pin 3 sits exactly at six o'clock. Firmware rotation remains 0 degrees.

```powershell
.\tools\compile.ps1 -SelfTest
.\tools\compile.ps1
.\tools\upload.ps1
```

`compile.ps1` verifies the Chinese glyph subset and the focus-layout invariant before
invoking Arduino CLI. The home-wheel button centers must remain `159 / 240 / 321`,
and focus animation must refresh LVGL layout before reading the target coordinate.
