# BIOS metadata and identification hashes

This file contains identification data only. None of the firmware described
below is distributed by BluMach.

## Olivetti PCS86

| Component | Expected filename | Size | SHA-256 |
|---|---|---:|---|
| Odd/high EPROM | `CSAB04_02-25.BIN` | 32768 | `82f8363ea7cde1fb8abe50fd76c7381831b7b89539d3dc71cbe4305fb1568d40` |
| Even/low EPROM | `CSAB05_02-17.BIN` | 32768 | `c92a79509def8aee30d76700a5ce1c7b6716d2375a64075c9669ca376fde4eb8` |
| Interleaved identification image | not distributed | 65536 | `d457e34bf0d138e7250b11e017fc01edbf5776dd4f2c93ea0a2497a73ccc097e` |

## Olivetti PCS 286

| Revision | Layout | SHA-256 |
|---|---|---|
| 1.34 | 64 KiB normalized/interleaved identification image | `888db2d5a9ed4f5c39b8167fb25f9ad26a1a42dddfe0ba2b7c1ccdafa04905d4` |
| 1.42 | 128 KiB normalized/interleaved identification image | `afbd051666869f3f58f23e52f9dd468fb9ad9f629d2dbccfda5324e83a897621` |
| 1.42 HIGH | original 64 KiB EPROM | `10f1d90ba9f9eafa7d89cc5cc48c605d0b4750df5c91d6127511ce1a74698ad1` |
| 1.42 LOW | original 64 KiB EPROM | `8bde0d14c7b42328ca27c4eb6f355de20b5aeaeaeeac3f5d51dc21cb70cb0e57` |

`62410C62.BIN` (`98c4dbeb22e9d77a645142c534134278096e571441e7db28578a53f9bf5e800b`,
16384 bytes) is not an M5L8042 keyboard-controller ROM. It contains host x86
code and must not be loaded into the UPI-42 core. The genuine M5L8042-243P
firmware remains missing.

## Olivetti PCS 286/S TI/OLIMCU16

BIOS/Resident Diagnostics 2.06, dated 20 May 1991:

| Component | Size | SHA-256 |
|---|---:|---|
| LOW EPROM | 65536 | `fa129de6f464742082867eac5d9d19323dfe4eec432d927a4a4815103e889dce` |
| HIGH EPROM | 65536 | `364170a7fd65c5d897e8328283ed91450a682a6733ccd0dedea1255e0c8d7d4c` |
| Interleaved identification image | 131072 | `6ef9fbb9167f03a5cd532deda7b9e77d6cf8505033f99d6f576c7246e44e29fc` |

Interleave rule: `FULL[2*n] = LOW[n]`, `FULL[2*n+1] = HIGH[n]`.

## PCS 386SX and Dario 386SX/P45

| Brand/image | Component | Size | SHA-256 |
|---|---|---:|---|
| Olivetti | LOW | 65536 | `75ea5c71cd1dacdf78e2514345416ea11ff23cb1bfe13340b622319ebdc8d260` |
| Olivetti | HIGH | 65536 | `b6c6f91deb44e069ec9240c8705cd5fbb4ed7e30a6b1b58ab1dcdc4500613b95` |
| Olivetti | FULL | 131072 | `d745353a44d97b50b7eeb42f313aa072cdb68b41f70540a6c4e80b32a0b7dc94` |
| Dario | LOW | 65536 | `8d05e5a84ab17ed06709cc38df1c0ec2282916975557224651b371ce6c514897` |
| Dario | HIGH | 65536 | `d8553b94259c2f742a2394721b73753f1ae011ee7e2049defecb08a2c81fb70a` |
| Dario | FULL | 131072 | `4d83c689642472674796e698a624ad0e6a3c0fdef73951f5688f7bbccf75184d` |

The regions `00000h–05FFFh` (OVC 1.06 video BIOS) and `10000h–1FFFFh`
(Phoenix system BIOS 1.14, 30 October 1991) are byte-identical. Differences are
confined to unused padding at `06000h–0FFFFh`: mostly `FFh` in the Dario dump
and `00h` in the Olivetti dump. Both original dumps remain historically useful.

## TriGem SX386M / Emerson Elite SX386/16

| Component | Expected filename | Size | SHA-256 |
|---|---|---:|---|
| AMI system BIOS | `EESX386.BIN` | 65536 | `7432e489151742782bc81165ccd8c6244f59a96cc62c8c048966f084a2a4f3e9` |

The independent TriGem firmware identifies itself as
`DG2X-6080-020491-KB` and is mapped linearly at F0000h. A reconstructed image
from the surviving Emerson EPROM pair is byte-identical. It is not distributed.
