class MacPhoenix < Formula
  desc "Classic Macintosh emulator with web-based UI"
  homepage "https://github.com/sirmick/mac-phoenix"
  license "GPL-2.0-or-later"

  # When you cut a release:
  #   1. tools/make-source-tarball.sh                 (produces dist/mac-phoenix_<v>.tar.xz)
  #   2. gh release create vX.Y.Z dist/mac-phoenix_<v>.tar.xz
  #   3. shasum -a 256 dist/mac-phoenix_<v>.tar.xz
  #   4. fill in the url + sha256 below, drop the `head` block.
  #
  # url "https://github.com/sirmick/mac-phoenix/releases/download/v1.0.0/mac-phoenix_1.0.0.tar.xz"
  # sha256 "..."

  head do
    url "https://github.com/sirmick/mac-phoenix.git", branch: "main"
    # The submodules (libdatachannel, unicorn, nlohmann_json, lwip) must be
    # fetched too — Homebrew handles that with :using => :git when the URL
    # is a git repo. The release-tarball path above bundles them already.
  end

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build
  depends_on "rust" => :build      # net-bridge needs >= 1.80; brew rust is current
  depends_on "libvpx"
  depends_on "libyuv"
  depends_on "openh264"
  depends_on "openssl@3"
  depends_on "opus"
  depends_on "webp"

  def install
    args = %W[
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
      -DBUILD_TESTS=OFF
      -DBUILD_BRIDGE_AGENT=OFF
      -DCMAKE_INSTALL_PREFIX=#{prefix}
    ]

    system "cmake", "-B", "build", *args, *std_cmake_args
    system "cmake", "--build", "build", "--parallel", ENV.make_jobs.to_s
    system "cmake", "--install", "build"
  end

  def caveats
    <<~EOS
      MacPhoenix needs a Mac ROM and (optionally) a disk image to run.
      Neither is distributed with this formula. Place your files at:
        ~/storage/roms/quadra.rom
        ~/storage/images/macos-7.5.5.img

      Then start the emulator:
        mac-phoenix ~/storage/roms/quadra.rom
        open http://localhost:11000
    EOS
  end

  test do
    assert_match "Usage:", shell_output("#{bin}/mac-phoenix --help")
    assert_predicate bin/"net-bridge", :exist?
    assert_predicate share/"mac-phoenix/client/index.html", :exist?
  end
end
