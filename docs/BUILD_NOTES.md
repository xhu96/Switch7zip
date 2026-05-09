# Build notes

## Dependencies

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-libarchive
```

## Build

```sh
make clean
make
```

Expected outputs:

```text
Switch7zip.elf
Switch7zip.nacp
Switch7zip.nro
```

Only the `.nro` is needed on the SD card:

```text
sdmc:/switch/Switch7zip/Switch7zip.nro
```

## Notes for reviewers

The project should build warning-free with the current devkitA64/libnx toolchain used during the cleanup pass. If a future devkitPro update introduces warnings, please include the full build log in an issue.
