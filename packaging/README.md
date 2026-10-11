# Packaging ASCII ASMR as `asciiasmr`

These files are starter templates, not publish-ready package definitions. Before publishing, replace every `OWNER`, `REPO`, checksum, maintainer and `TODO_LICENSE` field; add a real `LICENSE` file matching the SPDX license identifier you choose. The current project source does not contain a `LICENSE`, so do not distribute publicly under an assumed license.

## Build and locally install on Linux/macOS

```sh
make
./asmr
sudo make install
asciiasmr
```

The `install` target installs the program to `$(PREFIX)/bin/asciiasmr` (default prefix `/usr/local`). For staging a package, use `make install DESTDIR=/tmp/stage PREFIX=/usr`.

## Homebrew

1. Put this project in a public GitHub repository and create a stable release tag `v1.1.0` (or update the version and tag together).
2. Download the exact source archive URL configured in `Formula/asciiasmr.rb` and compute its digest with `shasum -a 256 <archive>`.
3. Replace `OWNER`, `REPO`, `REPLACE_WITH_RELEASE_TARBALL_SHA256`, and `TODO_LICENSE`.
4. Create a tap repository named `homebrew-asciiasmr`, with the formula at `Formula/asciiasmr.rb`, and push it to GitHub.
5. Users can install with one command using the tap-qualified name: `brew install YOUR_GITHUB_USERNAME/homebrew-asciiasmr/asciiasmr`. For an even shorter `brew install asciiasmr`, users must already have your tap added, or the formula must be accepted into Homebrew/core.

Test before publishing with `brew install --build-from-source ./Formula/asciiasmr.rb` from a local tap, then `brew audit --new --formula asciiasmr` where appropriate.

## Termux (`pkg`)

The `termux/asciiasmr/build.sh` file is a Termux package build recipe. It belongs in `packages/asciiasmr/build.sh` in a checkout of `termux/termux-packages` (for upstream submission), or in the equivalent location in your own package-build/repository setup. Replace the placeholders and test-build it with Termux's package builder. Termux package maintainers require a suitable license and package metadata, and packages must not require root during build/install.

`pkg install asciiasmr` works only after the package is published in a repository configured on the device. If the package is accepted into the official Termux repositories, users can install it normally. If you host your own repository, each user must first add/trust that repository; after that, `pkg install asciiasmr` works. Just uploading the `.tar.gz` source does not make `pkg` find it.

## Debian/Ubuntu Linux (`apt`)

`sudo install asciiasmr` is not a package-install command; `install` copies files and sets permissions. The package-manager equivalent is `sudo apt install asciiasmr`, but that works by name only after the package is available in an APT repository that the user's system trusts. As a local test, a built Debian package can be installed with `sudo apt install ./asciiasmr_VERSION_ARCH.deb`. To enable `sudo apt install asciiasmr` on other machines, create and sign an APT repository or submit the package to a distribution repository.

## Runtime notes

Audio playback is automatic when one of the supported players is installed (`paplay`, `aplay`, `pw-cat`, `play`, or `ffplay`). Without one, the visuals still run; `-n` disables audio explicitly.
