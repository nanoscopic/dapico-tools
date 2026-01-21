class DapicoTools < Formula
  desc "macOS-only USB tools for Raspberry Pi Pico devices"
  homepage "https://github.com/nanoscopic/dapico-tools"
  url "https://github.com/nanoscopic/dapico-tools/archive/refs/tags/0.1.2.tar.gz"
  sha256 "860fac5a0ca4e26f78bb894d9ceb18e82ce290c9268f5f62790f4ab564459835"
  version "0.1.2"
  depends_on "cmake" => :build
  depends_on :macos

  def install
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    system "#{bin}/dapico-reboot", "--help"
    system "#{bin}/dapico-load", "--help"
  end
end
