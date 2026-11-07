# NixOS has no /bin/bash or /usr/include, which Mu-Silicium's build expects: its setup_env.sh packages, in a standard layout.
#
#   $(nix-build --no-out-link tools/mu-silicium-fhs.nix)/bin/mu-silicium -c '
#     python3 -m venv build/mu-venv && build/mu-venv/bin/pip install -r Mu-Silicium/pip-requirements.txt &&
#     . build/mu-venv/bin/activate && cd Mu-Silicium && python3 build_uefi.py -d beyond1lte -r RELEASE'
{ pkgs ? import <nixpkgs> { } }:
pkgs.buildFHSEnv {
  name = "mu-silicium";
  targetPkgs = p: with p; [
    bash coreutils curl file findutils gawk git gnugrep gnumake gnused gnutar gzip which
    # the unwrapped clang: Nix's wrapper adds --gcc-toolchain, which -Werror refuses on a UEFI target
    llvmPackages.clang-unwrapped gcc lld llvm mono nasm
    gettext libuuid ncurses openssl python3 zlib
  ];
  # ClangToolChain.py otherwise looks for lld-link beside the clang it finds
  profile = "export CLANG_BIN=/usr/bin/";
  runScript = "bash";
}
