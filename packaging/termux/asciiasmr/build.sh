# Template for termux/termux-packages or your own Termux repository.
# Replace Xenostopical/ascii-asmr, SHA256, maintainer, and MIT before building.
TERMUX_PKG_HOMEPAGE=https://gg.elitehub.page.gd/
TERMUX_PKG_DESCRIPTION="Terminal-native generative ambience playground"
TERMUX_PKG_LICENSE=MIT
TERMUX_PKG_MAINTAINER="xenostopic-cyber <ubereatsgoat99@gmail.com>"
TERMUX_PKG_VERSION=1.1.0
TERMUX_PKG_SRCURL=https://github.com/Xenostopical/ascii-asmr/archive/refs/tags/v${TERMUX_PKG_VERSION}.tar.gz
TERMUX_PKG_SHA256=REPLACE_WITH_RELEASE_TARBALL_SHA256

termux_step_make_install() {
    make
    install -Dm755 asmr "$TERMUX_PREFIX/bin/asciiasmr"
}
