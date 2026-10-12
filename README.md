# Gerald the Frog (3DS)

A tiny Tamagotchi-style pixel-art pet for the Nintendo 3DS.

Feed him flies, play with him, keep him clean and let him sleep.
Touch the buttons on the bottom screen, or use **A / B / X / Y**. **START** saves and exits.
Saves live in `sdmc:/3ds/gerald/save.bin` and Gerald slowly gets needy while the game is closed (he never dies).

## Build
1. Create a GitHub repo and push this folder to it.
2. Open the **Actions** tab, run **Build Gerald CIA** (it also runs on every push).
3. Download the `gerald` artifact: it contains `gerald.cia` (install with FBI) and `gerald.3dsx` (Homebrew Launcher).

## Sound needs DSP firmware
Install the DSP firmware once with Homebrew Launcher's **DSP1** app (or dump it with GodMode9) so `sdmc:/3ds/dspfirm.cdc` exists.
Without it Gerald still runs, just silently.

## Customise
- `tools/gen_assets.py` makes the icon, banner, banner jingle and sound effects. Edit and re-run it.
- Sprites live as text maps at the top of `source/main.c`.
- `APP_UNIQUE_ID` / `APP_PRODUCT_CODE` are set in `.github/workflows/build.yml`. Change the ID if it clashes with another title.
