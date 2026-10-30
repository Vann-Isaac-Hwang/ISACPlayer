# Arch Linux Local Package

This recipe builds a pacman package from the surrounding full source checkout.
It is intended for installing and testing ISACPlayer on the current Arch Linux
system; it is not yet an AUR recipe because it does not fetch a versioned source
archive.

From the repository root:

```sh
cd packaging/arch
makepkg -si
```

The package is registered with pacman. Remove it with:

```sh
sudo pacman -Rns isacplayer
```

The package installs the executable and desktop integration under `/usr`,
DSP and format modules under `/usr/lib/isacplayer/`, and license files under
`/usr/share/licenses/isacplayer/`. Per-user settings, library database,
playback cache, and custom DSP presets are kept outside the package and remain
after uninstall.

Before publishing to the AUR, create a version tag, change the recipe to fetch
that immutable source archive, pin its checksum, generate `.SRCINFO`, and
review all third-party license notices.
