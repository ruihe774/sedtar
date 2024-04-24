# sedtar

Modify path names inside archives without extracting first.

Usage:

```shell
# strip root directory
sedtar 's|^[^/]*/||' src.tar > dst.tar

# rename a pattern
sedtar 's|awd|dwa|' src.zip > dst.zip

# remove a path
sedtar 's|^unused/.*||' src.7z > dst.tar.xz
```
