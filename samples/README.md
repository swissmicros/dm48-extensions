<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright © 2026 SwissMicros GmbH -->
# Samples

One folder per sample extension. Each folder holds its C sources and a Makefile shaped like `template/Makefile`
(`make` builds `NAME.ext`), so CI can build it with `make -C samples/NAME all`.
