# Vendored C and C++ dependencies

These archives make a copied source tree self-contained and offline-buildable.
CMake verifies each SHA-256 checksum before extracting a missing dependency into
`third-part/`; extracted directories are intentionally ignored by Git.

| Archive | Upstream revision | SHA-256 |
| --- | --- | --- |
| `googletest-1.17.0.tar.xz` | `52eb8108c5bdec04579160ae17225d66034bd723` | `98463668a2c0333472f8d6ef834fae861f4970ef9e4860aff6b2936eb3b72f43` |
| `cpp-httplib-0.51.0-vna1.tar.xz` | `d66d9a95997d51a8ba9822a611d1267757741535` | `edea744ba00da1c3f14aea9fde895eed02edbef37af26bacdf3a09cafe21b00e` |
| `nlohmann-json-3.12.0.tar.xz` | `55f93686c01528224f448c19128836e7df245f72` | `c67a10f5ac8fa59449dfba8e651bdb3dfc8fbd4da6f3119822a13a3af4e538ae` |
| `scpi-parser-2.3.tar.xz` | `361fe25cb1fe4cacb4f7494944c5d4084eb07f61` | `70293372e51fc064e12fcabe9cbef7311c3556a444c800ea4fe051bf697e6d53` |
| `spdlog-1.17.0.tar.xz` | `79524ddd08a4ec981b7fea76afd08ee05f83755d` | `266939b62e6afbe10f871ae40784e28ae81d90209315861913ff3a6baac065ea` |

The cpp-httplib snapshot includes the project `close_now` compatibility patch,
so configuring and building never requires Git or a patch utility.

The scpi-parser snapshot is upstream libscpi v2.3 under BSD-2-Clause. Its
complete `LICENSE` is retained in the archive. Production Windows builds link
the library through `vna::scpi`; the portable release installs the matching
BSD-2-Clause text from `packaging/licenses/`.
