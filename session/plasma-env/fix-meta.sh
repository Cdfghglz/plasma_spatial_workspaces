#!/bin/sh
# Clear any keys in mod4 except Super_L and Super_R

# Get the keysyms from mod4, ignore commas
keys=$(xmodmap -pm | awk '/mod4/ {for(i=2;i<=NF;i++) gsub(",","",$i); print $i}')

for k in $keys; do
    # Only remove keys that are not Super_L or Super_R
    case "$k" in
        Super_L|Super_R)
            ;;
        *)
            xmodmap -e "remove mod4 = $k"
            ;;
    esac
done

# Add the correct keys to mod4
xmodmap -e "add mod4 = Super_L Super_R"
