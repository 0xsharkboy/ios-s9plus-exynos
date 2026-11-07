# NixOS has no /bin/bash or /usr/bin/clang, which the kernel's build expects (xnu-deps/host/bin/xnu-clang), and theos's toolchain is
# built for a conventional Linux: xnu-deps/setup.sh's tools, in a standard layout. Run from core/:
#
#   $(nix-build --no-out-link tools/xnu-fhs.nix)/bin/xnu-fhs -c 'bash tools/setup-theos.sh && make kernel'
{ pkgs ? import <nixpkgs> { } }:
pkgs.buildFHSEnv {
  name = "xnu-fhs";
  targetPkgs = p: with p; [
    bash coreutils curl file findutils gawk git gnugrep gnumake gnused gnutar gzip patchelf rsync which xz
    # unwrapped, as for tools/mu-silicium-fhs.nix: Nix's wrapper adds flags for its own target
    llvmPackages.clang-unwrapped gcc lld llvm
    autoconf automake bc bison flex gnum4 libtool perl python3
    libuuid ncurses stdenv.cc.cc.lib zlib
  ];
  # theos in the repository's ignored xnu-deps/, not in the home directory
  profile = ''export THEOS=''${THEOS:-$PWD/xnu-deps/theos}'';
  runScript = "bash";
}
