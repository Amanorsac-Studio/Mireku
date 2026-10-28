#!/bin/bash
# Uninstall MIREKU.command - removes everything the MIREKU installer placed.
# The sample library, the presets and the performances in ~/Documents/MIREKU are
# NOT touched. Delete that folder separately to remove them as well.
echo "This removes MIREKU from this Mac. The library, presets and performances are kept."
read -r -p "Remove MIREKU? (y/n) " answer
[ "$answer" = "y" ] || { echo "Nothing was removed."; exit 0; }
sudo rm -rf "/Applications/MIREKU.app" \
            "/Library/Audio/Plug-Ins/VST3/MIREKU.vst3" \
            "/Library/Audio/Plug-Ins/Components/MIREKU.component" \
            "/Library/Application Support/Amanorsac Studio/MIREKU"
for id in app vst3 au docs; do sudo pkgutil --forget "com.amanorsac.mireku.$id" >/dev/null 2>&1; done
rm -rf "$HOME/Library/Application Support/Amanorsac Studio/MIREKU"
echo "MIREKU has been removed. The library, presets and performances are still in ~/Documents/MIREKU."
