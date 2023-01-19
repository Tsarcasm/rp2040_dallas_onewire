# windows script to copy files to D:
# file source is ./dallas_onewire.uf2

# We're in WSL but need to run through cmd.exe
# If the drive D: is not mounted this will fail
# Repeatedly loop until it succeeds
# This is a hack, but it works

#loop:


# powershell.exe -Command "cp ./build/dallas_onewire.uf2 D:" || powershell.exe -Command "echo 'Failed to copy to D:'"
DELAY=1
while true; do
    powershell.exe -Command "Copy-Item -Path ./dallas_onewire.uf2 -Destination D: -ErrorAction SilentlyContinue"
    if [ $? -eq 0 ]; then
        echo "[Flashed]"
        break
    fi
    echo -n "."
    sleep $DELAY
done
