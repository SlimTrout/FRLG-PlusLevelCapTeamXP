# Brainy Blue Co-op host (prototype)

`coophost.c` runs two copies of the Brainy Blue ROM side by side on the mGBA core
and relays the co-op mailbox (`gCoopMailbox`, see `include/coop.h`) between them
every frame. It is the test version of the co-op app: the real app does the same
relay over the network, with a window, sound and controls.

Build against a static libmgba (mGBA 0.10+) with the same defines libmgba was built with:

    gcc -O2 -std=c11 <libmgba defines> -I<mgba>/include -I<mgba>/build/include -I<mgba>/src \
        coophost.c -o coophost <mgba>/build/libmgba.a -lpng -lfreetype -lz -lm -pthread

Run:

    ./coophost pokefirered.gba script.txt outdir saveA.sav saveB.sav

Script lines are `<frames> <keysA> <keysB> [shot]` (keys: A B S E U D l r L R, `-` for none).
