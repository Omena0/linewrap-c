
# Line wrap algorithm

Minimizes `sum(abs(deltaSpaceWidth))`
with some additional rules to make it look better.

Usage:

```sh
./linewrap <file.txt> > out.txt
cat file.txt | ./linewrap > out.txt
```

You can either specify a filename as an arg or

Build:

Just run `make`.
You need FreeType installed.
