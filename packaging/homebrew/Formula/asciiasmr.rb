# Packaging template for a personal Homebrew tap.
# Before publishing, replace Xenostopical/ascii-asmr and the SHA256. Add a license field only
# after the project has a LICENSE file with that actual license.
class Asciiasmr < Formula
  desc "Terminal-native generative ambience playground"
  homepage "https://gg.elitehub.page.gd/"
  url "https://github.com/Xenostopical/ascii-asmr/archive/refs/tags/v1.1.0.tar.gz"
  sha256 "REPLACE_WITH_RELEASE_TARBALL_SHA256"
  license "MIT"

  depends_on "make" => :build

  def install
    system "make", "CC=#{ENV.cc}", "CFLAGS=#{ENV.cflags}"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_match "1.1.0", shell_output("#{bin}/asciiasmr --version")
  end
end
