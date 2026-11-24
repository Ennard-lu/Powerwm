# Protocol XML

Vendored protocol descriptions used by the in-tree scanner.

- `wayland.xml`: Wayland core protocol, from the Wayland project. MIT licensed;
  the copyright notice is embedded in the file.
- `xdg-shell.xml`: `xdg-shell` stable protocol, from wayland-protocols. MIT licensed;
  the copyright notice is embedded in the file.

The scanner generates C++ binding descriptors and message codecs at build time. No
`wayland-scanner` output or `libwayland-*` code is used.
